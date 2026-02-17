/**
 * @file mb_reg_dayue.c
 * @brief 大岳协议 Modbus 寄存器回调实现
 * @note  支持功能码 0x03/0x06/0x10
 *        - 0x03: 读保持寄存器（各种状态查询）
 *        - 0x06: 写单个寄存器（采样/排空/送样/远程控制）
 *        - 0x10: 写多个寄存器（因子数据/留样触发/时间设置）
 */

#include "mb.h"
#include "work.h"
#include "freertos_app.h"
#include "sampling.h"
#include "Commtrigger.h"
#include "record_cache.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* 桶状态定义（用于通讯协议判断） */
#define STATE_BUCKET_EMPTY 0
#define STATE_BUCKET_FULL 1

/* ===== 外部变量声明 ===== */

/* 系统状态 */
extern State g_State;
extern SampleConfig g_SampleConfig;
extern RetainSampleModeConfig g_RetainSampleConfig;
extern CommSettingConfig g_CommSettingConfig;
extern SystemSettingConfig g_SystemSettingConfig;
extern RetainBottleState g_RetainBottleState;

/* 大岳协议命令状态 */
extern DayueCommandStatus_t g_dayue_cmd_status;

/* 通讯触发留样相关 */
extern CommRetainWindowContext g_comm_retain_ctx_a;
extern CommRetainWindowContext g_comm_retain_ctx_b;
extern uint8_t g_comm_retain_bottle_count;
extern CommTriggerRequest g_comm_trigger_request;

/* 因子数据 */
extern FactorData_t g_FactorDataFromHost[MAX_FACTOR_COUNT];
extern uint8_t g_FactorCount;

/* 留样信息 */
extern RetainSampleInfo_t g_LastRetainInfo;
extern RetainSampleInfo_t g_LastInstantRetainInfo;
extern DiscardSampleInfo_t g_LastDiscardInfo;

/* 送样记录 */
extern uint8_t g_last_delivery_bucket;
extern uint32_t g_last_delivery_time;

/* 水样准备状态 */
extern uint8_t g_water_sample_ready_A;
extern uint8_t g_water_sample_ready_B;

/* 采样序列计数器 */
extern uint32_t g_sampling_sequence_A;
extern uint32_t g_sampling_sequence_B;

/* RTC 日历 */
extern calendar_type calendar;

/* ===== 外部函数声明 ===== */
extern uint8_t sampling_get_status(void);
extern uint8_t delivery_get_status(void);
extern void system_reset_start(void);
/* system_start_sequence 已在 sampling.h 中声明 */
/* rtc_time_get 和 rtc_time_set 已在 rtc.h 中声明 */
extern uint32_t rtc_counter_get(void);
extern uint8_t instant_retention_execute(uint8_t start_bottle, uint8_t bottle_count, uint8_t trigger_source);
extern uint8_t emptybottle(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);
/* log_retain_record 已在 sampling.h 中声明 */

/* ===== 内部辅助函数声明 ===== */
static uint8_t dayue_read_holding(MBInstance_t *inst, uint8_t *buf,
                                  uint16_t addr, uint16_t nregs);
static uint8_t dayue_write_holding(MBInstance_t *inst, uint8_t *buf,
                                   uint16_t addr, uint16_t nregs);

/* 十进制转BCD (如 30 → 0x30) */
static uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

/* 获取最新门禁事件时间戳 */
static uint32_t dayue_get_last_door_timestamp(void) {
    if (g_cache_mgr.door.count == 0) return 0;
    uint16_t newest_idx = (g_cache_mgr.door.window_end_idx == 0)
        ? (CACHE_CAPACITY_DOOR - 1)
        : (g_cache_mgr.door.window_end_idx - 1);
    return g_cache_mgr.door.events[newest_idx].timestamp;
}

/* 门禁开门超时检测 */
static uint32_t g_door_open_timestamp = 0;

/* 检测门禁开门超时 (返回: 0=正常, 2=开门超30分钟) */
static uint8_t dayue_check_door_timeout(void) {
    uint8_t door_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) ? 0 : 1;  // PE12: HIGH=关(0), LOW=开(1)
    uint32_t now = rtc_counter_get();

    if (door_state == 1) {  // 门开
        if (g_door_open_timestamp == 0) {
            g_door_open_timestamp = now;
        }
        if ((now - g_door_open_timestamp) > 30 * 60) {
            return 2;  // 开门超30分钟
        }
    } else {  // 门关
        g_door_open_timestamp = 0;
    }
    return 0;
}

/* ===== 输入寄存器回调（大岳协议不使用） ===== */

/**
 * @brief 大岳协议输入寄存器回调
 * @note  大岳协议不使用输入寄存器（0x04功能码），直接返回异常
 * @param inst Modbus 实例指针
 * @param buf 数据缓冲区
 * @param addr 起始地址（Modbus地址，0-based）
 * @param nregs 寄存器数量
 * @return 异常码（0=成功，非0=异常）
 */
uint8_t dayue_input_cb(MBInstance_t *inst, uint8_t *buf,
                       uint16_t addr, uint16_t nregs)
{
    (void)inst;
    (void)buf;
    (void)addr;
    (void)nregs;

    /* 大岳协议不使用输入寄存器 */
    printf("[大岳] 不支持读输入寄存器\r\n");
    return MB_EX_ILLEGAL_FUNCTION;
}

/* ===== 保持寄存器回调（核心函数） ===== */

/**
 * @brief 大岳协议保持寄存器回调
 * @note  处理所有读写操作（0x03/0x06/0x10功能码）
 * @param inst Modbus 实例指针
 * @param buf 数据缓冲区
 *            - 读操作：函数填充数据到buf（大端序，每个寄存器2字节）
 *            - 写操作：buf包含待写入数据（大端序）
 * @param addr 起始地址（Modbus地址，0-based）
 * @param nregs 寄存器数量
 * @param mode 操作模式（MB_REG_READ=读，MB_REG_WRITE=写）
 * @return 异常码（0=成功，非0=异常）
 */
uint8_t dayue_holding_cb(MBInstance_t *inst, uint8_t *buf,
                         uint16_t addr, uint16_t nregs, uint8_t mode)
{
    uint8_t result;

    if (mode == MB_REG_READ)
    {
        /* 读保持寄存器（0x03功能码） */
        result = dayue_read_holding(inst, buf, addr, nregs);
    }
    else
    {
        /* 写保持寄存器（0x06/0x10功能码） */
        result = dayue_write_holding(inst, buf, addr, nregs);
    }

    return result;
}

/* ===== Dayue read block helpers ===== */
#define DAYUE_MAX_BLOCK_REGS 118
#define DAYUE_READY_PULSE_SECONDS 60U

static uint32_t dayue_ready_ts_a = 0;
static uint32_t dayue_ready_ts_b = 0;
static uint8_t dayue_ready_last_a = 0;
static uint8_t dayue_ready_last_b = 0;

/**
 * @brief 填充请求范围与block交集部分的数据（支持跨block读取）
 * @param dst 目标缓冲区
 * @param req_addr 请求起始地址
 * @param req_nregs 请求寄存器数量
 * @param block_start block起始地址
 * @param block_regs block寄存器数量
 * @param build block数据构建函数
 */
static void dayue_fill_block_overlap(uint8_t *dst, uint16_t req_addr, uint16_t req_nregs,
                                     uint16_t block_start, uint16_t block_regs,
                                     uint16_t (*build)(uint8_t *buf, uint16_t buf_size))
{
    uint32_t req_end = (uint32_t)req_addr + req_nregs - 1;
    uint32_t block_end = (uint32_t)block_start + block_regs - 1;
    uint16_t overlap_start, overlap_end, overlap_regs;
    uint16_t src_offset, dst_offset;
    uint8_t block_buf[DAYUE_MAX_BLOCK_REGS * 2];

    /* 检查是否有交集 */
    if (req_end < block_start || req_addr > block_end) {
        return;
    }

    /* 计算交集范围 */
    overlap_start = (req_addr > block_start) ? req_addr : block_start;
    overlap_end = (req_end < block_end) ? (uint16_t)req_end : (uint16_t)block_end;
    overlap_regs = overlap_end - overlap_start + 1;

    /* 构建block数据 */
    if (build(block_buf, sizeof(block_buf)) == 0) {
        return;
    }

    /* 计算偏移并复制数据 */
    src_offset = (overlap_start - block_start) * 2;
    dst_offset = (overlap_start - req_addr) * 2;
    memcpy(&dst[dst_offset], &block_buf[src_offset], overlap_regs * 2);
}

static void dayue_update_ready_latch(uint32_t now)
{
    if (g_water_sample_ready_A && !dayue_ready_last_a) {
        dayue_ready_ts_a = now;
    }
    if (g_water_sample_ready_B && !dayue_ready_last_b) {
        dayue_ready_ts_b = now;
    }

    dayue_ready_last_a = g_water_sample_ready_A;
    dayue_ready_last_b = g_water_sample_ready_B;
}

static uint8_t dayue_bucket_ready_pulse(uint8_t bucket, uint32_t now)
{
    uint32_t ts = bucket ? dayue_ready_ts_b : dayue_ready_ts_a;
    if (ts == 0) {
        return 0;
    }
    return (now - ts) <= DAYUE_READY_PULSE_SECONDS;
}

static uint8_t dayue_bucket_ready_expired(uint8_t bucket, uint32_t now)
{
    uint32_t *ts = bucket ? &dayue_ready_ts_b : &dayue_ready_ts_a;
    if (*ts == 0) {
        return 0;
    }
    if ((now - *ts) > DAYUE_READY_PULSE_SECONDS) {
        *ts = 0;
        return 1;
    }
    return 0;
}

static uint16_t dayue_build_bucket_status_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    uint16_t bucket = 0xFFFF;
    uint16_t ready = 0;
    uint32_t now = rtc_counter_get();
    uint8_t expired_a;
    uint8_t expired_b;
    uint8_t has_activity;

    (void)buf_size;

    dayue_update_ready_latch(now);
    expired_a = dayue_bucket_ready_expired(0, now);
    expired_b = dayue_bucket_ready_expired(1, now);

    has_activity = (g_State.State != 0) ||
                   (g_State.ABucketState != 0) ||
                   (g_State.BBucketState != 0) ||
                   g_water_sample_ready_A ||
                   g_water_sample_ready_B ||
                   (dayue_ready_ts_a != 0) ||
                   (dayue_ready_ts_b != 0);

    if (has_activity) {
        if (g_State.InletThreeWayValve == 1) {
            bucket = 0x0000;
        } else if (g_State.InletThreeWayValve == 2) {
            bucket = 0x0001;
        }

        if (bucket == 0x0000 && expired_a) {
            bucket = 0x0001;
        } else if (bucket == 0x0001 && expired_b) {
            bucket = 0x0000;
        }

        if (bucket == 0x0000) {
            ready = dayue_bucket_ready_pulse(0, now);
        } else if (bucket == 0x0001) {
            ready = dayue_bucket_ready_pulse(1, now);
        }
    }

    /* 0x0000: special command (write-only, return 0 on read) */
    buf[idx++] = 0;
    buf[idx++] = 0;
    /* 0x0001: current mixed bucket */
    buf[idx++] = (bucket >> 8) & 0xFF;
    buf[idx++] = bucket & 0xFF;
    /* 0x0002: ready flag */
    buf[idx++] = 0;
    buf[idx++] = ready ? 1 : 0;
    return idx / 2;
}

#if 0
static uint16_t dayue_build_ready_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    uint16_t ready = (g_water_sample_ready_A || g_water_sample_ready_B) ? 0x0001 : 0x0000;
    buf[idx++] = (ready >> 8) & 0xFF;
    buf[idx++] = ready & 0xFF;
    return idx / 2;
}

#endif

/**
 * @brief 内部桶状态转换为大岳协议桶状态
 * @param internal_state 内部状态值 (0-50)
 * @return 大岳协议状态值
 *
 * 内部状态定义 (freertos_app.h):
 *   0:空闲 8:采样中 19:供样中 38:留样中 45:排空中 等
 *
 * 大岳协议定义:
 *   0x0000:停止 0x0001:润洗 0x0004:采样 0x0006:供样
 *   0x0009:留样 0x000C:混匀桶排空 0x000D:待机
 */
static uint8_t dayue_convert_bucket_state(uint8_t internal_state)
{
    switch (internal_state) {
        case 0:  return 0x00;  // 空闲 -> 停止
        case 1:  return 0x01;  // 润洗
        case 2:  return 0x02;  // 润洗管路排空
        case 3:  return 0x03;  // 采样管存
        case 8:  return 0x04;  // 采样中 -> 采样
        case 5:  return 0x05;  // 采样管排空
        case 19: return 0x06;  // 供样中 -> 供样
        case 7:  return 0x07;  // 供样排空
        case 37: return 0x08;  // 留样管存
        case 38: return 0x09;  // 留样中 -> 留样
        case 30: return 0x0A;  // 瓶位调整
        case 42: return 0x0B;  // 留样排空
        case 45: return 0x0C;  // 排空中 -> 混匀桶排空
        default: return 0x0D;  // 其他 -> 待机
    }
}

static uint16_t dayue_build_device_state_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    buf[idx++] = 0;
    buf[idx++] = g_State.State;

    buf[idx++] = 0;
    buf[idx++] = dayue_convert_bucket_state(g_State.ABucketState);

    buf[idx++] = 0;
    buf[idx++] = dayue_convert_bucket_state(g_State.BBucketState);

    // 0x0006: A桶液位状态 (0=未到位, 1=到位)
    {
        uint32_t target_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        uint8_t level_a = (g_sampling_sequence_A >= target_count) ? 1 : 0;
        buf[idx++] = 0;
        buf[idx++] = level_a;
    }

    // 0x0007: B桶液位状态 (0=未到位, 1=到位)
    {
        uint32_t target_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        uint8_t level_b = (g_sampling_sequence_B >= target_count) ? 1 : 0;
        buf[idx++] = 0;
        buf[idx++] = level_b;
    }

    uint8_t samp_status = sampling_get_status();
    uint16_t sample_state = (samp_status == 3 || samp_status == 4) ? 1 : 0;
    buf[idx++] = 0;
    buf[idx++] = sample_state;

    uint8_t deliv_status = delivery_get_status();
    uint16_t delivery_state = (deliv_status == 3 || deliv_status == 4) ? 1 : 0;
    buf[idx++] = 0;
    buf[idx++] = delivery_state;

    // 0x000A: 仪器留样状态 (0=正常, 1=失败)
    // 优先检测转盘系统故障，其次检查最近留样结果
    {
        uint8_t retain_state = 0;  // 默认正常
        if (bottle_is_fault_active()) {
            retain_state = 1;  // 转盘系统故障
        } else if (g_LastRetainInfo.result == 0) {
            retain_state = 1;  // 最近留样失败
        }
        buf[idx++] = 0;
        buf[idx++] = retain_state;
    }

    // 0x000B: 仪器故障状态 (7=无可用留样瓶)
    // TODO: 传感器安装到位后补充其他故障类型(1-6)
    {
        uint16_t used = 0;
        for (int i = 0; i < 24; i++) {
            if (g_RetainBottleState.usedMask & (1 << i)) used++;
        }
        buf[idx++] = 0;
        buf[idx++] = (used >= 24) ? 7 : 0;
    }

    uint16_t usedCount = 0;
    for (int i = 0; i < 24; i++)
    {
        if (g_RetainBottleState.usedMask & (1 << i))
        {
            usedCount++;
        }
    }
    buf[idx++] = (usedCount >> 8) & 0xFF;
    buf[idx++] = usedCount & 0xFF;

    for (int i = 0; i < 24; i++)
    {
        uint16_t bottleState = 0x0000;
        if (g_RetainBottleState.usedMask & (1 << i))
        {
            uint16_t vol = g_RetainSampleConfig.SampleVolume;
            if (vol < 16)
                vol = 16;
            if (vol > 1000)
                vol = 1000;
            bottleState = vol;
        }
        buf[idx++] = (bottleState >> 8) & 0xFF;
        buf[idx++] = bottleState & 0xFF;
    }

    float temperature = 4.0f;
    uint8_t tempBytes[4];
    convertFloatToBytes(temperature, tempBytes);
    buf[idx++] = tempBytes[0];
    buf[idx++] = tempBytes[1];
    buf[idx++] = tempBytes[2];
    buf[idx++] = tempBytes[3];

    buf[idx++] = 0;
    buf[idx++] = (g_State.InletThreeWayValve == 1) ? 1 : 0;

    buf[idx++] = 0;
    buf[idx++] = (g_State.InletThreeWayValve == 2) ? 1 : 0;

    buf[idx++] = 0;
    buf[idx++] = g_State.DrainA;

    buf[idx++] = 0;
    buf[idx++] = g_State.DrainB;

    uint8_t mix_a_state = gpio_input_data_bit_read(GPIOB, GPIO_PINS_10) ? 0 : 1;
    buf[idx++] = 0;
    buf[idx++] = mix_a_state;

    uint8_t mix_b_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_15) ? 0 : 1;
    buf[idx++] = 0;
    buf[idx++] = mix_b_state;

    uint8_t external_pump_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_11) ? 0 : 1;
    buf[idx++] = 0;
    buf[idx++] = external_pump_state;

    buf[idx++] = 0;
    buf[idx++] = g_State.SamplingMotor;

    buf[idx++] = 0;
    buf[idx++] = g_State.SampleThreeWayValve;

    buf[idx++] = 0;
    buf[idx++] = g_State.OutletThreeWayValve;

    // 0x0031: 留样蠕动泵状态 (与送样泵是同一个泵)
    buf[idx++] = 0;
    buf[idx++] = g_State.DeliveryMotor;

    buf[idx++] = 0;
    buf[idx++] = g_State.DeliveryMotor;

    // 0x0033: 门禁报警状态 (0=正常, 2=开门超30分钟)
    buf[idx++] = 0;
    buf[idx++] = dayue_check_door_timeout();

    uint8_t door_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) ? 0 : 1;  // PE12: HIGH=关(0), LOW=开(1)
    buf[idx++] = 0;
    buf[idx++] = door_state;

    // 0x0035~0x003A: 门禁操作时间 (BCD格式)
    uint32_t door_ts = dayue_get_last_door_timestamp();
    if (door_ts > 0) {
        time_t ts = (time_t)door_ts;
        struct tm *t = localtime(&ts);
        uint8_t year = (t->tm_year + 1900 - 2000);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(year);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(t->tm_mon + 1);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(t->tm_mday);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(t->tm_hour);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(t->tm_min);
        buf[idx++] = 0; buf[idx++] = dec_to_bcd(t->tm_sec);
    } else {
        for (int i = 0; i < 6; i++) {
            buf[idx++] = 0;
            buf[idx++] = 0;
        }
    }

    uint32_t cardId = g_SystemSettingConfig.CardId[0];
    buf[idx++] = (cardId >> 24) & 0xFF;
    buf[idx++] = (cardId >> 16) & 0xFF;
    buf[idx++] = (cardId >> 8) & 0xFF;
    buf[idx++] = cardId & 0xFF;

    buf[idx++] = 0x27;
    buf[idx++] = 0x0F;

    // 0x003E~0x0041: 样品编号 (4个寄存器)
    buf[idx++] = (g_LastRetainInfo.sampleId[0] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[0] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[1] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[1] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[2] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[2] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[3] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[3] & 0xFF;

    // 0x0042~0x0050: 保留区域 (15个寄存器)
    for (int i = 0; i < 15; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    return idx / 2;
}

static uint16_t dayue_build_status_device_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t regs = 0;
    uint16_t chunk = 0;

    if (buf_size < (0x003E * 2)) {
        return 0;
    }

    chunk = dayue_build_bucket_status_block(buf, buf_size);
    regs += chunk;
    if ((regs * 2) < buf_size) {
        chunk = dayue_build_device_state_block(buf + (regs * 2), buf_size - (regs * 2));
        regs += chunk;
    }

    return regs;
}

static uint16_t dayue_build_retain_exceed_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    buf[idx++] = 0x20;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.year - 2000);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.month);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.day);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.hour);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.minute);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.second);

    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.result & 0xFF;

    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.failReason & 0xFF;

    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.bottleCount & 0xFF;

    // 0x005A: 超标因子总个数 (统计启用的通道数)
    uint8_t factor_count = 0;
    for (int i = 0; i < 6; i++) {
        if (g_RetainSampleConfig.channelLimits[i].Enable) {
            factor_count++;
        }
    }
    buf[idx++] = 0x00;
    buf[idx++] = factor_count;

    // 0x005B~0x0084: 超标因子1-6详情 (每个因子7个寄存器)
    // 因子编号(1) + 因子值(2,浮点) + 下限(2,浮点) + 上限(2,浮点)
    for (int i = 0; i < 6; i++) {
        // 因子编号
        buf[idx++] = 0x00;
        buf[idx++] = g_RetainSampleConfig.channelLimits[i].FactorType;

        // 因子值 (浮点数)
        float value = g_RetainSampleConfig.channelData[i];
        uint8_t valBytes[4];
        convertFloatToBytes(value, valBytes);
        buf[idx++] = valBytes[0];
        buf[idx++] = valBytes[1];
        buf[idx++] = valBytes[2];
        buf[idx++] = valBytes[3];

        // 下限 (浮点数)
        float lower = g_RetainSampleConfig.channelLimits[i].LowerLimit;
        uint8_t loBytes[4];
        convertFloatToBytes(lower, loBytes);
        buf[idx++] = loBytes[0];
        buf[idx++] = loBytes[1];
        buf[idx++] = loBytes[2];
        buf[idx++] = loBytes[3];

        // 上限 (浮点数)
        float upper = g_RetainSampleConfig.channelLimits[i].UpperLimit;
        uint8_t hiBytes[4];
        convertFloatToBytes(upper, hiBytes);
        buf[idx++] = hiBytes[0];
        buf[idx++] = hiBytes[1];
        buf[idx++] = hiBytes[2];
        buf[idx++] = hiBytes[3];
    }

    // 0x0085~0x00C6: 瓶1-6留样详情 (每瓶11个寄存器)
    // 瓶号(1) + 结果(1) + 容积(1) + 失败原因(1) + 第几瓶(1) + 时间(6,BCD)
    for (int bottle = 0; bottle < 6; bottle++) {
        if (bottle < g_LastRetainInfo.bottleCount) {
            // 有效瓶信息
            buf[idx++] = 0x00;
            buf[idx++] = (g_LastRetainInfo.startBottle + bottle) & 0xFF;  // 瓶号
            buf[idx++] = 0x00;
            buf[idx++] = g_LastRetainInfo.result & 0xFF;  // 结果
            buf[idx++] = (g_LastRetainInfo.volume >> 8) & 0xFF;  // 容积
            buf[idx++] = g_LastRetainInfo.volume & 0xFF;
            buf[idx++] = 0x00;
            buf[idx++] = g_LastRetainInfo.failReason & 0xFF;  // 失败原因
            buf[idx++] = 0x00;
            buf[idx++] = (bottle + 1) & 0xFF;  // 第几瓶
            // 留样时间 (BCD)
            buf[idx++] = 0x20;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.year - 2000);
            buf[idx++] = 0x00;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.month);
            buf[idx++] = 0x00;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.day);
            buf[idx++] = 0x00;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.hour);
            buf[idx++] = 0x00;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.minute);
            buf[idx++] = 0x00;
            buf[idx++] = dec_to_bcd(g_LastRetainInfo.second);
        } else {
            // 空瓶信息 (11个寄存器 = 22字节)
            for (int j = 0; j < 22; j++) {
                buf[idx++] = 0x00;
            }
        }
    }

    return idx / 2;
}

static uint16_t dayue_build_retain_normal_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x00C7: 保留
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00C8: 保留
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00C9~0x00CE: 留样时间 (BCD格式, 6个寄存器)
    buf[idx++] = 0x20;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.year - 2000);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.month);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.day);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.hour);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.minute);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastRetainInfo.second);

    // 0x00CF: 执行结果
    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.result & 0xFF;

    // 0x00D0: 执行失败原因
    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.failReason & 0xFF;

    // 0x00D1: 留样起始瓶号
    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.startBottle & 0xFF;

    // 0x00D2: 留样总瓶数
    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.bottleCount & 0xFF;

    // 0x00D3: 留样量
    buf[idx++] = (g_LastRetainInfo.volume >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.volume & 0xFF;

    // 0x00D4: 留样模式
    buf[idx++] = 0x00;
    buf[idx++] = g_LastRetainInfo.mode & 0xFF;

    // 0x00D5: 留样触发条件
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00D6: 是否添加药剂
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00D7: 药剂类型 (0x0000=硫酸)
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00D8: 保留
    buf[idx++] = 0x00;
    buf[idx++] = 0x00;

    // 0x00D9~0x00DC: 样品编号 (4个寄存器)
    buf[idx++] = (g_LastRetainInfo.sampleId[0] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[0] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[1] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[1] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[2] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[2] & 0xFF;
    buf[idx++] = (g_LastRetainInfo.sampleId[3] >> 8) & 0xFF;
    buf[idx++] = g_LastRetainInfo.sampleId[3] & 0xFF;

    return idx / 2;  // 返回22
}

static uint16_t dayue_build_sampling_params_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x0180: 采样模式
    // 内部定义: 1-时间等比 2-通讯触发 3-开关量触发
    // 大岳协议: 0-远程控制 1-定时采样 2-时间等比 3-流量等比 4-流量跟随 5-开关触发 6-串口控制(通讯触发) 7-其他
    uint8_t dayue_mode;
    switch (g_SampleConfig.SamplingMode) {
        case 1:  dayue_mode = 2; break;  // 时间等比 -> 时间等比
        case 2:  dayue_mode = 6; break;  // 通讯触发 -> 串口控制
        case 3:  dayue_mode = 5; break;  // 开关量触发 -> 开关触发
        default: dayue_mode = 7; break;  // 其他
    }
    buf[idx++] = 0;
    buf[idx++] = dayue_mode;

    // 0x0181: 单次采样量
    buf[idx++] = (g_SampleConfig.SampleVolume >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.SampleVolume & 0xFF;

    // 0x0182: 时间等比间隔
    buf[idx++] = (g_SampleConfig.SampleInterval >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.SampleInterval & 0xFF;

    // 0x0183: 排放等比排放量 (单位M³)
    buf[idx++] = (g_SampleConfig.DischargeVolume >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.DischargeVolume & 0xFF;

    // 0x0184: 流量比例间隔
    buf[idx++] = (g_SampleConfig.SampleInterval >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.SampleInterval & 0xFF;

    // 0x0185: 流量比例采样比例
    buf[idx++] = (g_SampleConfig.FlowRatio >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.FlowRatio & 0xFF;

    // 0x0186: 流量触发阈值 (单位M³/h)
    buf[idx++] = (g_SampleConfig.FlowStart >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.FlowStart & 0xFF;

    // 0x0187: 流量触发间隔 (复用SampleInterval，单位分钟)
    buf[idx++] = (g_SampleConfig.SampleInterval >> 8) & 0xFF;
    buf[idx++] = g_SampleConfig.SampleInterval & 0xFF;

    // 0x0188~0x0197: 保留 (16个寄存器)
    for (int i = 0; i < 16; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    return 24;  // 0x0180~0x0197: 24个寄存器
}

static uint16_t dayue_build_retain_instant_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;


    buf[idx++] = 0x20;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.year - 2000);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.month);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.day);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.hour);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.minute);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastInstantRetainInfo.second);

    buf[idx++] = 0x00;
    buf[idx++] = g_LastInstantRetainInfo.result & 0xFF;

    buf[idx++] = 0x00;
    buf[idx++] = g_LastInstantRetainInfo.failReason & 0xFF;

    buf[idx++] = 0x00;
    buf[idx++] = g_LastInstantRetainInfo.startBottle & 0xFF;

    buf[idx++] = 0x00;
    buf[idx++] = g_LastInstantRetainInfo.bottleCount & 0xFF;

    buf[idx++] = (g_LastInstantRetainInfo.volume >> 8) & 0xFF;
    buf[idx++] = g_LastInstantRetainInfo.volume & 0xFF;

    return 10;  // 0x00DD~0x00E6: 10个寄存器
}

// 阶段6: 弃样记录信息 (0x00E7~0x00F4, 14个寄存器)
// 协议定义:
//   0x00E7~0x00EC: 弃样时间 (BCD格式, 6个寄存器)
//   0x00ED: 执行结果 (0=失败, 1=成功)
//   0x00EE: 执行失败原因
//   0x00EF: 留样起始瓶号 (1~24)
//   0x00F0: 留样总瓶数 (1~24)
//   0x00F1~0x00F4: 样品编号 (4个寄存器)
static uint16_t dayue_build_discard_record_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x00E7~0x00EC: 弃样时间 (BCD格式, 6个寄存器)
    buf[idx++] = 0x20;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.year - 2000);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.month);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.day);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.hour);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.minute);
    buf[idx++] = 0x00;
    buf[idx++] = dec_to_bcd(g_LastDiscardInfo.second);

    // 0x00ED: 执行结果 (0=失败, 1=成功)
    buf[idx++] = 0x00;
    buf[idx++] = g_LastDiscardInfo.result & 0xFF;

    // 0x00EE: 执行失败原因
    buf[idx++] = 0x00;
    buf[idx++] = g_LastDiscardInfo.failReason & 0xFF;

    // 0x00EF: 留样起始瓶号 (1~24)
    buf[idx++] = 0x00;
    buf[idx++] = g_LastDiscardInfo.startBottle & 0xFF;

    // 0x00F0: 留样总瓶数 (1~24)
    buf[idx++] = 0x00;
    buf[idx++] = g_LastDiscardInfo.bottleCount & 0xFF;

    // 0x00F1~0x00F4: 样品编号 (4个寄存器, BCD格式)
    buf[idx++] = (g_LastDiscardInfo.sampleId[0] >> 8) & 0xFF;
    buf[idx++] = g_LastDiscardInfo.sampleId[0] & 0xFF;
    buf[idx++] = (g_LastDiscardInfo.sampleId[1] >> 8) & 0xFF;
    buf[idx++] = g_LastDiscardInfo.sampleId[1] & 0xFF;
    buf[idx++] = (g_LastDiscardInfo.sampleId[2] >> 8) & 0xFF;
    buf[idx++] = g_LastDiscardInfo.sampleId[2] & 0xFF;
    buf[idx++] = (g_LastDiscardInfo.sampleId[3] >> 8) & 0xFF;
    buf[idx++] = g_LastDiscardInfo.sampleId[3] & 0xFF;

    return 14;  // 0x00E7~0x00F4: 14个寄存器
}

// 阶段7: 保留区域 (0x00F5~0x0129, 53个寄存器)
static uint16_t dayue_build_reserved_block_1(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x00F5~0x0129: 保留区域，全部返回0
    for (int i = 0; i < 53; i++) {
        buf[idx++] = 0x00;
        buf[idx++] = 0x00;
    }

    return 53;
}

// 阶段8: 制水完成时间 (0x012A~0x012F, 6个寄存器)
static uint16_t dayue_build_water_make_time_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x012A~0x012F: 制水完成时间 (BCD格式)
    // 暂无制水功能，返回0
    for (int i = 0; i < 6; i++) {
        buf[idx++] = 0x00;
        buf[idx++] = 0x00;
    }

    return 6;
}

// 阶段9: 水质因子超标设置 (0x0130~0x015F, 48个寄存器)
// 协议定义: 6个因子，每个因子8个寄存器
//   因子编号(1) + 超标下限(2,浮点) + 超标上限(2,浮点) + 平行样数(1) + 留样间隔(1) + 启用(1)
static uint16_t dayue_build_water_quality_factor_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x0130~0x015F: 水质因子超标设置 (6个因子 × 8寄存器 = 48寄存器)
    for (int i = 0; i < 6; i++) {
        // 因子编号 (1个寄存器)
        buf[idx++] = 0x00;
        buf[idx++] = g_RetainSampleConfig.channelLimits[i].FactorType;

        // 超标下限 (2个寄存器，浮点数)
        float lower = g_RetainSampleConfig.channelLimits[i].LowerLimit;
        uint8_t loBytes[4];
        convertFloatToBytes(lower, loBytes);
        buf[idx++] = loBytes[0];
        buf[idx++] = loBytes[1];
        buf[idx++] = loBytes[2];
        buf[idx++] = loBytes[3];

        // 超标上限 (2个寄存器，浮点数)
        float upper = g_RetainSampleConfig.channelLimits[i].UpperLimit;
        uint8_t hiBytes[4];
        convertFloatToBytes(upper, hiBytes);
        buf[idx++] = hiBytes[0];
        buf[idx++] = hiBytes[1];
        buf[idx++] = hiBytes[2];
        buf[idx++] = hiBytes[3];

        // 超标平行样数 (1个寄存器)
        buf[idx++] = 0x00;
        buf[idx++] = g_RetainSampleConfig.channelLimits[i].ParallelCount;

        // 超标留样间隔 (1个寄存器，单位小时)
        uint16_t interval = g_RetainSampleConfig.channelLimits[i].RetainInterval;
        buf[idx++] = (interval >> 8) & 0xFF;
        buf[idx++] = interval & 0xFF;

        // 启用/停用 (1个寄存器)
        buf[idx++] = 0x00;
        buf[idx++] = g_RetainSampleConfig.channelLimits[i].Enable;
    }

    return 48;  // 0x0130~0x015F: 48个寄存器
}

// 阶段10: 保留区域 (0x0160~0x017F, 32个寄存器)
static uint16_t dayue_build_reserved_block_2(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    for (int i = 0; i < 32; i++) {
        buf[idx++] = 0x00;
        buf[idx++] = 0x00;
    }

    return 32;
}

// 阶段12: 留样模式参数 (0x0198~0x01AC, 21个寄存器)
static uint16_t dayue_build_retain_params_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x0198: 留样模式
    buf[idx++] = 0;
    buf[idx++] = g_RetainSampleConfig.Mode & 0xFF;

    // 0x0199: 单次留样量 (10~1000mL)
    buf[idx++] = (g_RetainSampleConfig.SampleVolume >> 8) & 0xFF;
    buf[idx++] = g_RetainSampleConfig.SampleVolume & 0xFF;

    // 0x019A: 平行样数 (1~6瓶)
    buf[idx++] = 0;
    buf[idx++] = g_RetainSampleConfig.ParallelCount & 0xFF;

    // 0x019B: 单瓶混合次数 (1~99次)
    buf[idx++] = 0;
    buf[idx++] = g_RetainSampleConfig.MixCount & 0xFF;

    // 0x019C: 是否加药 (0=否, 1=是)
    buf[idx++] = 0;
    buf[idx++] = g_RetainSampleConfig.EnableAcid & 0xFF;

    // 0x019D: 药剂类型 (只读, 系统不支持加药功能, 硬编码0)
    buf[idx++] = 0;
    buf[idx++] = 0;

    // 0x019E: 留样瓶加药比 (只读, 系统不支持加药功能, 硬编码0)
    buf[idx++] = 0;
    buf[idx++] = 0;

    // 0x019F~0x01AC: 保留 (14个寄存器)
    for (int i = 0; i < 14; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    return 21;
}

// 阶段14: 因子数据读取 (0x01B0~0x01E0, 49个寄存器)
static uint16_t dayue_build_factor_data_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x01B0~0x01CD: 因子测量值 (最多10个因子，每个3寄存器)
    for (int i = 0; i < 10; i++) {
        if (i < g_FactorCount) {
            // 因子编号
            buf[idx++] = (g_FactorDataFromHost[i].factorType >> 8) & 0xFF;
            buf[idx++] = g_FactorDataFromHost[i].factorType & 0xFF;
            // 因子值 (浮点数高16位) - 使用memcpy避免strict aliasing问题
            uint32_t fval;
            memcpy(&fval, &g_FactorDataFromHost[i].factorValue, sizeof(fval));
            buf[idx++] = (fval >> 24) & 0xFF;
            buf[idx++] = (fval >> 16) & 0xFF;
            // 因子值 (浮点数低16位)
            buf[idx++] = (fval >> 8) & 0xFF;
            buf[idx++] = fval & 0xFF;
        } else {
            for (int j = 0; j < 6; j++) buf[idx++] = 0;
        }
    }

    // 0x01CE~0x01E0: 保留 (19个寄存器)
    for (int i = 0; i < 19; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    return 49;
}

// 阶段15: 门禁卡号+设备信息 (0x01E7~0x0215, 47个寄存器)
static uint16_t dayue_build_door_card_device_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x01E7~0x01FA: 门禁卡号 (10个卡号，每个2寄存器)
    for (int i = 0; i < 10; i++) {
        uint32_t cardId = g_SystemSettingConfig.CardId[i];
        buf[idx++] = (cardId >> 24) & 0xFF;
        buf[idx++] = (cardId >> 16) & 0xFF;
        buf[idx++] = (cardId >> 8) & 0xFF;
        buf[idx++] = cardId & 0xFF;
    }

    // 0x01FB~0x01FC: 动态密码 (2个寄存器)
    // 算法: 999999 - 日时分(DDHHMI), 例如24日19:09 -> 999999-241909=758090
    rtc_time_get();
    uint32_t dynamic_code = 999999 - (calendar.date * 10000 + calendar.hour * 100 + calendar.min);
    buf[idx++] = (dynamic_code >> 24) & 0xFF;
    buf[idx++] = (dynamic_code >> 16) & 0xFF;
    buf[idx++] = (dynamic_code >> 8) & 0xFF;
    buf[idx++] = dynamic_code & 0xFF;

    // 0x01FD~0x01FF: 密码 (3个寄存器)
    for (int i = 0; i < 3; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    // 0x0200~0x020E: 保留 (15个寄存器)
    for (int i = 0; i < 15; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    // 0x020F: 联网地址 (1个寄存器)
    buf[idx++] = 0;
    buf[idx++] = g_CommSettingConfig.DeviceAddr;

    // 0x0210~0x0215: 出厂编号 (6个寄存器, ASCII文本)
    const char *serial = g_SystemSettingConfig.SoftwareSerial;
    for (int i = 0; i < 6; i++) {
        buf[idx++] = serial[i * 2];
        buf[idx++] = serial[i * 2 + 1];
    }

    return 47;
}

// 阶段16: 桶操作控制 (0x0216~0x021B, 6个寄存器)
static uint16_t dayue_build_bucket_control_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x0216: 触发采样状态
    buf[idx++] = 0;
    buf[idx++] = 0;

    // 0x0217: 触发排空状态
    buf[idx++] = 0;
    buf[idx++] = 0;

    // 0x0218: 触发送样状态
    buf[idx++] = 0;
    buf[idx++] = 0;

    // 0x0219~0x021B: 保留
    for (int i = 0; i < 3; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    return 6;
}

// 阶段17: 保留+授权码 (0x0221~0x0258, 56个寄存器)
static uint16_t dayue_build_reserved_auth_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 0x0221~0x0247: 保留区域 (39个寄存器)
    for (int i = 0; i < 39; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    // 0x0248~0x0257: 授权码 (16个寄存器)
    for (int i = 0; i < 16; i++) {
        buf[idx++] = 0;
        buf[idx++] = 0;
    }

    // 0x0258: 保留
    buf[idx++] = 0;
    buf[idx++] = 0;

    return 56;
}

static uint16_t dayue_build_delivery_ready_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;


    uint8_t delivery_ready = 0;
    if ((g_State.ABucketState == 1 || g_State.BBucketState == 1))
    {
        delivery_ready = 1;
    }
    if (delivery_get_status() == 0)
    {
        if (g_State.ABucketState == 1 || g_State.BBucketState == 1)
        {
            delivery_ready = 1;
        }
    }
    else
    {
        delivery_ready = 0;
    }

    buf[idx++] = 0x00;
    buf[idx++] = delivery_ready;

    return idx / 2;
}

// 0x021D: 送样同步信号 (1个寄存器)
static uint16_t dayue_build_delivery_sync_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    // 送样同步信号状态 (由写入命令设置，完成后自动清零)
    buf[idx++] = 0;
    buf[idx++] = 0;

    return idx / 2;
}

static uint16_t dayue_build_exceed_signal_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;


    uint16_t exceedSignal = 0x0000;
    for (int i = 0; i < 8; i++)
    {
        float threshold = 10.0f;
        if (g_RetainSampleConfig.channelData[i] > threshold)
        {
            exceedSignal = 0x0001;
            break;
        }
    }

    buf[idx++] = (exceedSignal >> 8) & 0xFF;
    buf[idx++] = exceedSignal & 0xFF;

    return idx / 2;
}

static uint16_t dayue_build_flow_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;


    float flowRate = g_RetainSampleConfig.channelData[7];
    uint8_t flowBytes[4];
    convertFloatToBytes(flowRate, flowBytes);

    buf[idx++] = flowBytes[0];
    buf[idx++] = flowBytes[1];
    buf[idx++] = flowBytes[2];
    buf[idx++] = flowBytes[3];

    return idx / 2;
}

static uint16_t dayue_build_system_time_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;


    rtc_time_get();

    buf[idx++] = 0;
    buf[idx++] = (uint8_t)(calendar.year - 2000);
    buf[idx++] = 0;
    buf[idx++] = calendar.month;
    buf[idx++] = 0;
    buf[idx++] = calendar.date;
    buf[idx++] = 0;
    buf[idx++] = calendar.hour;
    buf[idx++] = 0;
    buf[idx++] = calendar.min;
    buf[idx++] = 0;
    buf[idx++] = calendar.sec;

    return idx / 2;
}

/* ===== 读保持寄存器实现 ===== */

/**
 * @brief 读保持寄存器内部实现
 * @param inst Modbus 实例指针
 * @param buf 数据缓冲区（函数填充大端序数据）
 * @param addr 起始地址（0-based）
 * @param nregs 寄存器数量
 * @return 异常码（0=成功，非0=异常）
 */
static uint8_t dayue_read_holding(MBInstance_t *inst, uint8_t *buf,
                                  uint16_t addr, uint16_t nregs)
{
    (void)inst;

    /* 检查地址范围是否有效 (0x0000~0x0258) */
    if (addr > 0x0258 || (uint32_t)addr + nregs > 0x0259) {
        return MB_EX_ILLEGAL_ADDRESS;
    }

    /* 初始化输出缓冲区为0 */
    memset(buf, 0, nregs * 2);

    /* 遍历所有block，填充有交集的部分 */
    dayue_fill_block_overlap(buf, addr, nregs, 0x0000, 0x0051, dayue_build_status_device_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0051, 118, dayue_build_retain_exceed_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x00C7, 22, dayue_build_retain_normal_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x00DD, 10, dayue_build_retain_instant_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x00E7, 14, dayue_build_discard_record_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x00F5, 53, dayue_build_reserved_block_1);
    dayue_fill_block_overlap(buf, addr, nregs, 0x012A, 6, dayue_build_water_make_time_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0130, 48, dayue_build_water_quality_factor_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0160, 32, dayue_build_reserved_block_2);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0180, 24, dayue_build_sampling_params_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0198, 21, dayue_build_retain_params_block);
    // 注意: 动态密码在0x01FB~0x01FC，由door_card_device_block处理，不在0x019B
    dayue_fill_block_overlap(buf, addr, nregs, 0x01B0, 49, dayue_build_factor_data_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x01E1, 6, dayue_build_system_time_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x01E7, 47, dayue_build_door_card_device_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0216, 6, dayue_build_bucket_control_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x021C, 1, dayue_build_delivery_ready_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x021D, 1, dayue_build_delivery_sync_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x021E, 1, dayue_build_exceed_signal_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x021F, 2, dayue_build_flow_block);
    dayue_fill_block_overlap(buf, addr, nregs, 0x0221, 56, dayue_build_reserved_auth_block);

    return 0;
}

/* ===== 写保持寄存器实现 ===== */

/**
 * @brief 写保持寄存器内部实现
 * @param inst Modbus 实例指针
 * @param buf 数据缓冲区（包含大端序待写入数据）
 * @param addr 起始地址（0-based）
 * @param nregs 寄存器数量
 * @return 异常码（0=成功，非0=异常）
 */
static uint8_t dayue_write_holding(MBInstance_t *inst, uint8_t *buf,
                                   uint16_t addr, uint16_t nregs)
{
    (void)inst;
    uint16_t regValue;

    printf("[大岳] 写寄存器: 地址=0x%04X, 数量=%d\r\n", addr, nregs);

    /* ===== 写单个寄存器（nregs == 1） ===== */
    if (nregs == 1)
    {
        regValue = (buf[0] << 8) | buf[1];

        if (addr == 0x0000)
        {
            if (regValue != 0)
            {
                printf("[Dayue] Special command received: 0x%04X\r\n", regValue);
            }
            return 0;
        }

        /* ===== 0x0216-0x0218 (40535-40537): 触发采样/排空/送样 ===== */
        if (addr >= 0x0216 && addr <= 0x0218)
        {
            uint8_t bucket = 0;

            if (addr == 0x0216)
            {
                bucket = 0; /* A桶 */
                printf("[大岳] A桶操作, 值=0x%04X\r\n", regValue);
            }
            else if (addr == 0x0217)
            {
                bucket = 1; /* B桶 */
                printf("[大岳] B桶操作, 值=0x%04X\r\n", regValue);
            }
            else if (addr == 0x0218)
            {
                bucket = 2; /* AB自动 */
                printf("[大岳] AB自动操作, 值=0x%04X\r\n", regValue);
            }

            /* 检查采样模式 */
            if (g_SampleConfig.SamplingMode != 2)
            {
                printf("[大岳] 警告: 当前采样模式不是通讯触发模式(%d)\r\n",
                       g_SampleConfig.SamplingMode);
            }
            else
            {
                switch (regValue)
                {
                case 0x0001: /* 采样 */
                    printf("[大岳] 触发采样, 桶=%d\r\n", bucket);
                    if (bucket == 0)
                    {
                        g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.a_cmd_timestamp = rtc_counter_get();
                    }
                    else if (bucket == 1)
                    {
                        g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.b_cmd_timestamp = rtc_counter_get();
                    }
                    else
                    {
                        g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.ab_cmd_timestamp = rtc_counter_get();
                    }
                    g_comm_trigger_request.request_type = COMM_REQ_SAMPLING;
                    g_comm_trigger_request.bucket_selector = bucket;
                    g_comm_trigger_request.volume = g_SampleConfig.SampleVolume;
                    g_comm_trigger_request.pending = 1;
                    printf("[大岳] 已设置采样请求\r\n");
                    break;

                case 0x0002: /* 排空 */
                    printf("[大岳] 触发排空, 桶=%d\r\n", bucket);
                    if (bucket == 0)
                    {
                        g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.a_cmd_timestamp = rtc_counter_get();
                    }
                    else if (bucket == 1)
                    {
                        g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.b_cmd_timestamp = rtc_counter_get();
                    }
                    else
                    {
                        g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.ab_cmd_timestamp = rtc_counter_get();
                    }
                    g_comm_trigger_request.request_type = COMM_REQ_DRAIN;
                    g_comm_trigger_request.bucket_selector = bucket;
                    g_comm_trigger_request.volume = 0;
                    g_comm_trigger_request.pending = 1;
                    printf("[大岳] 已设置排空请求\r\n");
                    break;

                case 0x0003: /* 送样 */
                    printf("[大岳] 触发送样, 桶=%d\r\n", bucket);
                    if (bucket == 0)
                    {
                        g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.a_cmd_timestamp = rtc_counter_get();
                    }
                    else if (bucket == 1)
                    {
                        g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.b_cmd_timestamp = rtc_counter_get();
                    }
                    else
                    {
                        g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
                        g_dayue_cmd_status.ab_cmd_timestamp = rtc_counter_get();
                    }
                    g_comm_trigger_request.request_type = COMM_REQ_DELIVERY;
                    g_comm_trigger_request.bucket_selector = bucket;
                    g_comm_trigger_request.volume = 0;
                    g_comm_trigger_request.pending = 1;
                    printf("[大岳] 已设置送样请求\r\n");
                    break;

                default:
                    printf("[大岳] 未知操作: 0x%04X\r\n", regValue);
                    break;
                }
            }
            return 0;
        }

        /* ===== 0x01AD (40430): 远程控制 ===== */
        else if (addr == 0x01AD)
        {
            printf("[大岳] 远程控制, 值=0x%04X\r\n", regValue);

            switch (regValue)
            {
            case 0x0001: /* 远程复位 */
                printf("[大岳] 远程复位\r\n");
                system_reset_start();
                printf("[大岳] 复位序列已启动\r\n");
                break;

            case 0x0002: /* 远程启动 */
                printf("[大岳] 远程启动\r\n");
                if (g_State.State == 0)
                {
                    system_start_sequence(START_MODE_MANUAL);
                    printf("[大岳] 系统启动序列已执行\r\n");
                }
                else
                {
                    printf("[大岳] 设备非停止状态(State=%d)，无法启动\r\n", g_State.State);
                }
                break;

            case 0x0003: /* 远程停止 */
                printf("[大岳] 远程停止\r\n");
                if (g_State.State == 1)
                {
                    g_State.State = 0;
                    printf("[大岳] 设备状态已设置为停止\r\n");
                }
                else
                {
                    printf("[大岳] 设备非运行状态(State=%d)，无需停止\r\n", g_State.State);
                }
                break;

            case 0x0004: /* 远程恢复 */
                printf("[大岳] 远程恢复\r\n");
                if (g_State.State == 3) /* 暂停状态 */
                {
                    g_State.State = 1;
                    printf("[大岳] 设备已从暂停状态恢复\r\n");
                }
                else
                {
                    printf("[大岳] 设备非暂停状态(State=%d)，无法恢复\r\n", g_State.State);
                }
                break;

            default:
                printf("[大岳] 未知控制命令: 0x%04X\r\n", regValue);
                break;
            }
            return 0;
        }

        /* ===== 0x01AE: 复位瓶信息 ===== */
        else if (addr == 0x01AE)
        {
            printf("[大岳] 复位瓶信息, 瓶号=%d\r\n", regValue);
            if (regValue >= 1 && regValue <= 24)
            {
                g_RetainBottleState.usedMask &= ~(1UL << (regValue - 1));
                printf("[大岳] 瓶%d已复位\r\n", regValue);
            }
            else if (regValue == 0xFF)
            {
                g_RetainBottleState.usedMask = 0;
                printf("[大岳] 所有瓶位已复位\r\n");
            }
            return 0;
        }

        /* ===== 0x021C (40541): 自主查找瓶号瞬时留样 ===== */
        else if (addr == 0x021C)
        {
            printf("[大岳] 自主查找瓶号瞬时留样, 瓶数=%d\r\n", regValue);

            if (regValue >= 1 && regValue <= 24)
            {
                uint8_t result = instant_retention_execute(0, (uint8_t)regValue, 1);
                printf("[大岳] 瞬时留样%s\r\n", result ? "成功" : "失败");
            }
            else
            {
                printf("[大岳] 瞬时留样失败: 瓶数无效(%d)\r\n", regValue);
            }
            return 0;
        }

        /* ===== 0x0180~0x0187: 采样参数单寄存器写入 ===== */
        else if (addr >= 0x0180 && addr <= 0x0187)
        {
            printf("[大岳] 采样参数单写: 地址=0x%04X, 值=%d\r\n", addr, regValue);

            switch (addr) {
                case 0x0180:  /* 采样模式 */
                    switch (regValue) {
                        case 2:  g_SampleConfig.SamplingMode = 1; break;
                        case 6:  g_SampleConfig.SamplingMode = 2; break;
                        case 5:  g_SampleConfig.SamplingMode = 3; break;
                        default: g_SampleConfig.SamplingMode = 1; break;
                    }
                    break;
                case 0x0181:  /* 单次采样量 */
                    if (regValue >= 10 && regValue <= 2000)
                        g_SampleConfig.SampleVolume = regValue;
                    break;
                case 0x0182:  /* 时间等比间隔 */
                    if (regValue >= 5 && regValue <= 9999)
                        g_SampleConfig.SampleInterval = regValue;
                    break;
                case 0x0183:  /* 排放等比排放量 */
                    if (regValue >= 1 && regValue <= 9999)
                        g_SampleConfig.DischargeVolume = regValue;
                    break;
                case 0x0184:  /* 流量比例间隔 */
                    if (regValue >= 5 && regValue <= 9999)
                        g_SampleConfig.SampleInterval = regValue;
                    break;
                case 0x0185:  /* 流量比例采样比例 */
                    if (regValue >= 10 && regValue <= 9999)
                        g_SampleConfig.FlowRatio = regValue;
                    break;
                case 0x0186:  /* 流量触发阈值 */
                    if (regValue >= 1 && regValue <= 9999)
                        g_SampleConfig.FlowStart = regValue;
                    break;
                case 0x0187:  /* 流量触发间隔 */
                    if (regValue >= 3 && regValue <= 9999)
                        g_SampleConfig.SampleInterval = regValue;
                    break;
            }

            extern uint8_t cfg_save_sample(const void *p);
            cfg_save_sample(&g_SampleConfig);
            printf("[大岳] 采样参数已保存\r\n");
            return 0;
        }

        /* ===== 0x0198~0x019C: 留样参数单寄存器写入 ===== */
        else if (addr >= 0x0198 && addr <= 0x019C)
        {
            printf("[大岳] 留样参数单写: 地址=0x%04X, 值=%d\r\n", addr, regValue);

            switch (addr) {
                case 0x0198:  /* 留样模式 (0~6) */
                    if (regValue <= 6)
                        g_RetainSampleConfig.Mode = (uint8_t)regValue;
                    break;
                case 0x0199:  /* 单次留样量 (10~1000mL) */
                    if (regValue >= 10 && regValue <= 1000)
                        g_RetainSampleConfig.SampleVolume = regValue;
                    break;
                case 0x019A:  /* 平行样数 (1~6瓶) */
                    if (regValue >= 1 && regValue <= 6)
                        g_RetainSampleConfig.ParallelCount = (uint8_t)regValue;
                    break;
                case 0x019B:  /* 单瓶混合次数 (1~99次) */
                    if (regValue >= 1 && regValue <= 99)
                        g_RetainSampleConfig.MixCount = (uint8_t)regValue;
                    break;
                case 0x019C:  /* 是否加药 (0=否, 1=是) */
                    if (regValue <= 1)
                        g_RetainSampleConfig.EnableAcid = (uint8_t)regValue;
                    break;
            }

            extern uint8_t cfg_save_retain(const void *p);
            cfg_save_retain(&g_RetainSampleConfig);
            printf("[大岳] 留样参数已保存\r\n");
            extern void write_retain_settings_page(void);
            write_retain_settings_page();
            return 0;
        }
    }

    /* ===== 写多个寄存器（nregs > 1） ===== */

    /* 0x01B0: 接收因子数据 */
    if (addr == 0x01B0)
    {
        printf("[大岳] 功能1: 接收因子数据\r\n");

        g_FactorCount = nregs / 3; /* 每个因子3个寄存器 */

        for (uint8_t i = 0; i < g_FactorCount && i < MAX_FACTOR_COUNT; i++)
        {
            uint16_t offset = i * 6; /* 每个因子6字节 */

            /* 解析因子类型（2字节，大端序） */
            g_FactorDataFromHost[i].factorType = (buf[offset] << 8) | buf[offset + 1];

            /* 解析因子值（4字节，大端序浮点数） */
            g_FactorDataFromHost[i].factorValue = convertToFloatH(
                    buf[offset + 2], /* 高字节 */
                    buf[offset + 3],
                    buf[offset + 4],
                    buf[offset + 5] /* 低字节 */
                                                  );

            printf("[大岳] 因子[%d]: 类型=0x%04X, 数值=%.2f\r\n",
                   i,
                   g_FactorDataFromHost[i].factorType,
                   g_FactorDataFromHost[i].factorValue);
        }
        return 0;
    }

    /* 0x0219: 触发留样（3个寄存器） */
    else if (addr == 0x0219 && nregs == 3)
    {
        uint16_t startBottle = (buf[0] << 8) | buf[1];
        uint16_t bottleCount = (buf[2] << 8) | buf[3];
        uint16_t type = (buf[4] << 8) | buf[5];

        printf("[大岳] 触发留样: 起始瓶=%d, 数量=%d, 类型=%d\r\n",
               startBottle, bottleCount, type);

        /* Type 1=A桶, 2=B桶, 3=AB自动 */
        if (type >= 1 && type <= 3)
        {
            CommRetainWindowContext *ctx = NULL;
            uint8_t bucket_id = 0;

            if (type == 1)
            {
                ctx = &g_comm_retain_ctx_a;
                bucket_id = 0;
                printf("[大岳] A桶触发留样\r\n");
            }
            else if (type == 2)
            {
                ctx = &g_comm_retain_ctx_b;
                bucket_id = 1;
                printf("[大岳] B桶触发留样\r\n");
            }
            else
            {
                /* type == 3: AB自动，根据送样完成状态选择（检查留样窗口） */
                uint8_t a_ready = (g_comm_retain_ctx_a.in_window || g_comm_retain_ctx_a.delivery_time > 0) && !g_comm_retain_ctx_a.retain_executed;
                uint8_t b_ready = (g_comm_retain_ctx_b.in_window || g_comm_retain_ctx_b.delivery_time > 0) && !g_comm_retain_ctx_b.retain_executed;

                if (a_ready && !b_ready)
                {
                    ctx = &g_comm_retain_ctx_a;
                    bucket_id = 0;
                    printf("[大岳] AB自动选择A桶(已送样完成)\r\n");
                }
                else if (b_ready && !a_ready)
                {
                    ctx = &g_comm_retain_ctx_b;
                    bucket_id = 1;
                    printf("[大岳] AB自动选择B桶(已送样完成)\r\n");
                }
                else if (a_ready)
                {
                    ctx = &g_comm_retain_ctx_a;
                    bucket_id = 0;
                    printf("[大岳] AB自动优先A桶(已送样完成)\r\n");
                }
                else
                {
                    printf("[大岳] AB桶都未送样完成，无法留样\r\n");
                    return MB_EX_SLAVE_DEVICE_FAIL;
                }
            }

            /* 设置通讯触发标志 */
            ctx->comm_trigger_received = 1;
            ctx->bucket_id = bucket_id;

            /* 保存瓶数量到全局变量 */
            g_comm_retain_bottle_count = (uint8_t)bottleCount;

            /* 记录留样日志 */
            RetainLogRecord log;
            memset(&log, 0, sizeof(RetainLogRecord));
            log.retain_mode = 2; /* 通讯触发 */
            log.bottle_number = (uint16_t)startBottle;
            log.retain_volume = 0; /* 通讯留样体积由配置决定 */
            log.result = 1;        /* 预设成功 */

            /* 获取当前时间戳 */
            log.start_time = rtc_counter_get();

            log_retain_record(&log);
            printf("[大岳] 留样触发已记录\r\n");

            /* 更新大岳命令状态 */
            g_dayue_cmd_status.retention_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.retention_cmd_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

            return 0;
        }

        /* Type 4: 瞬时留样 */
        else if (type == 4)
        {
            printf("[大岳] 瞬时留样: 起始瓶=%d, 数量=%d\r\n", startBottle, bottleCount);

            uint8_t result = instant_retention_execute((uint8_t)startBottle, (uint8_t)bottleCount, 1);

            if (result)
            {
                printf("[大岳] 瞬时留样成功\r\n");

                /* 更新大岳命令状态 */
                g_dayue_cmd_status.retention_status = CMD_STATUS_COMPLETED;
                return 0;
            }
            else
            {
                printf("[大岳] 瞬时留样失败\r\n");
                g_dayue_cmd_status.retention_status = CMD_STATUS_FAILED;
                return MB_EX_SLAVE_DEVICE_FAIL;
            }
        }

        /* Type 5: 瓶排空 */
        else if (type == 5)
        {
            printf("[大岳] 瓶排空: 起始瓶=%d, 数量=%d\r\n", startBottle, bottleCount);

            uint8_t all_success = 1;
            for (uint16_t i = 0; i < bottleCount; i++)
            {
                uint8_t target_bottle = startBottle + i;

                /* 检查瓶号范围 */
                if (target_bottle < 1 || target_bottle > BOTTLE_COUNT)
                {
                    printf("[大岳] 瓶号%d超出范围\r\n", target_bottle);
                    all_success = 0;
                    continue;
                }

                uint8_t result = emptybottle(target_bottle, 100, 60000);
                if (!result)
                {
                    printf("[大岳] 瓶%d排空失败\r\n", target_bottle);
                    all_success = 0;
                }
                else
                {
                    printf("[大岳] 瓶%d排空成功\r\n", target_bottle);
                }
            }

            if (all_success)
            {
                printf("[大岳] 所有瓶排空成功\r\n");
                return 0;
            }
            else
            {
                printf("[大岳] 部分瓶排空失败\r\n");
                return MB_EX_SLAVE_DEVICE_FAIL;
            }
        }

        else
        {
            printf("[大岳] 未知的留样类型: %d\r\n", type);
            return MB_EX_ILLEGAL_VALUE;
        }
    }

    /* 0x0130~0x015F: 水质因子超标设置（48个寄存器，支持单个因子或批量写入） */
    else if (addr >= 0x0130 && addr <= 0x015F)
    {
        printf("[大岳] 水质因子超标设置: 地址=0x%04X, 数量=%d\r\n", addr, nregs);

        /* 计算起始因子索引和寄存器偏移 */
        uint16_t offset = addr - 0x0130;
        uint16_t end_offset = offset + nregs;

        /* 检查地址范围 */
        if (end_offset > 48) {
            printf("[大岳] 地址范围超出: end=0x%04X\r\n", addr + nregs - 1);
            return MB_EX_ILLEGAL_ADDRESS;
        }

        /* 逐个寄存器处理 */
        for (uint16_t i = 0; i < nregs; i++) {
            uint16_t reg_offset = offset + i;
            uint16_t factor_idx = reg_offset / 8;  /* 每个因子8个寄存器 */
            uint16_t field_idx = reg_offset % 8;   /* 因子内字段索引 */
            uint16_t buf_offset = i * 2;

            if (factor_idx >= 6) continue;  /* 最多6个因子 */

            switch (field_idx) {
                case 0:  /* 因子编号 */
                    g_RetainSampleConfig.channelLimits[factor_idx].FactorType = buf[buf_offset + 1];
                    break;
                case 1:  /* 超标下限高16位 */
                case 2:  /* 超标下限低16位 */
                    if (field_idx == 1 && (i + 1) < nregs) {
                        /* 完整的浮点数写入 */
                        float val = convertToFloatH(buf[buf_offset], buf[buf_offset + 1],
                                                    buf[buf_offset + 2], buf[buf_offset + 3]);
                        g_RetainSampleConfig.channelLimits[factor_idx].LowerLimit = val;
                        i++;  /* 跳过下一个寄存器 */
                    }
                    break;
                case 3:  /* 超标上限高16位 */
                case 4:  /* 超标上限低16位 */
                    if (field_idx == 3 && (i + 1) < nregs) {
                        /* 完整的浮点数写入 */
                        float val = convertToFloatH(buf[buf_offset], buf[buf_offset + 1],
                                                    buf[buf_offset + 2], buf[buf_offset + 3]);
                        g_RetainSampleConfig.channelLimits[factor_idx].UpperLimit = val;
                        i++;  /* 跳过下一个寄存器 */
                    }
                    break;
                case 5:  /* 超标平行样数 */
                    g_RetainSampleConfig.channelLimits[factor_idx].ParallelCount = buf[buf_offset + 1];
                    break;
                case 6:  /* 超标留样间隔 */
                    g_RetainSampleConfig.channelLimits[factor_idx].RetainInterval =
                        (buf[buf_offset] << 8) | buf[buf_offset + 1];
                    break;
                case 7:  /* 启用/停用 */
                    g_RetainSampleConfig.channelLimits[factor_idx].Enable = buf[buf_offset + 1];
                    break;
            }
        }

        /* 保存到KVDB */
        extern uint8_t cfg_save_retain(const void *p);
        cfg_save_retain(&g_RetainSampleConfig);
        printf("[大岳] 水质因子超标设置已保存\r\n");
        return 0;
    }

    /* 0x0180~0x0187: 采样参数设置（8个寄存器） */
    else if (addr >= 0x0180 && addr <= 0x0187)
    {
        printf("[大岳] 采样参数设置: 地址=0x%04X, 数量=%d\r\n", addr, nregs);

        /* 检查地址范围 */
        uint16_t end_addr = addr + nregs - 1;
        if (end_addr > 0x0187) {
            printf("[大岳] 地址范围超出: end=0x%04X\r\n", end_addr);
            return MB_EX_ILLEGAL_ADDRESS;
        }

        /* 逐个寄存器处理 */
        for (uint16_t i = 0; i < nregs; i++) {
            uint16_t reg_addr = addr + i;
            uint16_t buf_offset = i * 2;
            uint16_t value = (buf[buf_offset] << 8) | buf[buf_offset + 1];

            switch (reg_addr) {
                case 0x0180:  /* 采样模式 */
                    /* 大岳协议: 0-远程 1-定时 2-时间等比 3-流量等比 4-流量跟随 5-开关触发 6-串口控制 7-其他 */
                    /* 内部定义: 1-时间等比 2-通讯触发 3-开关量触发 */
                    switch (value) {
                        case 2:  g_SampleConfig.SamplingMode = 1; break;  /* 时间等比 */
                        case 6:  g_SampleConfig.SamplingMode = 2; break;  /* 串口控制->通讯触发 */
                        case 5:  g_SampleConfig.SamplingMode = 3; break;  /* 开关触发 */
                        default: g_SampleConfig.SamplingMode = 1; break;  /* 默认时间等比 */
                    }
                    printf("[大岳] 采样模式: %d -> %d\r\n", value, g_SampleConfig.SamplingMode);
                    break;
                case 0x0181:  /* 单次采样量 (10~2000ml) */
                    if (value >= 10 && value <= 2000) {
                        g_SampleConfig.SampleVolume = value;
                        printf("[大岳] 单次采样量: %d ml\r\n", value);
                    }
                    break;
                case 0x0182:  /* 时间等比间隔 (5~9999分钟) */
                    if (value >= 5 && value <= 9999) {
                        g_SampleConfig.SampleInterval = value;
                        printf("[大岳] 时间等比间隔: %d 分钟\r\n", value);
                    }
                    break;
                case 0x0183:  /* 排放等比排放量 (1~9999 M³) */
                    if (value >= 1 && value <= 9999) {
                        g_SampleConfig.DischargeVolume = value;
                        printf("[大岳] 排放等比排放量: %d M³\r\n", value);
                    }
                    break;
                case 0x0184:  /* 流量比例间隔 (5~9999分钟) */
                    if (value >= 5 && value <= 9999) {
                        g_SampleConfig.SampleInterval = value;
                        printf("[大岳] 流量比例间隔: %d 分钟\r\n", value);
                    }
                    break;
                case 0x0185:  /* 流量比例采样比例 (10~9999 ml/M³) */
                    if (value >= 10 && value <= 9999) {
                        g_SampleConfig.FlowRatio = value;
                        printf("[大岳] 流量比例采样比例: %d ml/M³\r\n", value);
                    }
                    break;
                case 0x0186:  /* 流量触发阈值 (1~9999 M³/h) */
                    if (value >= 1 && value <= 9999) {
                        g_SampleConfig.FlowStart = value;
                        printf("[大岳] 流量触发阈值: %d M³/h\r\n", value);
                    }
                    break;
                case 0x0187:  /* 流量触发间隔 (3~9999分钟) */
                    if (value >= 3 && value <= 9999) {
                        g_SampleConfig.SampleInterval = value;
                        printf("[大岳] 流量触发间隔: %d 分钟\r\n", value);
                    }
                    break;
            }
        }

        /* 保存到KVDB */
        extern uint8_t cfg_save_sample(const void *p);
        cfg_save_sample(&g_SampleConfig);
        printf("[大岳] 采样参数已保存\r\n");
        return 0;
    }

    /* 0x0198~0x019C: 留样参数设置（5个寄存器） */
    else if (addr >= 0x0198 && addr <= 0x019C)
    {
        printf("[大岳] 留样参数设置: 地址=0x%04X, 数量=%d\r\n", addr, nregs);

        /* 检查地址范围 */
        uint16_t end_addr = addr + nregs - 1;
        if (end_addr > 0x019C) {
            printf("[大岳] 地址范围超出: end=0x%04X\r\n", end_addr);
            return MB_EX_ILLEGAL_ADDRESS;
        }

        /* 逐个寄存器处理 */
        for (uint16_t i = 0; i < nregs; i++) {
            uint16_t reg_addr = addr + i;
            uint16_t buf_offset = i * 2;
            uint16_t value = (buf[buf_offset] << 8) | buf[buf_offset + 1];

            switch (reg_addr) {
                case 0x0198:  /* 留样模式 (0~6) */
                    if (value <= 6) {
                        g_RetainSampleConfig.Mode = (uint8_t)value;
                        printf("[大岳] 留样模式: %d\r\n", value);
                    }
                    break;
                case 0x0199:  /* 单次留样量 (10~1000mL) */
                    if (value >= 10 && value <= 1000) {
                        g_RetainSampleConfig.SampleVolume = value;
                        printf("[大岳] 单次留样量: %d mL\r\n", value);
                    }
                    break;
                case 0x019A:  /* 平行样数 (1~6瓶) */
                    if (value >= 1 && value <= 6) {
                        g_RetainSampleConfig.ParallelCount = (uint8_t)value;
                        printf("[大岳] 平行样数: %d 瓶\r\n", value);
                    }
                    break;
                case 0x019B:  /* 单瓶混合次数 (1~99次) */
                    if (value >= 1 && value <= 99) {
                        g_RetainSampleConfig.MixCount = (uint8_t)value;
                        printf("[大岳] 单瓶混合次数: %d 次\r\n", value);
                    }
                    break;
                case 0x019C:  /* 是否加药 (0=否, 1=是) */
                    if (value <= 1) {
                        g_RetainSampleConfig.EnableAcid = (uint8_t)value;
                        printf("[大岳] 是否加药: %s\r\n", value ? "是" : "否");
                    }
                    break;
            }
        }

        /* 保存到KVDB */
        extern uint8_t cfg_save_retain(const void *p);
        cfg_save_retain(&g_RetainSampleConfig);
        printf("[大岳] 留样参数已保存\r\n");
        extern void write_retain_settings_page(void);
        write_retain_settings_page();
        return 0;
    }

    /* 0x01DA: 系统时间设置（7个寄存器） */
    else if (addr == 0x01DA && nregs == 7)
    {
        uint16_t year = (buf[0] << 8) | buf[1];
        uint16_t month = (buf[2] << 8) | buf[3];
        uint16_t day = (buf[4] << 8) | buf[5];
        uint16_t hour = (buf[6] << 8) | buf[7];
        uint16_t minute = (buf[8] << 8) | buf[9];
        uint16_t second = (buf[10] << 8) | buf[11];
        uint16_t cmd = (buf[12] << 8) | buf[13];

        printf("[大岳] 设置时间: %04d-%02d-%02d %02d:%02d:%02d, cmd=%d\r\n",
               2000 + year, month, day, hour, minute, second, cmd);

        /* 只有cmd=1时才更新时间 */
        if (cmd == 1) {
            /* 构造时间结构 */
            calendar_type time_struct;
            time_struct.year = 2000 + year;
            time_struct.month = (uint8_t)month;
            time_struct.date = (uint8_t)day;
            time_struct.week = 0;  /* 星期由RTC自动计算 */
            time_struct.hour = (uint8_t)hour;
            time_struct.min = (uint8_t)minute;
            time_struct.sec = (uint8_t)second;

            /* 设置RTC时间 */
            rtc_time_set(&time_struct);
            printf("[大岳] 系统时间设置成功\r\n");
        } else {
            printf("[大岳] 时间已接收但未更新(cmd!=1)\r\n");
        }
        return 0;
    }

    /* 未知地址 */
    printf("[大岳] 未实现的寄存器地址: 0x%04X (nregs=%d)\r\n", addr, nregs);
    return MB_EX_ILLEGAL_ADDRESS;
}

/* ============ 大岳协议实例初始化 ============ */

/**
 * @brief 注册大岳协议回调函数
 * @param inst Modbus实例指针
 *
 * @note 大岳协议使用MB_MODE_NORMAL模式，检查站号匹配
 */
void dayue_register_callbacks(MBInstance_t *inst)
{
    /* 注册输入寄存器回调(0x04) - 大岳协议不使用 */
    eMBRegisterInputCB_Inst(inst, dayue_input_cb);

    /* 注册保持寄存器回调(0x03/0x06/0x10) */
    eMBRegisterHoldingCB_Inst(inst, dayue_holding_cb);
}


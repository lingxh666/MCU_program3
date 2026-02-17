/**
 * @file mb_reg_sichuan.c
 * @brief 四川管控协议 Modbus 寄存器回调实现
 * @note  支持功能码 0x03/0x06/0x10
 */

#include "mb.h"
#include "work.h"
#include "freertos_app.h"
#include "Commtrigger.h"
#include "retain_judge.h"
#include "Flowtrigger.h"
#include "rtc.h"
#include "sampling.h"
#include "bsp_button.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define SICHUAN_REG_BASE    40001
#define SICHUAN_REG_MAX     40309
#define SICHUAN_REG_COUNT   (SICHUAN_REG_MAX - SICHUAN_REG_BASE + 1)
#define SICHUAN_PULSE_SECONDS 60U

extern State g_State;
extern SampleConfig g_SampleConfig;
extern RetainSampleModeConfig g_RetainSampleConfig;
extern CommSettingConfig g_CommSettingConfig;
extern SystemSettingConfig g_SystemSettingConfig;
extern RetainBottleState g_RetainBottleState;
extern RetainSampleInfo_t g_LastRetainInfo;
extern RetainSampleInfo_t g_LastInstantRetainInfo;
extern DoorAccessRecord_t g_DoorAccessRecords[DOOR_ACCESS_RECORD_MAX];
extern uint8_t g_DoorAccessRecordCount;
extern CommTriggerRequest g_comm_trigger_request;
extern uint32_t g_last_delivery_time;
extern calendar_type calendar;
extern volatile uint8_t g_manual_operation_abort_flag;
extern volatile uint8_t g_retention_abort_flag;
extern uint32_t rtc_counter_get(void);
extern WaterSampleContext g_water_ctx_A;
extern WaterSampleContext g_water_ctx_B;
extern SichuanSampleId_t g_SichuanSampleId;
extern SichuanExceedRetainCtx_t g_SichuanExceedRetainCtx;

static uint32_t s_sichuan_delivery_signal_until = 0;
static uint32_t s_sichuan_last_delivery_ts = 0;
static uint32_t s_sichuan_retain_signal_until = 0;
static uint32_t s_sichuan_last_retain_sig = 0;
static uint16_t s_sichuan_retain_volume = 0;

static uint16_t sichuan_map_bucket_state(uint8_t internal_state)
{
    if (internal_state == 45 || internal_state == 42) {
        return 4;  // 排空
    }
    if (internal_state >= 30 && internal_state <= 48) {
        return 3;  // 留样
    }
    if (internal_state == 22) {
        return 2;  // 等待分析
    }
    if (internal_state >= 4 && internal_state <= 19) {
        return 1;  // 采水/供样过程
    }
    return 0;      // 空闲
}

static uint16_t sichuan_map_sampling_mode(void)
{
    switch (g_SampleConfig.SamplingMode) {
    case 3:  return 2;  // 流量触发
    case 2:  return 3;  // 通讯触发 -> 串口控制
    default: return 1;  // 时间等比/定时
    }
}

static int sichuan_time_compare(const RetainSampleInfo_t *a, const RetainSampleInfo_t *b)
{
    if (a->year != b->year) return (a->year > b->year) ? 1 : -1;
    if (a->month != b->month) return (a->month > b->month) ? 1 : -1;
    if (a->day != b->day) return (a->day > b->day) ? 1 : -1;
    if (a->hour != b->hour) return (a->hour > b->hour) ? 1 : -1;
    if (a->minute != b->minute) return (a->minute > b->minute) ? 1 : -1;
    if (a->second != b->second) return (a->second > b->second) ? 1 : -1;
    return 0;
}

static const RetainSampleInfo_t *sichuan_pick_latest_retain(void)
{
    const RetainSampleInfo_t *a = &g_LastRetainInfo;
    const RetainSampleInfo_t *b = &g_LastInstantRetainInfo;

    if (a->year == 0 && b->year == 0) {
        return a;
    }
    if (a->year == 0) {
        return b;
    }
    if (b->year == 0) {
        return a;
    }
    return (sichuan_time_compare(a, b) >= 0) ? a : b;
}

static uint32_t sichuan_make_retain_sig(const RetainSampleInfo_t *info)
{
    return ((uint32_t)info->year << 20) ^
           ((uint32_t)info->month << 16) ^
           ((uint32_t)info->day << 11) ^
           ((uint32_t)info->hour << 6) ^
           (uint32_t)info->minute ^
           ((uint32_t)info->second << 26);
}

static uint16_t sichuan_map_retain_mode(uint16_t internal_mode)
{
    switch (internal_mode) {
    case RETAIN_MODE_MODBUS: return 0;  // 远程留样
    case RETAIN_MODE_SYNC:   return 1;  // 同步留样
    case RETAIN_MODE_ALARM:  return 2;  // 超标留样
    case RETAIN_MODE_DIRECT: return 3;  // 直接留样
    default:                 return 0;
    }
}

static uint16_t sichuan_map_acid_type(const RetainSampleInfo_t *info)
{
    if (info->addAcid == 0) {
        return 0;
    }
    switch (info->acidType) {
    case 1:  return 1;  // 硝酸
    case 2:  return 2;  // 盐酸
    case 3:  return 3;  // 氢氧化钠
    case 0:  return 4;  // 硫酸
    default: return 0;
    }
}

static uint32_t sichuan_build_bottle_mask(void)
{
    uint32_t mask = 0;
    for (int i = 0; i < 24; i++) {
        if (g_RetainBottleState.usedMask & (1u << i)) {
            mask |= (1u << (31 - i));
        }
    }
    return mask;
}

static void sichuan_request_comm(CommTriggerRequestType type)
{
    g_comm_trigger_request.request_type = type;
    g_comm_trigger_request.bucket_selector = 2;  // AB自动
    g_comm_trigger_request.volume = (type == COMM_REQ_SAMPLING) ? g_SampleConfig.SampleVolume : 0;
    g_comm_trigger_request.pending = 1;
}

// 瞬时留样编号生成
static void sichuan_generate_instant_sample_id(SichuanSampleId_t *out)
{
    rtc_time_get();
    out->year = calendar.year;
    out->month = calendar.month;
    out->day = calendar.date;
    out->start_hour = calendar.hour;
    out->start_min = calendar.min;
    out->end_hour = calendar.hour;   // 瞬时：开始=结束
    out->end_min = calendar.min;
}

// 从桶上下文生成留样编号
static void sichuan_generate_sample_id_from_ctx(
    const WaterSampleContext *ctx,
    SichuanSampleId_t *out)
{
    if (!ctx || !out || !ctx->valid) {
        // 无效上下文，使用当前时间
        sichuan_generate_instant_sample_id(out);
        return;
    }

    // 从sample_id解析开始时间: "YYYYMMDDHHmmss-SSS"
    if (strlen(ctx->sample_id) >= 12) {
        char buf[5] = {0};

        // 年: 位置0-3
        strncpy(buf, ctx->sample_id, 4);
        out->year = (uint16_t)atoi(buf);

        // 月: 位置4-5
        strncpy(buf, ctx->sample_id + 4, 2);
        buf[2] = '\0';
        out->month = (uint16_t)atoi(buf);

        // 日: 位置6-7
        strncpy(buf, ctx->sample_id + 6, 2);
        out->day = (uint16_t)atoi(buf);

        // 开始时: 位置8-9
        strncpy(buf, ctx->sample_id + 8, 2);
        out->start_hour = (uint16_t)atoi(buf);

        // 开始分: 位置10-11
        strncpy(buf, ctx->sample_id + 10, 2);
        out->start_min = (uint16_t)atoi(buf);
    }

    // 计算结束时间: 开始时间 + CycleTime
    uint16_t cycle_min = g_SampleConfig.CycleTime;
    uint16_t total_min = out->start_hour * 60 + out->start_min + cycle_min;
    out->end_hour = (total_min / 60) % 24;
    out->end_min = total_min % 60;
}

static uint8_t sichuan_exec_instant_retention(uint16_t volume)
{
    uint16_t old_volume = g_RetainSampleConfig.SampleVolume;
    if (volume > 0) {
        g_RetainSampleConfig.SampleVolume = volume;
    }
    uint8_t ok = instant_retention_execute(0, 1, 1);
    g_RetainSampleConfig.SampleVolume = old_volume;
    return ok;
}

// 超标留样函数（从桶中留样，继承sample_id）
static uint8_t sichuan_exec_exceed_retention(uint16_t volume)
{
    // 1. 查找有效的桶上下文（优先选择有水的桶）
    WaterSampleContext *ctx = NULL;
    uint8_t bucket_id = 0xFF;

    // 优先选择A桶
    if (g_water_ctx_A.valid && g_State.SaveWarterA > 0) {
        ctx = &g_water_ctx_A;
        bucket_id = 0;
    }
    // 其次选择B桶
    else if (g_water_ctx_B.valid && g_State.SaveWarterB > 0) {
        ctx = &g_water_ctx_B;
        bucket_id = 1;
    }

    // 2. 无有效桶，回退到瞬时留样
    if (ctx == NULL || bucket_id == 0xFF) {
        printf("[四川超标留样] 无有效桶，回退到瞬时留样\r\n");
        sichuan_generate_instant_sample_id(&g_SichuanSampleId);
        return sichuan_exec_instant_retention(volume);
    }

    // 3. 从桶上下文生成留样编号
    sichuan_generate_sample_id_from_ctx(ctx, &g_SichuanSampleId);

    printf("[四川超标留样] 桶%c, sample_id=%s\r\n",
           bucket_id ? 'B' : 'A', ctx->sample_id);

    // 4. 设置留样体积
    uint16_t old_volume = g_RetainSampleConfig.SampleVolume;
    if (volume > 0) {
        g_RetainSampleConfig.SampleVolume = volume;
    }

    // 5. 调用调度器的留样执行函数（从桶中留样）
    uint32_t delivery_time = ctx->delivery_complete_time;
    uint8_t ok = retention_execute(bucket_id, delivery_time);

    // 6. 恢复配置
    g_RetainSampleConfig.SampleVolume = old_volume;

    return ok;
}

static uint8_t sichuan_read_holding(MBInstance_t *inst, uint8_t *buf,
                                    uint16_t addr, uint16_t nregs)
{
    (void)inst;
    if ((addr + nregs) > SICHUAN_REG_COUNT) {
        return MB_EX_ILLEGAL_ADDRESS;
    }

    uint32_t now = rtc_counter_get();
    const RetainSampleInfo_t *retain_info = sichuan_pick_latest_retain();
    uint32_t retain_sig = sichuan_make_retain_sig(retain_info);

    if (retain_info->year != 0 && retain_sig != s_sichuan_last_retain_sig) {
        s_sichuan_last_retain_sig = retain_sig;
        s_sichuan_retain_signal_until = now + SICHUAN_PULSE_SECONDS;
    }

    if (g_last_delivery_time > 0 && g_last_delivery_time != s_sichuan_last_delivery_ts) {
        s_sichuan_last_delivery_ts = g_last_delivery_time;
        s_sichuan_delivery_signal_until = now + SICHUAN_PULSE_SECONDS;
    }

    uint16_t sync_supply_signal = (s_sichuan_delivery_signal_until != 0 && now <= s_sichuan_delivery_signal_until) ? 1 : 0;
    uint16_t retain_done_signal = (s_sichuan_retain_signal_until != 0 && now <= s_sichuan_retain_signal_until) ? 1 : 0;

    uint32_t bottle_mask = sichuan_build_bottle_mask();

    uint32_t fixed_pwd = g_SystemSettingConfig.CardId[0];
    if (fixed_pwd > 999999) {
        fixed_pwd = 0;
    }

    /* ★ 修复：使用门锁开关事件时间（g_door_last_event_time）而非刷卡记录 */
    extern DoorLastEventTime_t g_door_last_event_time;
    uint16_t door_time[6] = {0};
    uint32_t door_card = 0;
    if (g_door_last_event_time.year != 0) {
        // 有门禁开关事件记录
        door_time[0] = g_door_last_event_time.year;
        door_time[1] = g_door_last_event_time.month;
        door_time[2] = g_door_last_event_time.day;
        door_time[3] = g_door_last_event_time.hour;
        door_time[4] = g_door_last_event_time.minute;
        door_time[5] = g_door_last_event_time.second;
    }
    // 卡号仍从刷卡记录获取
    DoorAccessRecord_t *last_record = (g_DoorAccessRecordCount > 0)
        ? &g_DoorAccessRecords[g_DoorAccessRecordCount - 1]
        : NULL;
    if (last_record) {
        door_card = last_record->cardId;
    } else {
        door_card = g_SystemSettingConfig.CardId[0];
    }

    rtc_time_get();
    // 动态密码算法: 999999 - 日时分(DDHHMI)
    uint32_t dynamic_pwd = 999999 - (calendar.date * 10000 + calendar.hour * 100 + calendar.min);

    uint16_t current_bucket = 0;
    if (g_State.InletThreeWayValve == 1) {
        current_bucket = 1;
    } else if (g_State.InletThreeWayValve == 2) {
        current_bucket = 2;
    } else if (g_State.CurrentBucket <= 1) {
        current_bucket = g_State.CurrentBucket ? 2 : 1;
    }

    uint16_t run_state = 2;
    switch (g_State.State) {
    case 0:  run_state = 0;  break;  // 停止
    case 3:  run_state = 1;  break;  // 空闲待机
    case 4:  run_state = 99; break;  // 维护
    case 5:  run_state = 98; break;  // 故障
    default: run_state = 2;  break;  // 运行中
    }

    for (uint16_t i = 0; i < nregs; i++) {
        uint16_t reg = SICHUAN_REG_BASE + addr + i;
        uint16_t value = 0;

        switch (reg) {
        case 40001: value = g_RetainBottleState.currentBottle; break;
        case 40002: value = current_bucket; break;
        case 40003: value = run_state; break;
        case 40004: value = sync_supply_signal; break;
        case 40005: value = retain_done_signal; break;
        case 40006: value = 40; break; // 冰柜温度*10
        case 40007: value = sichuan_map_bucket_state(g_State.ABucketState); break;
        case 40008: value = sichuan_map_bucket_state(g_State.BBucketState); break;
        case 40009: value = (uint16_t)(bottle_mask >> 16); break;
        case 40010: value = (uint16_t)(bottle_mask & 0xFFFF); break;
        case 40011: value = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) ? 0 : 1; break;  // PE12: HIGH=开(0), LOW=关(1)
        case 40012: value = (uint16_t)(fixed_pwd >> 16); break;
        case 40013: value = (uint16_t)(fixed_pwd & 0xFFFF); break;
        case 40014: value = sichuan_map_sampling_mode(); break;
        case 40015: value = door_time[0]; break;
        case 40016: value = door_time[1]; break;
        case 40017: value = door_time[2]; break;
        case 40018: value = door_time[3]; break;
        case 40019: value = door_time[4]; break;
        case 40020: value = door_time[5]; break;
        case 40021: value = (uint16_t)(door_card >> 16); break;
        case 40022: value = (uint16_t)(door_card & 0xFFFF); break;
        case 40101: value = retain_info->year; break;
        case 40102: value = retain_info->month; break;
        case 40103: value = retain_info->day; break;
        case 40104: value = retain_info->hour; break;
        case 40105: value = retain_info->minute; break;
        case 40106: value = retain_info->startBottle; break;
        case 40107: value = retain_info->volume; break;
        case 40108: value = sichuan_map_retain_mode(retain_info->mode); break;
        case 40109: value = g_SichuanSampleId.year; break;
        case 40110: value = g_SichuanSampleId.month; break;
        case 40111: value = g_SichuanSampleId.day; break;
        case 40112: value = g_SichuanSampleId.start_hour; break;
        case 40113: value = g_SichuanSampleId.start_min; break;
        case 40114: value = g_SichuanSampleId.end_hour; break;
        case 40115: value = g_SichuanSampleId.end_min; break;
        case 40116: value = (retain_info->result == 1) ? 0 : 1; break;
        case 40117: value = sichuan_map_acid_type(retain_info); break;
        case 40118: value = retain_info->acidRatio; break;
        case 40119: value = (uint16_t)(dynamic_pwd >> 16); break;
        case 40120: value = (uint16_t)(dynamic_pwd & 0xFFFF); break;
        default: value = 0; break;
        }

        buf[i * 2] = (uint8_t)(value >> 8);
        buf[i * 2 + 1] = (uint8_t)(value & 0xFF);
    }

    return 0;
}

static uint8_t sichuan_write_holding(MBInstance_t *inst, uint8_t *buf,
                                     uint16_t addr, uint16_t nregs)
{
    (void)inst;
    if ((addr + nregs) > SICHUAN_REG_COUNT) {
        return MB_EX_ILLEGAL_ADDRESS;
    }

    uint16_t start_reg = SICHUAN_REG_BASE + addr;
    uint16_t retain_volume = 0xFFFF;
    uint8_t do_retain = 0;
    uint8_t do_sample = 0;
    uint8_t do_delivery = 0;
    uint8_t do_drain = 0;
    uint8_t do_exceed_retain = 0;
    uint8_t do_not_exceed_drain = 0;
    uint8_t do_instant_delivery = 0;
    uint8_t do_stop = 0;

    for (uint16_t i = 0; i < nregs; i++) {
        uint16_t reg = start_reg + i;
        uint16_t val = ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1];

        switch (reg) {
        case 40301:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_retain = (uint8_t)val;
            break;
        case 40302:
            retain_volume = val;
            break;
        case 40303:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_sample = (uint8_t)val;
            break;
        case 40304:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_delivery = (uint8_t)val;
            break;
        case 40305:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_drain = (uint8_t)val;
            break;
        case 40306:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_exceed_retain = (uint8_t)val;
            break;
        case 40307:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_not_exceed_drain = (uint8_t)val;
            break;
        case 40308:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_instant_delivery = (uint8_t)val;
            break;
        case 40309:
            if (val != 0 && val != 1) return MB_EX_ILLEGAL_VALUE;
            do_stop = (uint8_t)val;
            break;
        default:
            return MB_EX_ILLEGAL_ADDRESS;
        }
    }

    if (retain_volume != 0xFFFF && retain_volume > 0) {
        s_sichuan_retain_volume = retain_volume;
    }

    if (do_stop) {
        g_manual_operation_abort_flag = 1;
        g_retention_abort_flag = 1;
        sichuan_request_comm(COMM_REQ_DRAIN);
    }

    if (do_sample) {
        sichuan_request_comm(COMM_REQ_SAMPLING);
    }

    if (do_delivery) {
        sichuan_request_comm(COMM_REQ_DELIVERY);
    }

    if (do_drain || do_not_exceed_drain) {
        sichuan_request_comm(COMM_REQ_DRAIN);
    }

    if (do_instant_delivery) {
        if (!instant_delivery_start()) {
            return MB_EX_SLAVE_DEVICE_FAIL;
        }
    }

    // 40301: 瞬时留样 - 调用 sichuan_exec_instant_retention()
    if (do_retain) {
        uint16_t volume = (retain_volume != 0xFFFF && retain_volume > 0)
                          ? retain_volume
                          : (s_sichuan_retain_volume > 0 ? s_sichuan_retain_volume
                                                         : g_RetainSampleConfig.SampleVolume);
        sichuan_generate_instant_sample_id(&g_SichuanSampleId);
        if (!sichuan_exec_instant_retention(volume)) {
            return MB_EX_SLAVE_DEVICE_FAIL;
        }
    }

    // 40306: 超标留样 - 调用 sichuan_exec_exceed_retention()
    if (do_exceed_retain) {
        uint16_t volume = (retain_volume != 0xFFFF && retain_volume > 0)
                          ? retain_volume
                          : (s_sichuan_retain_volume > 0 ? s_sichuan_retain_volume
                                                         : g_RetainSampleConfig.SampleVolume);
        if (!sichuan_exec_exceed_retention(volume)) {
            return MB_EX_SLAVE_DEVICE_FAIL;
        }
    }

    return 0;
}

uint8_t sichuan_input_cb(MBInstance_t *inst, uint8_t *buf,
                         uint16_t addr, uint16_t nregs)
{
    (void)inst;
    (void)buf;
    (void)addr;
    (void)nregs;
    return MB_EX_ILLEGAL_FUNCTION;
}

uint8_t sichuan_holding_cb(MBInstance_t *inst, uint8_t *buf,
                           uint16_t addr, uint16_t nregs, uint8_t mode)
{
    if (mode == MB_REG_READ) {
        return sichuan_read_holding(inst, buf, addr, nregs);
    }
    return sichuan_write_holding(inst, buf, addr, nregs);
}

void sichuan_register_callbacks(MBInstance_t *inst)
{
    eMBRegisterInputCB_Inst(inst, sichuan_input_cb);
    eMBRegisterHoldingCB_Inst(inst, sichuan_holding_cb);
}

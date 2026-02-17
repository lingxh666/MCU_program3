/**
 * @file mb_reg_dahu.c
 * @brief 大湖协议 Modbus 寄存器回调实现（03/06/10）
 */

#include "mb.h"
#include "work.h"
#include "freertos_app.h"
#include "sampling.h"
#include "Commtrigger.h"
#include "rtc.h"
#include "bsp_button.h"
#include <string.h>
#include <stdio.h>

#define DAHU_REG_BASE   40001
#define DAHU_REG_MAX    40543
#define DAHU_REG_COUNT  (DAHU_REG_MAX - DAHU_REG_BASE + 1)

/* ==== 外部变量 ==== */
extern State g_State;
extern SampleConfig g_SampleConfig;
extern RetainSampleModeConfig g_RetainSampleConfig;
extern CommSettingConfig g_CommSettingConfig;
extern SystemSettingConfig g_SystemSettingConfig;
extern RetainBottleState g_RetainBottleState;
extern CommRetainWindowContext g_comm_retain_ctx_a;
extern CommRetainWindowContext g_comm_retain_ctx_b;
extern uint8_t g_comm_retain_bottle_count;
extern CommTriggerRequest g_comm_trigger_request;
extern uint8_t g_water_sample_ready_A;
extern uint8_t g_water_sample_ready_B;
extern DayueCommandStatus_t g_dayue_cmd_status;
extern RetainSampleInfo_t g_LastRetainInfo;
extern RetainSampleInfo_t g_LastInstantRetainInfo;
extern FactorData_t g_FactorDataFromHost[MAX_FACTOR_COUNT];
extern uint8_t g_FactorCount;
extern calendar_type calendar;
extern DoorAccessRecord_t g_DoorAccessRecords[DOOR_ACCESS_RECORD_MAX];
extern uint8_t g_DoorAccessRecordCount;

extern uint8_t sampling_get_status(void);
extern uint8_t delivery_get_status(void);
extern uint8_t bottle_is_fault_active(void);
extern void system_reset_start(void);
extern void system_start_sequence(uint8_t mode);
extern uint8_t instant_retention_execute(uint8_t start_bottle, uint8_t bottle_count, uint8_t trigger_source);
extern uint8_t emptybottle(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);
extern uint32_t rtc_counter_get(void);
extern void log_retain_record(const RetainLogRecord *record);

static uint16_t dahu_regs[DAHU_REG_COUNT];

/* 门禁开门超时检测变量 */
static uint32_t g_dahu_door_open_timestamp = 0;

/* 十进制转BCD (如 30 → 0x30) */
static uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

/* 检测门禁开门超时 (返回: 0=正常, 2=开门超30分钟) */
static uint8_t dahu_check_door_timeout(void) {
    uint8_t door_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) ? 0 : 1;  // PE12: HIGH=关(0), LOW=开(1)
    uint32_t now = rtc_counter_get();

    if (door_state == 1) {  // 门开
        if (g_dahu_door_open_timestamp == 0) {
            g_dahu_door_open_timestamp = now;
        }
        if ((now - g_dahu_door_open_timestamp) > 30 * 60) {
            return 2;  // 开门超30分钟
        }
    } else {  // 门关
        g_dahu_door_open_timestamp = 0;
    }
    return 0;
}

static inline int32_t dahu_index(uint16_t reg)
{
    if (reg < DAHU_REG_BASE || reg > DAHU_REG_MAX) {
        return -1;
    }
    return (int32_t)(reg - DAHU_REG_BASE);
}

static void dahu_set_reg(uint16_t reg, uint16_t value)
{
    int32_t idx = dahu_index(reg);
    if (idx >= 0) {
        dahu_regs[idx] = value;
    }
}

static uint16_t dahu_get_reg(uint16_t reg)
{
    int32_t idx = dahu_index(reg);
    if (idx < 0) {
        return 0;
    }
    return dahu_regs[idx];
}

static void dahu_set_float(uint16_t start_reg, float value)
{
    uint8_t bytes[4];
    convertFloatToBytes(value, bytes);
    dahu_set_reg(start_reg,       ((uint16_t)bytes[0] << 8) | bytes[1]);
    dahu_set_reg(start_reg + 1,   ((uint16_t)bytes[2] << 8) | bytes[3]);
}

static uint16_t dahu_count_used_bottles(void)
{
    uint16_t used = 0;
    for (uint8_t i = 0; i < BOTTLE_COUNT; i++) {
        if (g_RetainBottleState.usedMask & (1u << i)) {
            used++;
        }
    }
    return used;
}

/**
 * @brief 桶状态映射：内部状态(0-50) -> 协议状态(0-9)
 * 协议定义: 0=待机,1=工作,2=清洗,3=进样,4=搅拌,5=送样,6=排空,7=留样,8=分析,9=等待
 */
static uint16_t dahu_map_bucket_state(uint8_t internal_state)
{
    switch (internal_state) {
        case 0:  return 0;  // 待机中 -> 待机
        case 2:  return 9;  // 等待采样 -> 等待
        case 4:  return 2;  // 采样前反吹 -> 清洗
        case 6:  return 3;  // 启动外接泵 -> 进样
        case 7:  return 3;  // 采样管存 -> 进样
        case 8:  return 3;  // 采样中 -> 进样
        case 9:  return 3;  // 采样暂停 -> 进样
        case 10: return 2;  // 采样后反吹 -> 清洗
        case 12: return 9;  // 采样完成 -> 等待
        case 13: return 9;  // 等待送样 -> 等待
        case 16: return 5;  // 送样前回抽 -> 送样
        case 19: return 5;  // 送样中 -> 送样
        case 22: return 8;  // 等待分析 -> 分析
        case 30: return 7;  // 旋转分样盘 -> 留样
        case 31: return 7;  // 留样瓶排空 -> 留样
        case 34: return 7;  // 留样前回抽 -> 留样
        case 35: return 7;  // 留前回抽完 -> 留样
        case 37: return 7;  // 留样管存 -> 留样
        case 38: return 7;  // 留样中 -> 留样
        case 39: return 9;  // 留样暂停 -> 等待
        case 40: return 7;  // 留样后回抽 -> 留样
        case 42: return 7;  // 留样排空 -> 留样
        case 47: return 7;  // 排空留样瓶 -> 留样
        case 48: return 9;  // 排空瓶完成 -> 等待
        default: return 0;  // 未知 -> 待机
    }
}

/**
 * @brief 工作状态映射：内部State(0-7) -> 协议状态(0-5)
 * 协议定义: 0=待机,1=运行_等待,2=运行_采样,3=运行_留样,4=维护,5=故障
 */
static uint16_t dahu_map_work_state(uint8_t internal_state, uint8_t bucket_state)
{
    switch (internal_state) {
        case 0:  return 0;  // 停止 -> 待机
        case 1:  // 运行 -> 根据桶状态细分
            if (bucket_state >= 4 && bucket_state <= 12) return 2;  // 采样相关 -> 运行_采样
            if (bucket_state >= 30 && bucket_state <= 48) return 3; // 留样相关 -> 运行_留样
            return 1;  // 其他 -> 运行_等待
        case 2:  return 1;  // 延时 -> 运行_等待
        case 3:  return 0;  // 空闲 -> 待机
        case 4:  return 4;  // 维护 -> 维护
        case 5:  return 5;  // 故障 -> 故障
        case 6:  return 1;  // 启动 -> 运行_等待
        case 7:  return 1;  // 复位中 -> 运行_等待
        default: return 0;  // 未知 -> 待机
    }
}

static void dahu_refresh_runtime_regs(void)
{
    /* 40001 当前混样桶编号：0=A，1=B，0xFFFF=未开始混样 */
    uint16_t bucket;
    if (g_State.State == 0 || g_State.State == 3) {
        bucket = 0xFFFF;  // 待机或空闲时，未开始混样
    } else {
        bucket = (g_State.InletThreeWayValve == 2) ? 1 : 0;
    }
    dahu_set_reg(40001, bucket);

    /* 40002 蓄水桶水样是否准备好 */
    uint8_t ready = (g_water_sample_ready_A || g_water_sample_ready_B) ? 1 : 0;
    dahu_set_reg(40002, ready);

    /* 40003 仪器工作状态（同时检查两个桶的状态） */
    uint16_t work_state_a = dahu_map_work_state(g_State.State, g_State.ABucketState);
    uint16_t work_state_b = dahu_map_work_state(g_State.State, g_State.BBucketState);
    /* 优先级: 留样(3) > 采样(2) > 等待(1) > 待机(0)，故障(5)和维护(4)最高 */
    uint16_t work_state = (work_state_a > work_state_b) ? work_state_a : work_state_b;
    dahu_set_reg(40003, work_state);

    /* 40004/40005 A/B桶状态（使用映射函数） */
    dahu_set_reg(40004, dahu_map_bucket_state(g_State.ABucketState));
    dahu_set_reg(40005, dahu_map_bucket_state(g_State.BBucketState));

    /* 40006/40007 液位状态：存水量>=满量时到位 */
    /* 满量 = (周期/间隔) * 单次采样量 */
    uint32_t full_volume = 0;
    if (g_SampleConfig.SampleInterval > 0) {
        uint32_t sample_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        full_volume = sample_count * (uint32_t)g_SampleConfig.SampleVolume;
    }
    dahu_set_reg(40006, (full_volume > 0 && g_State.SaveWarterA >= full_volume) ? 1 : 0);
    dahu_set_reg(40007, (full_volume > 0 && g_State.SaveWarterB >= full_volume) ? 1 : 0);

    /* 40008~40010 进样/送样/留样状态 - 与大岳协议相同的判断逻辑 */
    uint8_t samp_status = sampling_get_status();
    uint8_t deliv_status = delivery_get_status();
    uint16_t sample_state = (samp_status == 3 || samp_status == 4) ? 1 : 0;
    uint16_t delivery_state = (deliv_status == 3 || deliv_status == 4) ? 1 : 0;

    /* 留样状态: 优先检测转盘系统故障，其次检查最近留样结果 */
    uint8_t retain_state = 0;
    if (bottle_is_fault_active()) {
        retain_state = 1;  // 转盘系统故障
    } else if (g_LastRetainInfo.result == 0) {
        retain_state = 1;  // 最近留样失败
    }

    dahu_set_reg(40008, sample_state);
    dahu_set_reg(40009, delivery_state);
    dahu_set_reg(40010, retain_state);

    /* 40011 故障状态: 0=无报警,1=采样失败,2=供样失败,3=留样失败,4=温度报警 */
    uint16_t fault_code = 0x0000;
    if (sample_state) fault_code = 0x0001;
    else if (delivery_state) fault_code = 0x0002;
    else if (retain_state) fault_code = 0x0003;
    dahu_set_reg(40011, fault_code);

    /* 40012 已留瓶数（使用标记计数） */
    dahu_set_reg(40012, dahu_count_used_bottles());

    /* 40013~40036 瓶水量（已使用的瓶返回当前配置留样量） */
    for (uint8_t i = 0; i < 24; i++) {
        uint16_t vol = 0;
        if (g_RetainBottleState.usedMask & (1u << i)) {
            vol = g_RetainSampleConfig.SampleVolume;
            if (vol < 16) vol = 16;
            if (vol > 1000) vol = 1000;
        }
        dahu_set_reg(40013 + i, vol);
    }

    /* 40037~40038 冷藏柜温度（示例值） */
    dahu_set_float(40037, 4.0f);

    /* 40039~40050 阀/泵状态 */
    dahu_set_reg(40039, (g_State.InletThreeWayValve == 1) ? 1 : 0);  // A桶进样阀
    dahu_set_reg(40040, (g_State.InletThreeWayValve == 2) ? 1 : 0);  // B桶进样阀
    dahu_set_reg(40041, g_State.DrainA);   // A桶排水阀
    dahu_set_reg(40042, g_State.DrainB);   // B桶排水阀
    /* A/B桶搅拌器状态 - 与大岳协议相同的GPIO读取 */
    uint8_t mix_a_state = gpio_input_data_bit_read(GPIOB, GPIO_PINS_10) ? 0 : 1;
    uint8_t mix_b_state = gpio_input_data_bit_read(GPIOE, GPIO_PINS_15) ? 0 : 1;
    dahu_set_reg(40043, mix_a_state);
    dahu_set_reg(40044, mix_b_state);
    /* 外接泵状态：E11 低电平为运行 */
    uint8_t ext_pump = gpio_input_data_bit_read(GPIOE, GPIO_PINS_11) ? 0 : 1;
    dahu_set_reg(40045, ext_pump);         // 外接抽水泵
    dahu_set_reg(40046, g_State.SamplingMotor);      // 进样蠕动泵
    dahu_set_reg(40047, g_State.SampleThreeWayValve); // 留样阀
    dahu_set_reg(40048, g_State.OutletThreeWayValve); // 送样阀
    dahu_set_reg(40049, g_State.DeliveryMotor);      // 留样蠕动泵（与送样共用）
    dahu_set_reg(40050, g_State.DeliveryMotor);      // 送样蠕动泵（与留样共用）

    /* 40051/40052 门禁状态 - 与大岳协议相同的超时检测 */
    uint8_t door_open = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) ? 0 : 1;  // PE12: HIGH=关(0), LOW=开(1)
    dahu_set_reg(40051, dahu_check_door_timeout());  /* 报警状态: 0=正常, 2=开门超30分钟 */
    dahu_set_reg(40052, door_open);    /* 开关状态 */

    /* 40053~40058 最近一次门禁操作记录 - 使用门锁开关事件时间 */
    /* ★ 修复：使用g_door_last_event_time（门锁开关事件）而非g_DoorAccessRecords（刷卡记录） */
    if (g_door_last_event_time.year != 0) {
        dahu_set_reg(40053, dec_to_bcd(g_door_last_event_time.year % 100));   // 年(BCD)
        dahu_set_reg(40054, dec_to_bcd(g_door_last_event_time.month));        // 月(BCD)
        dahu_set_reg(40055, dec_to_bcd(g_door_last_event_time.day));          // 日(BCD)
        dahu_set_reg(40056, dec_to_bcd(g_door_last_event_time.hour));         // 时(BCD)
        dahu_set_reg(40057, dec_to_bcd(g_door_last_event_time.minute));       // 分(BCD)
        dahu_set_reg(40058, dec_to_bcd(g_door_last_event_time.second));       // 秒(BCD)
    } else {
        dahu_set_reg(40053, 0);
        dahu_set_reg(40054, 0);
        dahu_set_reg(40055, 0);
        dahu_set_reg(40056, 0);
        dahu_set_reg(40057, 0);
        dahu_set_reg(40058, 0);
    }
    /* 40059~40060: 卡号/密码(32位无符号整数) - 仍从刷卡记录获取 */
    DoorAccessRecord_t *last_record = (g_DoorAccessRecordCount > 0) ? &g_DoorAccessRecords[g_DoorAccessRecordCount - 1] : NULL;
    uint32_t last_card = (last_record) ? last_record->cardId : 0;
    if (last_card == 0) {
        last_card = g_SystemSettingConfig.CardId[0];  // 无刷卡记录时使用配置的第一张卡号
    }
    dahu_set_reg(40059, (uint16_t)(last_card >> 16));  // 高16位
    dahu_set_reg(40060, (uint16_t)(last_card & 0xFFFF)); // 低16位

    /* 40061~40069 超标留样基础信息 - 与大岳协议相同的BCD格式 */
    dahu_set_reg(40061, 0x2000 | dec_to_bcd(g_LastRetainInfo.year % 100));  // 年(BCD)
    dahu_set_reg(40062, dec_to_bcd(g_LastRetainInfo.month));                 // 月(BCD)
    dahu_set_reg(40063, dec_to_bcd(g_LastRetainInfo.day));                   // 日(BCD)
    dahu_set_reg(40064, dec_to_bcd(g_LastRetainInfo.hour));                  // 时(BCD)
    dahu_set_reg(40065, dec_to_bcd(g_LastRetainInfo.minute));                // 分(BCD)
    dahu_set_reg(40066, dec_to_bcd(g_LastRetainInfo.second));                // 秒(BCD)
    dahu_set_reg(40067, g_LastRetainInfo.result);
    dahu_set_reg(40068, g_LastRetainInfo.failReason);
    dahu_set_reg(40069, g_LastRetainInfo.bottleCount);

    /* 40070 超标因子总个数 - 与大岳协议相同：统计启用的通道数 */
    uint8_t factor_count = 0;
    for (uint8_t i = 0; i < 6; i++) {
        if (g_RetainSampleConfig.channelLimits[i].Enable) {
            factor_count++;
        }
    }
    dahu_set_reg(40070, factor_count);

    /* 40071~40112 因子1-6详情 - 与大岳协议相同 */
    /* 每个因子7个寄存器: 编号(1) + 值(2,浮点) + 下限(2,浮点) + 上限(2,浮点) */
    for (uint8_t i = 0; i < 6; i++) {
        uint16_t base_reg = 40071 + i * 7;
        dahu_set_reg(base_reg, g_RetainSampleConfig.channelLimits[i].FactorType);
        dahu_set_float(base_reg + 1, g_RetainSampleConfig.channelData[i]);
        dahu_set_float(base_reg + 3, g_RetainSampleConfig.channelLimits[i].LowerLimit);
        dahu_set_float(base_reg + 5, g_RetainSampleConfig.channelLimits[i].UpperLimit);
    }

    /* 40113~40178 瓶1-6留样详情 - 与大岳协议相同 */
    /* 每瓶11个寄存器: 瓶号(1)+结果(1)+容积(1)+原因(1)+第几瓶(1)+时间(6,BCD) */
    for (uint8_t bottle = 0; bottle < 6; bottle++) {
        uint16_t base_reg = 40113 + bottle * 11;
        if (bottle < g_LastRetainInfo.bottleCount) {
            dahu_set_reg(base_reg, (g_LastRetainInfo.startBottle + bottle) & 0xFF);  // 瓶号
            dahu_set_reg(base_reg + 1, g_LastRetainInfo.result);                      // 结果
            dahu_set_reg(base_reg + 2, g_LastRetainInfo.volume);                      // 容积
            dahu_set_reg(base_reg + 3, g_LastRetainInfo.failReason);                  // 失败原因
            dahu_set_reg(base_reg + 4, (bottle + 1) & 0xFF);                          // 第几瓶
            /* 留样时间(BCD格式) */
            dahu_set_reg(base_reg + 5, 0x2000 | dec_to_bcd(g_LastRetainInfo.year % 100));
            dahu_set_reg(base_reg + 6, dec_to_bcd(g_LastRetainInfo.month));
            dahu_set_reg(base_reg + 7, dec_to_bcd(g_LastRetainInfo.day));
            dahu_set_reg(base_reg + 8, dec_to_bcd(g_LastRetainInfo.hour));
            dahu_set_reg(base_reg + 9, dec_to_bcd(g_LastRetainInfo.minute));
            dahu_set_reg(base_reg + 10, dec_to_bcd(g_LastRetainInfo.second));
        } else {
            for (uint8_t j = 0; j < 11; j++) {
                dahu_set_reg(base_reg + j, 0);
            }
        }
    }

    /* 其他模式留样信息（40201~40212）- 按协议正确映射 */
    /* 40201~40206: 留样时间(BCD格式) */
    dahu_set_reg(40201, 0x2000 + g_LastRetainInfo.year);  // 年(BCD: 2024->0x2024)
    dahu_set_reg(40202, ((g_LastRetainInfo.month / 10) << 4) | (g_LastRetainInfo.month % 10));  // 月(BCD)
    dahu_set_reg(40203, ((g_LastRetainInfo.day / 10) << 4) | (g_LastRetainInfo.day % 10));      // 日(BCD)
    dahu_set_reg(40204, ((g_LastRetainInfo.hour / 10) << 4) | (g_LastRetainInfo.hour % 10));    // 时(BCD)
    dahu_set_reg(40205, ((g_LastRetainInfo.minute / 10) << 4) | (g_LastRetainInfo.minute % 10)); // 分(BCD)
    dahu_set_reg(40206, ((g_LastRetainInfo.second / 10) << 4) | (g_LastRetainInfo.second % 10)); // 秒(BCD)
    /* 40207~40212: 结果/原因/瓶号/瓶数/留样量/模式 */
    dahu_set_reg(40207, g_LastRetainInfo.result);
    dahu_set_reg(40208, g_LastRetainInfo.failReason);
    dahu_set_reg(40209, g_LastRetainInfo.startBottle);
    dahu_set_reg(40210, g_LastRetainInfo.bottleCount);
    dahu_set_reg(40211, g_LastRetainInfo.volume);
    dahu_set_reg(40212, g_LastRetainInfo.mode);

    /* 瞬时留样信息（40213~40223）- 按协议正确映射 */
    /* 40213~40218: 留样时间(BCD格式) */
    dahu_set_reg(40213, 0x2000 + g_LastInstantRetainInfo.year);
    dahu_set_reg(40214, ((g_LastInstantRetainInfo.month / 10) << 4) | (g_LastInstantRetainInfo.month % 10));
    dahu_set_reg(40215, ((g_LastInstantRetainInfo.day / 10) << 4) | (g_LastInstantRetainInfo.day % 10));
    dahu_set_reg(40216, ((g_LastInstantRetainInfo.hour / 10) << 4) | (g_LastInstantRetainInfo.hour % 10));
    dahu_set_reg(40217, ((g_LastInstantRetainInfo.minute / 10) << 4) | (g_LastInstantRetainInfo.minute % 10));
    dahu_set_reg(40218, ((g_LastInstantRetainInfo.second / 10) << 4) | (g_LastInstantRetainInfo.second % 10));
    /* 40219~40223: 结果/原因/瓶号/瓶数/留样量 */
    dahu_set_reg(40219, g_LastInstantRetainInfo.result);
    dahu_set_reg(40220, g_LastInstantRetainInfo.failReason);
    dahu_set_reg(40221, g_LastInstantRetainInfo.startBottle);
    dahu_set_reg(40222, g_LastInstantRetainInfo.bottleCount);
    dahu_set_reg(40223, g_LastInstantRetainInfo.volume);

    /* 40304~40351 超标阈值设置区 - 与大岳协议相同 */
    /* 6个因子 × 8寄存器: 编号(1)+下限(2)+上限(2)+平行样数(1)+留样间隔(1)+启用(1) */
    for (uint8_t i = 0; i < 6; i++) {
        uint16_t base_reg = 40304 + i * 8;
        dahu_set_reg(base_reg, g_RetainSampleConfig.channelLimits[i].FactorType);
        dahu_set_float(base_reg + 1, g_RetainSampleConfig.channelLimits[i].LowerLimit);
        dahu_set_float(base_reg + 3, g_RetainSampleConfig.channelLimits[i].UpperLimit);
        dahu_set_reg(base_reg + 5, g_RetainSampleConfig.channelLimits[i].ParallelCount);
        dahu_set_reg(base_reg + 6, g_RetainSampleConfig.channelLimits[i].RetainInterval);
        dahu_set_reg(base_reg + 7, g_RetainSampleConfig.channelLimits[i].Enable);
    }

    /* 40384~40391 采样设置 - 与大岳协议相同 */
    dahu_set_reg(40384, g_SampleConfig.SamplingMode);
    dahu_set_reg(40385, g_SampleConfig.SampleVolume);
    dahu_set_reg(40386, g_SampleConfig.SampleInterval);
    dahu_set_reg(40387, g_SampleConfig.DischargeVolume);   // 排放等比排放量
    dahu_set_reg(40388, g_SampleConfig.SampleInterval);    // 流量比例间隔
    dahu_set_reg(40389, g_SampleConfig.FlowRatio);         // 流量比例采样比例
    dahu_set_reg(40390, g_SampleConfig.FlowStart);         // 流量触发阈值
    dahu_set_reg(40391, g_SampleConfig.SampleInterval);    // 流量触发间隔

    /* 40408~40411 留样设置 - 与大岳协议相同 */
    dahu_set_reg(40408, g_RetainSampleConfig.Mode);
    dahu_set_reg(40409, g_RetainSampleConfig.SampleVolume);
    dahu_set_reg(40410, g_RetainSampleConfig.ParallelCount);
    dahu_set_reg(40411, g_RetainSampleConfig.MixCount);

    /* 40433~40462 因子数据区 - 与大岳协议相同 */
    /* 10个因子 × 3寄存器: 编号(1) + 值(2,浮点) */
    for (uint8_t i = 0; i < 10; i++) {
        uint16_t base_reg = 40433 + i * 3;
        if (i < g_FactorCount) {
            dahu_set_reg(base_reg, g_FactorDataFromHost[i].factorType);
            dahu_set_float(base_reg + 1, g_FactorDataFromHost[i].factorValue);
        } else {
            dahu_set_reg(base_reg, 0);
            dahu_set_float(base_reg + 1, 0.0f);
        }
    }

    /* 40482~40487 系统时间读取 */
    rtc_time_get();
    dahu_set_reg(40482, (uint8_t)(calendar.year - 2000));
    dahu_set_reg(40483, calendar.month);
    dahu_set_reg(40484, calendar.date);
    dahu_set_reg(40485, calendar.hour);
    dahu_set_reg(40486, calendar.min);
    dahu_set_reg(40487, calendar.sec);

    /* 40488~40507 门禁卡号区 - 与大岳协议相同 */
    /* 10个卡号，每个2寄存器(32位) */
    for (uint8_t i = 0; i < 10; i++) {
        uint32_t cardId = g_SystemSettingConfig.CardId[i];
        dahu_set_reg(40488 + i * 2, (uint16_t)(cardId >> 16));
        dahu_set_reg(40489 + i * 2, (uint16_t)(cardId & 0xFFFF));
    }

    /* 40508~40509 动态门禁密码 - 算法: 999999 - 日时分(DDHHMI) */
    uint32_t dyn_code = 999999 - (calendar.date * 10000 + calendar.hour * 100 + calendar.min);
    dahu_set_reg(40508, (uint16_t)(dyn_code >> 16));
    dahu_set_reg(40509, (uint16_t)(dyn_code & 0xFFFF));

    /* 40528~40534 设备信息区 - 与大岳协议相同 */
    dahu_set_reg(40528, g_CommSettingConfig.DeviceAddr);  // 联网地址
    /* 40529~40534: 出厂编号(ASCII) */
    const char *serial = g_SystemSettingConfig.SoftwareSerial;
    for (uint8_t i = 0; i < 6; i++) {
        dahu_set_reg(40529 + i, ((uint16_t)serial[i * 2] << 8) | serial[i * 2 + 1]);
    }

    /* 40542~40543 数采同步信号 - 由上位机写入，采样器接收后执行动作并清零 */
    /* 40542: 送样同步信号 - 读取当前值(写入后会被清零) */
    /* 40543: 超标同步信号 - 读取当前值(写入后会被清零) */
    /* 注意: 这两个寄存器的值在写入处理中设置，读取时返回当前镜像值 */
}

static uint8_t dahu_input_cb(MBInstance_t *inst, uint8_t *buf,
                             uint16_t addr, uint16_t nregs)
{
    (void)inst;
    (void)buf;
    (void)addr;
    (void)nregs;
    printf("[大湖] 不支持读输入寄存器(0x04)\r\n");
    return MB_EX_ILLEGAL_FUNCTION;
}

static uint8_t dahu_handle_bucket_action(uint16_t reg, uint16_t value)
{
    uint8_t bucket = 2; /* 2=自动 */
    if (reg == 40535) bucket = 0;      /* A */
    else if (reg == 40536) bucket = 1; /* B */

    uint32_t now = rtc_counter_get();
    switch (value) {
    case 0x0001: /* 采样 */
        if (bucket == 0) {
            g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.a_cmd_timestamp = now;
        } else if (bucket == 1) {
            g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.b_cmd_timestamp = now;
        } else {
            g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.ab_cmd_timestamp = now;
        }
        g_comm_trigger_request.request_type = COMM_REQ_SAMPLING;
        g_comm_trigger_request.bucket_selector = bucket;
        g_comm_trigger_request.volume = g_SampleConfig.SampleVolume;
        g_comm_trigger_request.pending = 1;
        return 0;

    case 0x0002: /* 排空 */
        if (bucket == 0) {
            g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.a_cmd_timestamp = now;
        } else if (bucket == 1) {
            g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.b_cmd_timestamp = now;
        } else {
            g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.ab_cmd_timestamp = now;
        }
        g_comm_trigger_request.request_type = COMM_REQ_DRAIN;
        g_comm_trigger_request.bucket_selector = bucket;
        g_comm_trigger_request.volume = 0;
        g_comm_trigger_request.pending = 1;
        return 0;

    case 0x0003: /* 送样 */
        if (bucket == 0) {
            g_dayue_cmd_status.a_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.a_cmd_timestamp = now;
        } else if (bucket == 1) {
            g_dayue_cmd_status.b_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.b_cmd_timestamp = now;
        } else {
            g_dayue_cmd_status.ab_bucket_status = CMD_STATUS_RECEIVED;
            g_dayue_cmd_status.ab_cmd_timestamp = now;
        }
        g_comm_trigger_request.request_type = COMM_REQ_DELIVERY;
        g_comm_trigger_request.bucket_selector = bucket;
        g_comm_trigger_request.volume = 0;
        g_comm_trigger_request.pending = 1;
        return 0;

    default:
        return MB_EX_ILLEGAL_VALUE;
    }
}

static uint8_t dahu_handle_retain_command(uint16_t startBottle, uint16_t bottleCount, uint16_t type)
{
    /* 类型 1=A 2=B 3=自动 4=瞬时 5=瓶排空 */
    if (type >= 1 && type <= 3) {
        CommRetainWindowContext *ctx = NULL;
        uint8_t bucket_id = 0;

        if (type == 1) {
            ctx = &g_comm_retain_ctx_a;
            bucket_id = 0;
        } else if (type == 2) {
            ctx = &g_comm_retain_ctx_b;
            bucket_id = 1;
        } else {
            uint8_t a_ready = (g_comm_retain_ctx_a.in_window || g_comm_retain_ctx_a.delivery_time > 0) && !g_comm_retain_ctx_a.retain_executed;
            uint8_t b_ready = (g_comm_retain_ctx_b.in_window || g_comm_retain_ctx_b.delivery_time > 0) && !g_comm_retain_ctx_b.retain_executed;

            if (a_ready && !b_ready) {
                ctx = &g_comm_retain_ctx_a;
                bucket_id = 0;
            } else if (b_ready && !a_ready) {
                ctx = &g_comm_retain_ctx_b;
                bucket_id = 1;
            } else if (a_ready) {
                ctx = &g_comm_retain_ctx_a;
                bucket_id = 0;
            } else {
                return MB_EX_SLAVE_DEVICE_FAIL;
            }
        }

        if (ctx != NULL) {
            ctx->comm_trigger_received = 1;
            ctx->bucket_id = bucket_id;
        }
        g_comm_retain_bottle_count = (uint8_t)bottleCount;

        RetainLogRecord log;
        memset(&log, 0, sizeof(RetainLogRecord));
        log.retain_mode = 2; /* 通讯触发 */
        log.bottle_number = startBottle;
        log.retain_volume = 0;
        log.result = 1;
        log.start_time = rtc_counter_get();
        log_retain_record(&log);

        g_dayue_cmd_status.retention_status = CMD_STATUS_RECEIVED;
        g_dayue_cmd_status.retention_cmd_timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        return 0;
    }
    else if (type == 4) {
        uint8_t ok = instant_retention_execute((uint8_t)startBottle, (uint8_t)bottleCount, 1);
        g_dayue_cmd_status.retention_status = ok ? CMD_STATUS_COMPLETED : CMD_STATUS_FAILED;
        return ok ? 0 : MB_EX_SLAVE_DEVICE_FAIL;
    }
    else if (type == 5) {
        uint8_t all_ok = 1;
        for (uint16_t i = 0; i < bottleCount; i++) {
            uint16_t bottle = startBottle + i;
            if (bottle < 1 || bottle > BOTTLE_COUNT) {
                all_ok = 0;
                continue;
            }
            if (!emptybottle((uint8_t)bottle, 100, 60000)) {
                all_ok = 0;
            }
        }
        return all_ok ? 0 : MB_EX_SLAVE_DEVICE_FAIL;
    }

    return MB_EX_ILLEGAL_VALUE;
}

static uint8_t dahu_handle_time_set(const uint8_t *buf)
{
    uint16_t year   = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t month  = ((uint16_t)buf[2] << 8) | buf[3];
    uint16_t day    = ((uint16_t)buf[4] << 8) | buf[5];
    uint16_t hour   = ((uint16_t)buf[6] << 8) | buf[7];
    uint16_t minute = ((uint16_t)buf[8] << 8) | buf[9];
    uint16_t second = ((uint16_t)buf[10] << 8) | buf[11];
    uint16_t cmd    = ((uint16_t)buf[12] << 8) | buf[13]; /* 0x0001 生效 */

    if (cmd == 0x0001) {
        calendar_type t;
        t.year = 2000 + (uint8_t)year;
        t.month = (uint8_t)month;
        t.date = (uint8_t)day;
        t.hour = (uint8_t)hour;
        t.min = (uint8_t)minute;
        t.sec = (uint8_t)second;
        t.week = 0;
        rtc_time_set(&t);
    }
    return 0;
}

static uint8_t dahu_holding_cb(MBInstance_t *inst, uint8_t *buf,
                               uint16_t addr, uint16_t nregs, uint8_t mode)
{
    (void)inst;

    /* 地址越界检查 */
    if ((addr + nregs) > DAHU_REG_COUNT) {
        return MB_EX_ILLEGAL_ADDRESS;
    }

    uint16_t start_reg = DAHU_REG_BASE + addr;

    if (mode == MB_REG_READ) {
        dahu_refresh_runtime_regs();
        for (uint16_t i = 0; i < nregs; i++) {
            uint16_t v = dahu_get_reg(start_reg + i);
            buf[i * 2]     = (uint8_t)(v >> 8);
            buf[i * 2 + 1] = (uint8_t)(v & 0xFF);
        }
        return 0;
    }

    /* 写操作 */
    if (nregs == 1) {
        uint16_t val = ((uint16_t)buf[0] << 8) | buf[1];

        /* 触发采样/排空/送样 */
        if (start_reg >= 40535 && start_reg <= 40537) {
            return dahu_handle_bucket_action(start_reg, val);
        }
        /* 直接瞬时留样 */
        if (start_reg == 40541) {
            if (val >= 1 && val <= 24) {
                uint8_t ok = instant_retention_execute(0, (uint8_t)val, 1);
                return ok ? 0 : MB_EX_SLAVE_DEVICE_FAIL;
            }
            return MB_EX_ILLEGAL_VALUE;
        }

        /* 40431 控制命令 - 与大岳协议相同 */
        if (start_reg == 40431) {
            printf("[大湖] 控制命令, 值=0x%04X\r\n", val);
            switch (val) {
            case 0x0001:  /* 远程复位 */
                printf("[大湖] 远程复位\r\n");
                system_reset_start();
                break;
            case 0x0002:  /* 远程启动 */
                printf("[大湖] 远程启动\r\n");
                if (g_State.State == 0) {
                    system_start_sequence(START_MODE_MANUAL);
                }
                break;
            case 0x0003:  /* 远程停止 */
                printf("[大湖] 远程停止\r\n");
                if (g_State.State == 1) {
                    g_State.State = 0;
                }
                break;
            case 0x0004:  /* 远程恢复 */
                printf("[大湖] 远程恢复\r\n");
                if (g_State.State == 3) {
                    g_State.State = 1;
                }
                break;
            default:
                printf("[大湖] 未知控制命令: 0x%04X\r\n", val);
                break;
            }
            return 0;
        }

        /* 40432 复位瓶信息 - 与大岳协议相同 */
        if (start_reg == 40432) {
            printf("[大湖] 复位瓶信息, 瓶号=%d\r\n", val);
            if (val >= 1 && val <= 24) {
                g_RetainBottleState.usedMask &= ~(1UL << (val - 1));
            } else if (val == 0xFF) {
                g_RetainBottleState.usedMask = 0;
            }
            return 0;
        }

        /* 40542 送样同步信号 - 数采通知采样器需要供样 */
        if (start_reg == 40542) {
            printf("[大湖] 送样同步信号, 值=0x%04X\r\n", val);
            if (val == 0x0001) {
                printf("[大湖] 收到供样请求，启动送样\r\n");
                /* TODO: 触发送样动作 */
            }
            dahu_set_reg(40542, 0);  /* 接收后自动清零 */
            return 0;
        }

        /* 40543 超标同步信号 - 数采通知采样器水质超标 */
        if (start_reg == 40543) {
            printf("[大湖] 超标同步信号, 值=0x%04X\r\n", val);
            if (val == 0x0001) {
                printf("[大湖] 收到超标通知，启动超标留样\r\n");
                /* TODO: 触发超标留样动作 */
            }
            dahu_set_reg(40543, 0);  /* 接收后自动清零 */
            return 0;
        }

        /* 一般寄存器写入：同步部分配置到运行时结构 */
        dahu_set_reg(start_reg, val);
        if (start_reg == 40384) g_SampleConfig.SamplingMode = (uint8_t)val;
        if (start_reg == 40385) g_SampleConfig.SampleVolume = val;
        if (start_reg == 40386) g_SampleConfig.SampleInterval = val;
        if (start_reg == 40408) g_RetainSampleConfig.Mode = (uint8_t)val;
        if (start_reg == 40409) g_RetainSampleConfig.SampleVolume = val;
        if (start_reg == 40410) g_RetainSampleConfig.ParallelCount = (uint8_t)val;
        if (start_reg == 40411) g_RetainSampleConfig.MixCount = (uint8_t)val;
        return 0;
    }

    /* 多寄存器写 */
    if (start_reg == 40475 && nregs == 7) {
        uint8_t ret = dahu_handle_time_set(buf);
        /* 也保存在寄存器镜像中，便于回读 */
        for (uint16_t i = 0; i < nregs; i++) {
            uint16_t v = ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1];
            dahu_set_reg(start_reg + i, v);
        }
        return ret;
    }

    if (start_reg == 40538 && nregs == 3) {
        uint16_t startBottle = ((uint16_t)buf[0] << 8) | buf[1];
        uint16_t bottleCount = ((uint16_t)buf[2] << 8) | buf[3];
        uint16_t type        = ((uint16_t)buf[4] << 8) | buf[5];
        return dahu_handle_retain_command(startBottle, bottleCount, type);
    }

    if (start_reg == 40384 && nregs >= 3) {
        g_SampleConfig.SamplingMode = (uint8_t)(((uint16_t)buf[0] << 8) | buf[1]);
        g_SampleConfig.SampleVolume = ((uint16_t)buf[2] << 8) | buf[3];
        g_SampleConfig.SampleInterval = ((uint16_t)buf[4] << 8) | buf[5];
    }

    if (start_reg == 40408 && nregs >= 4) {
        g_RetainSampleConfig.Mode = (uint8_t)(((uint16_t)buf[0] << 8) | buf[1]);
        g_RetainSampleConfig.SampleVolume = ((uint16_t)buf[2] << 8) | buf[3];
        g_RetainSampleConfig.ParallelCount = (uint8_t)(((uint16_t)buf[4] << 8) | buf[5]);
        g_RetainSampleConfig.MixCount = (uint8_t)(((uint16_t)buf[6] << 8) | buf[7]);
    }

    /* 默认行为：逐个寄存器写入镜像，便于后续回读 */
    for (uint16_t i = 0; i < nregs; i++) {
        uint16_t v = ((uint16_t)buf[i * 2] << 8) | buf[i * 2 + 1];
        dahu_set_reg(start_reg + i, v);
    }

    return 0;
}

void dahu_register_callbacks(MBInstance_t *inst)
{
    eMBRegisterInputCB_Inst(inst, dahu_input_cb);
    eMBRegisterHoldingCB_Inst(inst, dahu_holding_cb);
}

#include "sampling.h"
#include "sampling_time.h"
#include "sample_id.h"
#include "record_cache.h"
#include "Flowtrigger.h"
#include "Timetrigger.h"
#include "Switchtrigger.h"
#include "Commtrigger.h"
#include "freertos_app.h"
#include "work.h"
#include "screen.h"
#include "retain_judge.h"
#include "queue.h"
#include "semphr.h"
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "event_groups.h"
#include "app_flashdb.h"

#include <string.h>
#include <stdint.h>

/* Bottle fault codes */
#define FAULT_BOTTLE_SENSOR_MISSING 0x1001
#define FAULT_BOTTLE_RESET_TIMEOUT 0x1002
#define FAULT_BOTTLE_RESET_FAILED 0x1003

#define TSDB_EVT_BOTTLE_SENSOR_FAULT 0x00F1
#define TSDB_EVT_BOTTLE_RESET_FAULT 0x00F2

// StartupSamplingMode 枚举已移至 sampling_time.h

static inline uint32_t unix_to_2000(uint32_t unix_ts)
{
    /* 1970-01-01到2000-01-01的秒数：30年+7个闰日 */
    const uint32_t offset = (30UL * 365UL + 7UL) * 24UL * 3600UL;
    return (unix_ts >= offset) ? (unix_ts - offset) : 0;
}

static SamplingContext g_sampling_ctx = {.stage = SAMP_IDLE};
static DeliveryContext g_delivery_ctx = {.stage = DELIV_IDLE};

// ============================================================================
// 桶上下文全局变量（sample_id继承机制）
// ============================================================================
WaterSampleContext g_water_ctx_A = {0};
WaterSampleContext g_water_ctx_B = {0};

// 调度器全局变量定义已移至 sampling_time.c

void log_retain_record(const RetainLogRecord *record)
{
    if (!record)
    {
        return;
    }

    printf("[LOG] retain mode=%d reason=%d bottle=%d volume=%d\r\n",
           record->retain_mode,
           record->retain_reason,
           record->bottle_number,
           record->retain_volume);

    if (tsdb_is_ready())
    {
        tsdb_event_append(LOG_RETAIN_RECORD, record, sizeof(RetainLogRecord));
    }
}

void log_discard_record(const DiscardLogRecord *record)
{
    if (!record)
    {
        return;
    }

    printf("[LOG] discard bottle=%d result=%d\r\n",
           record->bottle_number, record->result);

    if (tsdb_is_ready())
    {
        tsdb_event_append(LOG_DISCARD_RECORD, record, sizeof(DiscardLogRecord));
    }
}

static void log_bottle_sensor_fault(uint16_t fault_code, const char *description)
{
    if (!description)
    {
        description = "";
    }

    printf("[FAULT] bottle sensor: code=0x%04X desc=%s\r\n", fault_code, description);

    if (!tsdb_is_ready())
    {
        return;
    }

    struct
    {
        uint32_t timestamp;
        uint16_t code;
        uint8_t desc_len;
        char desc[64];
    } fault = {0};

    fault.timestamp = rtc_counter_get();
    fault.code = fault_code;
    strncpy(fault.desc, description, sizeof(fault.desc) - 1);
    fault.desc[sizeof(fault.desc) - 1] = '\0';
    fault.desc_len = (uint8_t)strlen(fault.desc);

    tsdb_event_append(TSDB_EVT_BOTTLE_SENSOR_FAULT, &fault, sizeof(fault));
}

static void log_bottle_reset_fault(uint16_t fault_code, const char *description)
{
    if (!description)
    {
        description = "";
    }

    printf("[FAULT] bottle reset: code=0x%04X desc=%s\r\n", fault_code, description);

    if (!tsdb_is_ready())
    {
        return;
    }

    struct
    {
        uint32_t timestamp;
        uint16_t code;
        uint8_t desc_len;
        char desc[64];
    } fault = {0};

    fault.timestamp = rtc_counter_get();
    fault.code = fault_code;
    strncpy(fault.desc, description, sizeof(fault.desc) - 1);
    fault.desc[sizeof(fault.desc) - 1] = '\0';
    fault.desc_len = (uint8_t)strlen(fault.desc);

    tsdb_event_append(TSDB_EVT_BOTTLE_RESET_FAULT, &fault, sizeof(fault));
}

HostStatusCode g_host_status = {0, 0, 0, 0};

int scheduler_is_emergency_active(void)
{
    extern volatile uint8_t g_retention_abort_flag;
    return (int)g_retention_abort_flag;
}

void update_bucket_state(uint8_t bucket_id, BucketStateCode state)
{
    if (bucket_id == 0u)
    {
        g_host_status.bucket_a_state = (uint8_t)state;
    }
    else if (bucket_id == 1u)
    {
        g_host_status.bucket_b_state = (uint8_t)state;
    }
    g_host_status.last_update_time = rtc_counter_get();
}

void update_system_running_state(uint8_t running)
{
    g_host_status.system_running = running ? 1u : 0u;
    g_host_status.last_update_time = rtc_counter_get();
}

// 系统复位状态机
typedef enum
{
    RESET_STATE_IDLE = 0,         // 空闲状态
    RESET_STATE_START,            // 启动复位（停止电机泵）
    RESET_STATE_DRAINING,         // 排空中
    RESET_STATE_BOTTLE_RESETTING, // 留样瓶复位中
    RESET_STATE_CLEANUP,          // 清理状态
    RESET_STATE_COMPLETE          // 完成
} SystemResetState;

static struct
{
    SystemResetState state;
    uint32_t drain_start_time;
    uint16_t drain_duration;
    uint32_t bottle_reset_start_time; // 留样瓶复位开始时间
    uint32_t bottle_reset_timeout;    // 留样瓶复位超时时间(ms)
    uint8_t bottle_reset_result;      // 留样瓶复位结果 (0=进行中, 1=成功, 2=超时/失败)
    uint8_t home_page_sent;           // 主页跳转是否已发送 (防止重复发送)
} g_reset_fsm = {RESET_STATE_IDLE, 0, 0, 0, 0, 0, 0};

/**
 * @brief 启动系统复位（非阻塞）
 */
void system_reset_start(void)
{
    if (g_reset_fsm.state != RESET_STATE_IDLE)
    {
        printf("[系统复位] 已在进行中，状态=%d\r\n", g_reset_fsm.state);
        return;
    }
    g_reset_fsm.state = RESET_STATE_START;
    printf("[系统复位] 复位序列已启动（非阻塞）\r\n");
}
/**
 * @brief 系统复位状态机更新（非阻塞，需周期性调用）
 * @return 0=进行中, 1=已完成
 */
uint8_t system_reset_update(void)
{
    // 声明主页跳转缓冲区（避免switch作用域问题）
    uint8_t home_page_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x0B};

    switch (g_reset_fsm.state)
    {
    case RESET_STATE_IDLE:
        return 1; // 空闲，无需处理

    case RESET_STATE_START:
        printf("[系统复位] ========================================\r\n");
        printf("[系统复位] 系统复位序列已启动\r\n");

        /* 1. 设置全局中止标志 */
        extern volatile uint8_t g_manual_operation_abort_flag;
        g_manual_operation_abort_flag = 1;
        printf("[系统复位] 中止标志已设置\r\n");

        /* 2. 停止屏幕分发器 */
        extern void screen_dispatcher_stop(void);
        screen_dispatcher_stop();
        printf("[系统复位] 屏幕分发器已停止\r\n");

        /* 3. 停止所有电机 */
        MotorStop(1);
        MotorStop(2);
        set_motor_rpm(0);
        printf("[系统复位] 所有电机已停止\r\n");

        /* 4. 停止所有泵 */
        ExternalPumpStop;
        AcidPumpStop;
        MixAStop;
        MixBStop;
        printf("[系统复位] 所有泵已停止\r\n");

        /* 5. 检查留样瓶传感器状态 */
        if (!bottle_sensor_is_working())
        {
            printf("[系统复位] 警告：留样瓶传感器异常或缺失\r\n");
            log_bottle_sensor_fault(FAULT_BOTTLE_SENSOR_MISSING, "留样瓶传感器缺失或无信号");
        }

        /* 6. 启动排空 */
        printf("[系统复位] 开始强制排空两个桶...\r\n");
        DrainARun;
        DrainBRun;

        /* 7. 启动留样瓶复位（与排空并行） */
        printf("[系统复位] 开始留样瓶复位到1号瓶...\r\n");
        if (bottle_home_to_1_start(100, 120000) != 0)
        {
            printf("[系统复位] 留样瓶复位已在进行中\r\n");
        }

        g_reset_fsm.drain_duration = g_SampleConfig.BucketDrainTime;
        if (g_reset_fsm.drain_duration == 0 || g_reset_fsm.drain_duration > 600)
        {
            g_reset_fsm.drain_duration = 60;
        }
        g_reset_fsm.bottle_reset_timeout = 120000; // 120秒超时
        g_reset_fsm.bottle_reset_start_time = g_tmr2_seconds;
        g_reset_fsm.bottle_reset_result = 0; // 进行中

        printf("[系统复位] 排空 %u 秒，留样瓶复位 %u ms...\r\n",
               g_reset_fsm.drain_duration, g_reset_fsm.bottle_reset_timeout);

        g_reset_fsm.drain_start_time = g_tmr2_seconds;
        g_reset_fsm.state = RESET_STATE_DRAINING;
        return 0;

    case RESET_STATE_DRAINING:
    {
        uint8_t drain_complete = 0;
        uint8_t bottle_complete = 0;

        /* 检查排空是否完成 */
        if ((g_tmr2_seconds - g_reset_fsm.drain_start_time) >= g_reset_fsm.drain_duration)
        {
            DrainAStop;
            DrainBStop;
            printf("[系统复位] 桶排空完成\r\n");
            drain_complete = 1;
        }

        /* 检查留样瓶复位状态 */
        uint8_t bottle_result = bottle_home_to_1_check();
        if (bottle_result == 1 && g_reset_fsm.bottle_reset_result == 0)
        {
            printf("[系统复位] 留样瓶复位成功\r\n");
            g_reset_fsm.bottle_reset_result = 1;
            bottle_complete = 1;
            bottle_clear_fault();  /* ★ 清除故障标志 */
        }
        else if (bottle_result == 2 && g_reset_fsm.bottle_reset_result == 0)
        {
            printf("[系统复位] 留样瓶复位超时失败\r\n");
            log_bottle_reset_fault(FAULT_BOTTLE_RESET_TIMEOUT, "留样瓶复位超时");
            g_reset_fsm.bottle_reset_result = 2;
            bottle_complete = 1;
            bottle_set_fault(BOTTLE_FAULT_RESET_TIMEOUT);  /* ★ 设置故障标志 */
        }
        else if (bottle_result == 1)
        {
            // 已经处理过成功状态，只设置完成标志
            bottle_complete = 1;
        }
        else if (bottle_result == 2)
        {
            // 已经处理过失败状态，只设置完成标志
            bottle_complete = 1;
        }

        /* 两者都完成才进入下一状态 */
        if (drain_complete && bottle_complete)
        {
            g_reset_fsm.state = RESET_STATE_BOTTLE_RESETTING;
            printf("[系统复位] 排空和留样瓶复位都已完成\r\n");
        }
    }
    return 0;

    case RESET_STATE_BOTTLE_RESETTING:
        /* 这个状态主要用于确认留样瓶复位的最终状态 */
        if (g_reset_fsm.bottle_reset_result == 1)
        {
            printf("[系统复位] 留样瓶复位成功，正在更新KVDB...\r\n");

            // 强制更新瓶位状态
            extern uint8_t g_current_bottle_number;
            extern RetainSampleModeConfig g_RetainSampleConfig;
            extern RetainBottleState g_RetainBottleState;

            g_current_bottle_number = 1;
            g_RetainBottleState.currentBottle = 1;
            g_RetainBottleState.usedMask = 0;
            g_RetainSampleConfig.bottleNumber = 1;
            g_RetainSampleConfig.EnableSample = 1;  // 确保留样功能启用

            // 立即保存到KVDB
            cfg_save_retain(&g_RetainSampleConfig);
            cfg_save_retain_state(&g_RetainBottleState);

            printf("[系统复位] 瓶位状态已保存到KVDB\r\n");
        }
        else if (g_reset_fsm.bottle_reset_result == 2)
        {
            printf("[系统复位] 留样瓶复位失败，但继续系统复位流程\r\n");
        }
        g_reset_fsm.state = RESET_STATE_CLEANUP;
        return 0;

    case RESET_STATE_CLEANUP:
        /* 6. 恢复所有阀门到初始状态 */
        InletThreeWayValveB;
        OutletThreeWayValveClose();
        SampleThreeWayValveSTAY;
        InstantThreeWayValveDirect;
        CleanStop;
        printf("[系统复位] 所有阀门已重置为初始状态\r\n");

        /* 7. 停止调度器 */
        printf("[系统复位] 正在停止调度器...\r\n");

        // 停止时间等比调度器
        tp_scheduler_stop();

        // 停止流量触发调度器
        ft_scheduler_stop();

        printf("[系统复位] 调度器已停止\r\n");

        /* 8. 重置状态机 */
        printf("[系统复位] 正在重置状态机...\r\n");

        // 重置采样状态机
        memset(&g_sampling_ctx, 0, sizeof(g_sampling_ctx));
        g_sampling_ctx.stage = SAMP_IDLE;

        // 重置送样状态机
        memset(&g_delivery_ctx, 0, sizeof(g_delivery_ctx));
        g_delivery_ctx.stage = DELIV_IDLE;

        // 重置瞬时送样状态机
        instant_delivery_reset();

        printf("[系统复位] 状态机重置完成\r\n");

        /* 9. 更新全局状态到待机 */
        g_State.State = 0;
        g_State.CurrentBucketRunState = 0;
        g_State.ABucketState = 0;
        g_State.BBucketState = 0;
        g_State.InstantOperationState = 0;
        g_State.SaveWarterA = 0;
        g_State.SaveWarterB = 0;
        g_State.SamplingMotor = 0;
        g_State.DeliveryMotor = 0;
        g_State.DrainA = 0;
        g_State.DrainB = 0;
        g_State.InletThreeWayValve = 2;
        g_State.OutletThreeWayValve = 2;
        g_State.SampleThreeWayValve = 1;
        g_State.InstantThreeWayValve = 0;
        printf("[系统复位] 系统状态设置为待机（State=0）\r\n");

        /* 10. 记录TSDB事件和错误报告 */
        if (tsdb_is_ready())
        {
            struct
            {
                uint32_t timestamp;
                uint8_t reset_reason;
                uint8_t bottle_reset_result;
                uint8_t sensor_status;
            } reset_data;
            reset_data.timestamp = rtc_counter_get();
            reset_data.reset_reason = 0xFF;
            reset_data.bottle_reset_result = g_reset_fsm.bottle_reset_result;
            reset_data.sensor_status = bottle_sensor_is_working();

            uint8_t ret = tsdb_event_append(0x00F0, &reset_data, sizeof(reset_data));
            printf("[系统复位] TSDB事件已记录（type=0x00F0, ret=%u）\r\n", ret);
        }

        /* 11. 复位完成状态报告 */
        printf("[系统复位] ========================================\r\n");
        printf("[系统复位] 系统复位完成，现在处于待机模式\r\n");

        /* 报告留样瓶复位结果 */
        if (g_reset_fsm.bottle_reset_result == 1)
        {
            printf("[系统复位] 留样瓶复位：成功\r\n");
        }
        else if (g_reset_fsm.bottle_reset_result == 2)
        {
            printf("[系统复位] 留样瓶复位：失败（超时或传感器异常）\r\n");
            printf("[系统复位] 警告：请检查留样瓶传感器连接和机械结构\r\n");
        }

        /* 报告传感器状态 */
        if (bottle_sensor_is_working())
        {
            printf("[系统复位] 留样瓶传感器：正常\r\n");
        }
        else
        {
            printf("[系统复位] 留样瓶传感器：异常或缺失\r\n");
            printf("[系统复位] 建议：检查传感器连接或更换传感器\r\n");
        }

        printf("[系统复位] 系统已准备好进行维护操作\r\n");

        /* 12. 恢复屏幕分发器 */
        screen_dispatcher_resume();
        printf("[系统复位] 屏幕分发器已恢复，可以进行设置调整\r\n");

        g_reset_fsm.state = RESET_STATE_COMPLETE;
        return 0;

    case RESET_STATE_COMPLETE:
        g_reset_fsm.state = RESET_STATE_IDLE;

        // ★ 不要重置Task3初始化标志，防止系统自动启动调度器
        // 注释掉下面的调用，让系统保持待机状态
        // extern void task03_reset_init_flag(void);
        // task03_reset_init_flag();

        // 确保系统保持待机状态（复位后不应该自动运行）
        extern State g_State;
        g_State.State = 0;
        printf("[系统复位] 系统状态已设置为待机(状态=0)，不会自动启动调度器\r\n");

        // 复位完成后跳转到主页
        screen_send_notify(USART_SCREEN, home_page_buf, sizeof(home_page_buf), 3);
        printf("[系统复位] 已跳转到主页\r\n");

        return 1;

    default:
        g_reset_fsm.state = RESET_STATE_IDLE;
        return 1;
    }
}

/**
 * @brief 查询系统复位是否正在进行
 * @return 1=复位进行中, 0=空闲
 */
uint8_t system_reset_is_active(void)
{
    return (g_reset_fsm.state != RESET_STATE_IDLE) ? 1 : 0;
}

// void system_start_sequence(SystemStartMode start_mode)
//{
//     printf("[系统] 执行系统启动序列，启动模式=%d\r\n", start_mode);

//    // 保存启动模式到全局变量（可选）
//    // g_system_start_mode = start_mode;

//    // 延时100ms确保日志输出完成
//    vTaskDelay(pdMS_TO_TICKS(100));

//    // 执行单片机复位
//    printf("[系统] 即将重启单片机...\r\n");

//    // 禁用中断
//    __disable_irq();

//    // 等待一小段时间确保输出完成
//    for (volatile uint32_t i = 0; i < 100000; i++);

//    // 执行系统复位
//    NVIC_SystemReset();
//}

/**
 * @brief 系统启动序列函数
 * 仅在待机状态下响应，相当于重新首次开机
 * 功能：
 * 1. 检查系统状态，仅在待机状态下执行
 * 2. 清除中止标志
 * 3. 初始化系统为运行状态
 * 4. 进入调度器（预留接口）
 * 5. 记录TSDB
 */
void system_start_sequence(SystemStartMode start_mode)
{
    // 声明全局配置变量（统一管理）
    extern SampleConfig g_SampleConfig;
    extern SampleDeliveryIntervalConfig g_DeliveryConfig;
    extern RetainSampleModeConfig g_RetainSampleConfig;
    extern CommSettingConfig g_CommSettingConfig;
    extern SystemSettingConfig g_SystemSettingConfig;
    extern CalibrationParams_t g_CalibrationParams;
    extern RetainBottleState g_RetainBottleState;
    extern uint8_t g_current_bottle_number;
    extern void power_failure_detection_and_record(void);

    /* 0. 初始化sample_id生成器 */
    sample_id_generator_init();

    /* 0.1. 初始化缓存管理器 */
    cache_manager_init();

    /* 1. 断电检测和记录（优先执行，确保在状态检查前完成） */
    power_failure_detection_and_record();

    /* 1. 清除中止标志 */
    extern volatile uint8_t g_manual_operation_abort_flag;
    g_manual_operation_abort_flag = 0;

    /* 2. 恢复屏幕分发器 */
    extern void screen_dispatcher_resume(void);
    screen_dispatcher_resume();

    /* 3. 处理留样瓶位（根据启动模式） */
    if (start_mode == START_MODE_POWER_RECOVERY)
    {
        /* 断电恢复模式：仅加载瓶位状态，不执行瓶盘初始化 */
        /* ★ 瓶盘初始化已移至首次留样时执行（bottle_ensure_initialized） */
        cfg_load_retain_state(&g_RetainBottleState);
        g_current_bottle_number = g_RetainBottleState.currentBottle;
        if (g_current_bottle_number == 0 || g_current_bottle_number > 24)
        {
            g_current_bottle_number = 1;
        }
        printf("[系统启动] 断电恢复模式，加载瓶位=%d（瓶盘初始化延迟到首次留样）\r\n", g_current_bottle_number);
    }
    else
    {
        /* 手动启动模式：重新读取所有KVDB配置 */
        // 临时变量用于读取
        SampleConfig tmpS;
        SampleDeliveryIntervalConfig tmpD;
        RetainSampleModeConfig tmpR;
        CommSettingConfig tmpC;
        SystemSettingConfig tmpSys;
        CalibrationParams_t tmpCal;

        // 1. 读取采样配置
        if (cfg_load_sample(&tmpS))
        {
            g_SampleConfig = tmpS;
        }

        // 2. 读取送样配置
        if (cfg_load_delivery(&tmpD))
        {
            g_DeliveryConfig = tmpD;
        }

        // 3. 读取留样配置
        if (cfg_load_retain(&tmpR))
        {
            g_RetainSampleConfig = tmpR;
        }

        // 4. 读取留样瓶位状态
        if (cfg_load_retain_state(&g_RetainBottleState))
        {
            g_current_bottle_number = g_RetainBottleState.currentBottle;
        }

        // 5. 读取通讯配置
        if (cfg_load_comm(&tmpC))
        {
            g_CommSettingConfig = tmpC;
        }

        // 6. 读取系统配置
        if (cfg_load_system(&tmpSys))
        {
            g_SystemSettingConfig = tmpSys;
        }

        // 7. 读取校准参数
        if (cfg_load_calib(&tmpCal))
        {
            g_CalibrationParams = tmpCal;
        }

        // 验证瓶号有效性
        if (g_current_bottle_number == 0 || g_current_bottle_number > 24)
        {
            g_current_bottle_number = 1;
            g_RetainBottleState.currentBottle = 1;
            if (g_RetainBottleState.usedMask == 0)
            {
                g_RetainBottleState.usedMask = 0x00000001; // 只有1号瓶为空
            }
        }
    }

    /* 4. 初始化系统状态为运行状态 */
    g_State.State = 1; // 1 = 运行状态

    /* 4.1 手动启动：确保两个桶已排空并置位完成标志，避免首样等待排空标志 */
    if (start_mode == START_MODE_MANUAL)
    {
        uint32_t drain_dur = g_SampleConfig.BucketDrainTime ? g_SampleConfig.BucketDrainTime : 30u;
        uint32_t t0 = g_tmr2_seconds;

        // 同时启动A、B桶排空泵
        DrainARun;
        DrainBRun;
        g_State.DrainA = 1;
        g_State.DrainB = 1;
        g_State.ABucketState = 45; // 排空中
        g_State.BBucketState = 45; // 排空中

        // 等待排空完成
        while ((g_tmr2_seconds - t0) < drain_dur)
        {
            vTaskDelay(pdMS_TO_TICKS(200));
        }

        // 停止排空泵并复位状态
        DrainAStop;
        DrainBStop;
        g_State.DrainA = 0;
        g_State.DrainB = 0;
        g_State.SaveWarterA = 0;
        g_State.SaveWarterB = 0;
        g_State.ABucketState = 0; // 空闲
        g_State.BBucketState = 0; // 空闲
        g_State.DrainAComplete = 1;
        g_State.DrainBComplete = 1;
    }

    /* 5. 记录TSDB事件 */
    if (tsdb_is_ready())
    {
        struct
        {
            uint32_t timestamp;
            uint8_t start_mode; // 0x01 = 手动启动, 0x02 = 断电恢复
            uint8_t current_bottle;
            uint8_t target_bottle;
        } start_data;

        start_data.timestamp = rtc_counter_get();
        start_data.start_mode = (uint8_t)start_mode;
        start_data.current_bottle = g_current_bottle_number;
        start_data.target_bottle = g_RetainBottleState.currentBottle;

        tsdb_event_append(0x00F1, &start_data, sizeof(start_data));
    }

    /* 6. 系统启动序列完成（调度器启动由 Task3 统一负责） */
}

//==============================================================================
// 全局变量定义
//==============================================================================

// 采样序列计数器（按桶独立计数）
uint32_t g_sampling_sequence_A = 0; // A桶采样序列
uint32_t g_sampling_sequence_B = 0; // B桶采样序列

// 蓄水桶水样准备好标志（用于大岳协议40001寄存器）
uint8_t g_water_sample_ready_A = 0;
uint8_t g_water_sample_ready_B = 0;

// 全局状态机上下文已在第55-56行定义，此处删除重复定义

// ? 流量触发调度器状态
// ? 时间等比调度器状态
// 开关量触发调度器状态
// 开关量触发任务通知标志（用于task3接收开关量中断通知）
// task3句柄（用于开关量中断通知）
extern TaskHandle_t task3_handle;

// 定时采样调度器状态
//==============================================================================
// 通讯触发调度器全局变量
//==============================================================================

// 通讯触发请求变量
// 通讯触发调度器状态
// 最近一次送样记录（用于留样时确定对应的送样桶）
uint8_t g_last_delivery_bucket = 0xFF; // 0xFF=无效，表示还没有送样记录
uint32_t g_last_delivery_time = 0;     // 最近送样完成时间戳（秒，自1970年基准，与rtc_counter_get()一致）

//==============================================================================
// 日志记录函数实现
//==============================================================================

// 已删除：log_sampling_record() 和 log_delivery_record()
// 原因：旧的单记录函数，已被新的split record设计（LOG_SAMPLING_START/COMPLETE）替代
// 这两个函数没有被调用，是残留代码

/**
 * @brief 更新全局状态时间
 * 从RTC硬件读取当前时间并更新到g_State.Time数组
 * 调用位置：write_begin_page()（每次刷新主界面时调用）
 */
void update_global_state_time(void)
{
    // 读取RTC硬件时间到全局calendar变量
    rtc_time_get();

    // 更新g_State.Time数组（屏幕协议格式）
    // Time[0]: 年（year - 2000，范围0-255，对应2000-2255年）
    g_State.Time[0] = (uint8_t)(calendar.year >= 2000 ? calendar.year - 2000 : 0);
    // Time[1]: 月（1-12）
    g_State.Time[1] = calendar.month;
    // Time[2]: 日（1-31）
    g_State.Time[2] = calendar.date;
    // Time[3]: 时（0-23）
    g_State.Time[3] = calendar.hour;
    // Time[4]: 分（0-59）
    g_State.Time[4] = calendar.min;
    // Time[5]: 秒（0-59）
    g_State.Time[5] = calendar.sec;
}

//==============================================================================
// 新状态机架构：采样/送样状态机实现
//==============================================================================

// 外部全局变量声明
extern volatile uint32_t g_tmr3_milliseconds;
extern volatile uint8_t g_manual_operation_abort_flag;

//------------------------------------------------------------------------------
// 辅助函数
//------------------------------------------------------------------------------

// 参数夹持函数
static inline uint16_t _clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}


//------------------------------------------------------------------------------
// 水量变化记录
//------------------------------------------------------------------------------

void log_water_volume_change(uint8_t bucket_id, uint16_t water_level, const char *operation)
{
    if (!tsdb_is_ready())
        return;

    struct
    {
        uint8_t bucket_id;
        uint16_t water_level;
        uint32_t timestamp;
        char operation[16];
    } log_data = {0};

    log_data.bucket_id = bucket_id;
    log_data.water_level = water_level;
    log_data.timestamp = rtc_counter_get();

    if (operation)
    {
        strncpy(log_data.operation, operation, 15);
        log_data.operation[15] = '\0';
    }

    tsdb_event_append(LOG_WATER_CHANGE, &log_data, sizeof(log_data));
}

/**
 * @brief 记录手动操作影响到TSDB
 *
 * 用于记录手动操作对系统状态的影响，包括：
 * - 水量变化（增加/减少）
 * - 调度影响（跳过采样/送样/留样）
 * - 瓶位变化
 *
 * @param impact 手动操作影响结构体指针
 */
void log_manual_operation_impact(const ManualOperationImpact *impact)
{
    if (!impact || !tsdb_is_ready())
        return;

    // 使用事件类型 LOG_MANUAL_IMPACT (0x0051)
    tsdb_event_append(LOG_MANUAL_IMPACT, impact, sizeof(ManualOperationImpact));

    printf("[日志] 手动操作影响已记录：操作=%d, 水量变化(A=%+d, B=%+d)\r\n",
           impact->operation, impact->water_delta_a, impact->water_delta_b);
}

/**
 * @brief 记录周期任务跳过事件到TSDB
 *
 * 当周期调度的任务因某种原因被跳过时调用此函数记录。
 * 常见场景：
 * - 水量不足跳过送样
 * - 手动操作后跳过同类周期任务
 * - 采样未完成跳过送样
 *
 * @param task_type 任务类型: 1=采样, 2=送样, 3=留样
 * @param reason 跳过原因码
 * @param description 描述字符串（可选，最多31字符）
 */
void log_cycle_task_skipped(uint8_t task_type, uint8_t reason, const char *description)
{
    if (!tsdb_is_ready())
        return;

    struct
    {
        uint8_t task_type; // 1=采样, 2=送样, 3=留样
        uint8_t reason;    // 跳过原因码
        uint32_t timestamp;
        char description[32];
    } log_data = {0};

    log_data.task_type = task_type;
    log_data.reason = reason;
    log_data.timestamp = rtc_counter_get();

    if (description)
    {
        strncpy(log_data.description, description, 31);
        log_data.description[31] = '\0';
    }

    // 使用事件类型 LOG_CYCLE_SKIP (0x0052)
    tsdb_event_append(LOG_CYCLE_SKIP, &log_data, sizeof(log_data));

    const char *task_name[] = {"未知", "采样", "送样", "留样"};
    printf("[日志] 周期任务已跳过：%s (原因=%d, 描述=%s)\r\n",
           task_type <= 3 ? task_name[task_type] : task_name[0],
           reason,
           description ? description : "无");
}

//------------------------------------------------------------------------------
// 采样状态机实现
//------------------------------------------------------------------------------

// 启动采样模式和参数（临时全局变量，待结构体字段添加后移除）

// ? 流量触发任务通知标志（与task6通信）
// 前向声明
static uint8_t _sampling_step(SamplingContext *ctx);
static uint8_t _sampling_stage_pre_blowback(SamplingContext *ctx);
static uint8_t _sampling_stage_external_pump(SamplingContext *ctx);
static uint8_t _sampling_stage_tube_hold(SamplingContext *ctx);
static uint8_t _sampling_stage_measure(SamplingContext *ctx);
static uint8_t _sampling_stage_post_blowback(SamplingContext *ctx);
static uint8_t _sampling_stage_delay(SamplingContext *ctx, uint32_t delay_ms, SamplingStage next_stage);
static void _sampling_complete(SamplingContext *ctx);
static void _sampling_abort(SamplingContext *ctx);

// 流量触发周期采样函数声明
/**
 * @brief 启动采样流程
 * @param skip_bucket_state_check 1=跳过桶状态检查（用于首周期B桶首次采样），0=正常检查
 */
uint8_t sampling_start(uint8_t bucket_id, uint16_t target_volume, uint8_t is_manual, uint8_t skip_bucket_state_check)
{
    // ====================================================================
    // 1. 生成唯一sample_id并保存到桶上下文
    // ====================================================================
    char sample_id[18];
    if (!generate_sample_id(sample_id, sizeof(sample_id)))
    {
        printf("[采样启动] 错误：生成sample_id失败\r\n");
        return 0;
    }

    // 获取对应桶的上下文
    WaterSampleContext *ctx = (bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    // 保存sample_id到桶上下文
    strcpy(ctx->sample_id, sample_id);
    ctx->bucket_id = bucket_id;
    ctx->sampling_complete_time = 0;  // 采样尚未完成
    ctx->delivery_complete_time = 0;
    ctx->valid = 1;  // 标记为有效

    printf("[采样启动] 桶%c sample_id=%s\r\n", bucket_id ? 'B' : 'A', sample_id);

    // 清除水样准备好标志（新采样开始，水样不再是"准备好"状态）
    if (bucket_id == 0)
    {
        g_water_sample_ready_A = 0;
    }
    else
    {
        g_water_sample_ready_B = 0;
    }

    // ? 检查桶状态：不能在留样或排空时采样
    // 规则：采样与留样不能同时，采样需要等待排空完成
    // 注意：允许同桶边采边送（文档表格显示周期最后一次采样与送样时间很近或重叠）
    // ★ 特殊处理：首周期B桶首次采样跳过状态检查（B桶还未参与循环）
    if (!skip_bucket_state_check)
    {
        uint8_t target_bucket_state = bucket_id ? g_State.BBucketState : g_State.ABucketState;
        if (target_bucket_state == 20)
        {
            printf("[采样启动] 错误：桶冲突 - 目标桶正在留样（桶=%c，状态=%d）\r\n",
                   bucket_id ? 'B' : 'A', target_bucket_state);
            return 0;
        }
        if (target_bucket_state == 42)
        {
            printf("[采样启动] 错误：桶冲突 - 目标桶正在排空（桶=%c，状态=%d）\r\n",
                   bucket_id ? 'B' : 'A', target_bucket_state);
            return 0;
        }
    }
    else
    {
        rtc_time_get();
        printf("[%02d:%02d:%02d][采样启动] 跳过桶状态检查（首周期B桶首次采样）\r\n",
               calendar.hour, calendar.min, calendar.sec);
    }

    // 计算采样时长
    uint16_t measure_time = calc_sampling_time_by_volume(target_volume);
    if (measure_time == 0)
    {
        printf("[采样启动] 错误：按体积计算采样时间失败\r\n");
        return 0;
    }

    // 参数快照（从全局配置读取并夹持）
    g_sampling_ctx.bucket_id = bucket_id;
    g_sampling_ctx.target_volume = target_volume;
    g_sampling_ctx.is_manual = is_manual;
    if (bucket_id == 0)
    {
        g_State.ABucketState = 8;
        g_State.CurrentBucketRunState = 8;
        g_State.ABucketCountDown[0] = 0;
        g_State.ABucketCountDown[1] = 0;
        g_State.ABucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
        // 重置B桶计时        g_State.BBucketCountDown[0] = 0;  // 时        g_State.BBucketCountDown[1] = 0;  // 分        g_State.BBucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
        // 重置A桶计时        g_State.ABucketCountDown[0] = 0;  // 时        g_State.ABucketCountDown[1] = 0;  // 分        g_State.ABucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
    }
    else
    {
        g_State.BBucketState = 8;
        g_State.CurrentBucketRunState = 8;
        g_State.BBucketCountDown[0] = 0;
        g_State.BBucketCountDown[1] = 0;
        g_State.BBucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
        // 重置B桶计时        g_State.BBucketCountDown[0] = 0;  // 时        g_State.BBucketCountDown[1] = 0;  // 分        g_State.BBucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
        // 重置A桶计时        g_State.ABucketCountDown[0] = 0;  // 时        g_State.ABucketCountDown[1] = 0;  // 分        g_State.ABucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
    }
    g_sampling_ctx.blowback_time = _clamp_u16(g_SampleConfig.BlowbackTime, 0, 36000);
    g_sampling_ctx.improve_time = _clamp_u16(g_SampleConfig.SamplingImproveTime, 0, 36000);
    g_sampling_ctx.tube_hold_time = _clamp_u16(g_SampleConfig.TubeHoldTime, 0, 36000);
    g_sampling_ctx.measure_time = measure_time;
    g_sampling_ctx.rpm = g_SystemSettingConfig.Motorspeed;

    // 初始化运行时状态
    g_sampling_ctx.stage = SAMP_PRE_BLOWBACK;
    g_sampling_ctx.stage_start_time = 0;
    g_sampling_ctx.delay_start_ms = 0;
    g_sampling_ctx.total_start_time = rtc_counter_get(); // 统一使用1970年基准时间戳
    g_sampling_ctx.result = 0;
    g_sampling_ctx.error_code = 0;

    // 设置进水阀到目标桶
    if (bucket_id == 0)
    {
        InletThreeWayValveA;
        g_State.InletThreeWayValve = 1;
    }
    else
    {
        InletThreeWayValveB;
        g_State.InletThreeWayValve = 2;
    }

    // 设置延时状态（非阻塞方式），等待阀门到位
    g_sampling_ctx.stage = SAMP_DELAY_VALVE_SETUP;
    g_sampling_ctx.delay_start_ms = g_tmr3_milliseconds;
    printf("[采样启动] 等待进水阀动作到位（非阻塞，10秒）...\r\n");

    // ====================================================================
    // 2. 写入采样开始记录到TSDB（新split模式）
    // ====================================================================
    // 递增采样序号
    if (bucket_id == 0)
    {
        g_sampling_sequence_A++;
    }
    else
    {
        g_sampling_sequence_B++;
    }

    // 构造采样开始记录
    SamplingStartRecord start_record = {0};
    strcpy(start_record.sample_id, sample_id);
    start_record.sampling_mode = g_SampleConfig.SamplingMode;  // 1-时间等比 2-流量 3-开关 4-直接
    start_record.bucket_id = bucket_id;
    start_record.start_time = rtc_counter_get();
    start_record.sequence = (bucket_id == 0) ? g_sampling_sequence_A : g_sampling_sequence_B;
    start_record.target_volume = target_volume;
    start_record.is_manual = is_manual;

    // 写入TSDB
    if (tsdb_is_ready())
    {
        tsdb_event_append(LOG_SAMPLING_START, &start_record, sizeof(SamplingStartRecord));
        printf("[采样TSDB] 写入开始记录：sample_id=%s, 序号=%u, 目标量=%u ml\r\n",
               sample_id, start_record.sequence, target_volume);
    }

    printf("[采样启动] 已启动：桶=%c, 体积=%d ml, 时间=%d s, 手动=%d\r\n",
           bucket_id ? 'B' : 'A', target_volume, measure_time, is_manual);

    return 1;
}

/**
 * @brief 足量采样：在一次完整采样流程中采满本周期总量（反吹/提升/管存/计量仅各一次）
 *        例如：单次500ml、4次 → 足量采样为一次2000ml计量
 * @param bucket_id  0=A桶, 1=B桶
 * @param is_manual  0=调度触发, 1=手动
 * @return 1=已启动, 0=失败
 */
uint8_t sampling_start_full_volume(uint8_t bucket_id, uint8_t is_manual)
{
    // 基于当前配置计算本周期应采集的总量
    uint16_t per_sample_ml = g_SampleConfig.SampleVolume;
    uint16_t interval_min = g_SampleConfig.SampleInterval;
    uint16_t cycle_min = g_SampleConfig.CycleTime;
    if (per_sample_ml == 0 || interval_min == 0 || cycle_min == 0)
    {
        printf("[足量采样] 错误：配置无效（体积=%u, 间隔=%u, 周期=%u）\r\n",
               per_sample_ml, interval_min, cycle_min);
        return 0;
    }
    uint8_t count = (uint8_t)(cycle_min / interval_min);
    if (count == 0)
        count = 1;
    uint32_t total_ml = (uint32_t)per_sample_ml * (uint32_t)count;
    if (total_ml > 65535u)
        total_ml = 65535u; // 体积上限保护

    printf("[足量采样] 总量=%lu ml (每次=%u ml × %u)\r\n",
           (unsigned long)total_ml, per_sample_ml, count);

    // 复用标准采样流程：只执行一次完整流程（反吹等动作仅一次）
    return sampling_start(bucket_id, (uint16_t)total_ml, is_manual, 0);
}

/**
 * @brief 主状态机推进函数
 */
static uint8_t _sampling_step(SamplingContext *ctx)
{
    if (!ctx)
        return 3;

    // 统一中止检查
    if (g_manual_operation_abort_flag)
    {
        printf("[采样步骤] 检测到中止信号\r\n");
        _sampling_abort(ctx);
        return 2; // 中止
    }

    // 状态机分发
    switch (ctx->stage)
    {
    case SAMP_IDLE:
        return 0;

    case SAMP_PRE_BLOWBACK:
        return _sampling_stage_pre_blowback(ctx);

    case SAMP_DELAY_500MS_AFTER_PRE_BLOW:
        return _sampling_stage_delay(ctx, 1000, SAMP_EXTERNAL_PUMP); // 已优化：从500ms改为200ms（反转→正转保护）

    case SAMP_EXTERNAL_PUMP:
        return _sampling_stage_external_pump(ctx);

    case SAMP_TUBE_HOLD:
        return _sampling_stage_tube_hold(ctx);

    case SAMP_DELAY_200MS_AFTER_TUBE_HOLD:
        return _sampling_stage_delay(ctx, 200, SAMP_MEASURE);

    case SAMP_MEASURE:
        return _sampling_stage_measure(ctx);

    case SAMP_DELAY_500MS_AFTER_MEASURE:
        // 延时500ms，防止电机突然换向（正转→反转）
        return _sampling_stage_delay(ctx, 500, SAMP_POST_BLOWBACK);

    case SAMP_DELAY_VALVE_SETUP:
        // 等待阀门到位（10秒）
        return _sampling_stage_delay(ctx, 10000, SAMP_PRE_BLOWBACK);

    case SAMP_POST_BLOWBACK:
        return _sampling_stage_post_blowback(ctx);

    case SAMP_COMPLETED:
        return 1; // 完成

    case SAMP_ABORTED:
        return 2; // 中止

    default:
        return 3; // 错误
    }
}

/**
 * @brief 阶段1：前反吹
 */
static uint8_t _sampling_stage_pre_blowback(SamplingContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动采样蠕动泵反转
        MotorRun(1, 0, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.SamplingMotor = 1;
        g_State.CurrentBucketRunState = 1; // 反吹
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 1;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 1;
        //        }

        printf("[采样] 前反吹阶段已开始：%u 秒\r\n", ctx->blowback_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->blowback_time)
    {
        // 时间到
        MotorStop(1);
        g_State.SamplingMotor = 0;

        printf("[采样] 前反吹阶段已完成：%lu 秒\r\n", elapsed);

        // 转到延时状态 (200ms保护，已优化)
        ctx->stage = SAMP_DELAY_500MS_AFTER_PRE_BLOW;
        ctx->stage_start_time = 0;
        ctx->delay_start_ms = g_tmr3_milliseconds;

        return 0;
    }

    // 每200ms重发命令（保持电机运行）
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(1, 0, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 延时状态通用处理
 */
static uint8_t _sampling_stage_delay(SamplingContext *ctx, uint32_t delay_ms, SamplingStage next_stage)
{
    if (ctx->delay_start_ms == 0)
    {
        ctx->delay_start_ms = g_tmr3_milliseconds;
        return 0;
    }

    uint32_t elapsed = g_tmr3_milliseconds - ctx->delay_start_ms;

    // 处理计时器溢出 (约49天一次)
    if (elapsed > 0x80000000UL)
    {
        elapsed = g_tmr3_milliseconds + (0xFFFFFFFFUL - ctx->delay_start_ms);
    }

    if (elapsed >= delay_ms)
    {
        ctx->stage = next_stage;
        ctx->delay_start_ms = 0;
        printf("[采样] 延时 %lu 毫秒已完成，下一阶段=%d\r\n", delay_ms, next_stage);
    }

    return 0;
}

/**
 * @brief 阶段2：外接泵提升
 */
static uint8_t _sampling_stage_external_pump(SamplingContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动外接泵
        ExternalPumpRun;
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.CurrentBucketRunState = 2; // 提升
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 2;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 2;
        //        }

        printf("[采样] 外接泵阶段已开始：%u 秒\r\n", ctx->improve_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->improve_time)
    {
        // 时间到（外接泵继续运行）
        printf("[采样] 外接泵阶段已完成：%lu 秒\r\n", elapsed);

        // 直接转到管存阶段
        ctx->stage = SAMP_TUBE_HOLD;
        ctx->stage_start_time = 0;

        return 0;
    }

    return 0;
}

/**
 * @brief 阶段3：管存
 */
static uint8_t _sampling_stage_tube_hold(SamplingContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动采样蠕动泵正转
        MotorRun(1, 1, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.SamplingMotor = 1;
        g_State.CurrentBucketRunState = 3; // 管存
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 3;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 3;
        //        }

        printf("[采样] 管存阶段已开始：%u 秒\r\n", ctx->tube_hold_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->tube_hold_time)
    {
        // 时间到
        MotorStop(1);
        g_State.SamplingMotor = 0;

        printf("[采样] 管存阶段已完成：%lu 秒\r\n", elapsed);

        // 转到延时状态 (200ms保护)
        ctx->stage = SAMP_DELAY_200MS_AFTER_TUBE_HOLD;
        ctx->stage_start_time = 0;
        ctx->delay_start_ms = g_tmr3_milliseconds;

        return 0;
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(1, 1, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 阶段4：计量采样
 */
static uint8_t _sampling_stage_measure(SamplingContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动采样蠕动泵正转
        MotorRun(1, 1, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.SamplingMotor = 1;
        g_State.CurrentBucketRunState = 8; // 采样
        // 重置B桶计时        g_State.BBucketCountDown[0] = 0;  // 时        g_State.BBucketCountDown[1] = 0;  // 分        g_State.BBucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
        // 重置A桶计时        g_State.ABucketCountDown[0] = 0;  // 时        g_State.ABucketCountDown[1] = 0;  // 分        g_State.ABucketCountDown[2] = 0;  // 秒        // 同步当前桶计时        g_State.CurrentBucketCountDown[0] = 0;        g_State.CurrentBucketCountDown[1] = 0;        g_State.CurrentBucketCountDown[2] = 0;
        if (ctx->bucket_id == 0)
        {
            g_State.ABucketState = 8;
        }
        else
        {
            g_State.BBucketState = 8;
        }

        printf("[采样] 计量阶段已开始：%u 秒, %u ml\r\n", ctx->measure_time, ctx->target_volume);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->measure_time)
    {
        // 时间到
        MotorStop(1);
        g_State.SamplingMotor = 0;

        // 停止外接泵 - 在计量完成后立即停止
        ExternalPumpStop;
        printf("[采样] 计量完成，停止外接泵\r\n");

        printf("[采样] 计量阶段已完成：%lu 秒\r\n", elapsed);

        // 转到延时状态，防止电机突然换向（正转→反转）
        ctx->stage = SAMP_DELAY_500MS_AFTER_MEASURE;
        ctx->stage_start_time = 0;
        ctx->delay_start_ms = g_tmr3_milliseconds;

        return 0;
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(1, 1, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 阶段5：后反吹
 */
static uint8_t _sampling_stage_post_blowback(SamplingContext *ctx)
{
    // 第一次进入（延时完成后）
    if (ctx->stage_start_time == 0)
    {
        // 启动采样蠕动泵反转
        MotorRun(1, 0, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.SamplingMotor = 1;
        g_State.CurrentBucketRunState = 5; // 后反吹
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 5;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 5;
        //        }

        printf("[采样] 后反吹阶段已开始：%u 秒\r\n", ctx->blowback_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->blowback_time)
    {
        // 时间到
        MotorStop(1);
        g_State.SamplingMotor = 0;

        printf("[采样] 后反吹阶段已完成：%lu 秒\r\n", elapsed);

        // 转到完成状态
        ctx->stage = SAMP_COMPLETED;
        _sampling_complete(ctx);

        return 1; // 返回完成
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(1, 0, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 完成处理
 */
static void _sampling_complete(SamplingContext *ctx)
{
    printf("[采样] 正在完成采样流程...\r\n");

    // 更新桶内水量
    if (ctx->bucket_id == 0)
    {
        g_State.SaveWarterA += ctx->target_volume;
        g_State.SamplingIntervalCountDown[0] = 0;  // 时
        g_State.SamplingIntervalCountDown[1] = 0;  // 分
        g_State.SamplingIntervalCountDown[2] = 0;  // 秒
        printf("[采样] A桶已更新：%d ml (+%d)\r\n", g_State.SaveWarterA, ctx->target_volume);
    }
    else
    {
        g_State.SaveWarterB += ctx->target_volume;
        g_State.SamplingIntervalCountDown[0] = 0;  // 时
        g_State.SamplingIntervalCountDown[1] = 0;  // 分
        g_State.SamplingIntervalCountDown[2] = 0;  // 秒
        printf("[采样] B桶已更新：%d ml (+%d)\r\n", g_State.SaveWarterB, ctx->target_volume);
    }

    // 启动阶段：不从完成时刻重置下一次触发，严格按预分配表推进

    // 按桶独立递增采样序列计数器
    uint32_t *p_sequence = (ctx->bucket_id == 0) ? &g_sampling_sequence_A : &g_sampling_sequence_B;
    (*p_sequence)++;

    // 计算总采样次数（一个周期内）
    uint8_t total_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
    if (total_count == 0)
        total_count = 1;
    if (total_count > 24)
        total_count = 24;

    // 格式化采样序列：高字节=当前次数，低字节=总次数
    // 例如：第5次/共8次 = 0x0508
    uint8_t current_count = (uint8_t)(*p_sequence);
    if (current_count > total_count)
        current_count = total_count; // 防止溢出
    uint16_t seq_formatted = ((uint16_t)current_count << 8) | total_count;

    printf("[采样] %c桶采样序列: %d/%d (0x%04X)\r\n",
           ctx->bucket_id ? 'B' : 'A', current_count, total_count, seq_formatted);

    // 最后一次采样完成后重置计数器并设置水样准备好标志
    if (current_count >= total_count)
    {
        *p_sequence = 0;
        // 设置水样准备好标志（用于大岳协议40001寄存器）
        if (ctx->bucket_id == 0)
        {
            g_water_sample_ready_A = 1;
        }
        else
        {
            g_water_sample_ready_B = 1;
        }
        printf("[采样] %c桶周期完成，采样序列已重置，水样已准备好\r\n", ctx->bucket_id ? 'B' : 'A');
    }

    // ====================================================================
    // 写入采样完成记录到TSDB（新split模式）
    // ====================================================================
    // 从桶上下文继承sample_id
    WaterSampleContext *water_ctx = (ctx->bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    if (!water_ctx->valid)
    {
        printf("[采样完成] 警告：桶%c的sample_id无效，无法写入完成记录\r\n", ctx->bucket_id ? 'B' : 'A');
        // sample_id无效时，使用当前桶水量记录
        log_water_volume_change(ctx->bucket_id, ctx->bucket_id ? g_State.SaveWarterB : g_State.SaveWarterA, "SAMPLING");
    }
    else
    {
        // 构造采样完成记录
        SamplingCompleteRecord complete_record = {0};
        strcpy(complete_record.sample_id, water_ctx->sample_id);
        complete_record.end_time = rtc_counter_get();
        complete_record.actual_volume = ctx->target_volume;
        complete_record.water_level = ctx->bucket_id ? g_State.SaveWarterB : g_State.SaveWarterA;
        complete_record.result = 1;  // 成功
        complete_record.error_code = 0;

        // 写入TSDB
        if (tsdb_is_ready())
        {
            tsdb_event_append(LOG_SAMPLING_COMPLETE, &complete_record, sizeof(SamplingCompleteRecord));
            printf("[采样TSDB] 写入完成记录：sample_id=%s, 实际量=%u ml, 水位=%u ml\r\n",
                   water_ctx->sample_id, complete_record.actual_volume, complete_record.water_level);
        }

        // 更新桶上下文的采样完成时间
        water_ctx->sampling_complete_time = complete_record.end_time;

        // 重新构造start_record（用于缓存添加）
        SamplingStartRecord start_record = {0};
        strcpy(start_record.sample_id, water_ctx->sample_id);
        start_record.sampling_mode = g_SampleConfig.SamplingMode;
        start_record.bucket_id = ctx->bucket_id;
        start_record.start_time = ctx->total_start_time;
        start_record.sequence = (ctx->bucket_id == 0) ? g_sampling_sequence_A : g_sampling_sequence_B;
        start_record.target_volume = ctx->target_volume;
        start_record.is_manual = ctx->is_manual;

        // 添加到缓存
        cache_add_sampling(&start_record, &complete_record);

        // 记录水量变化（在complete_record作用域内）
        log_water_volume_change(ctx->bucket_id, complete_record.water_level, "SAMPLING");
    }

    // 更新西安协议采样日志
    extern XianSamplingLog_t g_XianSamplingLog;
    RtcDateTimeComponents dt_xian;
    rtc_seconds_to_datetime(unix_to_2000(rtc_counter_get()), &dt_xian);

    g_XianSamplingLog.mode = ctx->is_manual ? 99 : g_SampleConfig.SamplingMode;
    g_XianSamplingLog.bucketId = (ctx->bucket_id == 0) ? 1 : 2; // 1=A桶, 2=B桶
    g_XianSamplingLog.year = dt_xian.year;
    g_XianSamplingLog.month = dt_xian.month;
    g_XianSamplingLog.day = dt_xian.day;
    g_XianSamplingLog.hour = dt_xian.hour;
    g_XianSamplingLog.minute = dt_xian.minute;
    g_XianSamplingLog.sequence = seq_formatted; // 使用格式化的序列号
    g_XianSamplingLog.volume = ctx->target_volume;
    g_XianSamplingLog.result = 1; // 1=成功

    // 更新屏幕状态
    g_State.CurrentBucketRunState = 0;
    if (ctx->bucket_id == 0)
    {
        g_State.ABucketState = 0;
    }
    else
    {
        g_State.BBucketState = 0;
    }

    // 更新上下文
    ctx->result = 1;
    ctx->stage = SAMP_IDLE;

    printf("[采样] 采样成功完成\r\n");
}

/**
 * @brief 中止处理
 */
static void _sampling_abort(SamplingContext *ctx)
{
    printf("[采样] 正在中止采样流程...\r\n");

    // 停止所有电机
    MotorStop(1);
    ExternalPumpStop;

    g_State.SamplingMotor = 0;
    g_State.CurrentBucketRunState = 0;
    if (ctx->bucket_id == 0)
    {
        g_State.ABucketState = 0;
    }
    else
    {
        g_State.BBucketState = 0;
    }

    // ====================================================================
    // 写入采样中止记录到TSDB（新split模式）
    // ====================================================================
    WaterSampleContext *water_ctx = (ctx->bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    if (water_ctx->valid)
    {
        // 构造采样完成记录（result=2表示中止）
        SamplingCompleteRecord complete_record = {0};
        strcpy(complete_record.sample_id, water_ctx->sample_id);
        complete_record.end_time = rtc_counter_get();
        complete_record.actual_volume = 0;  // 中止时无实际采样量
        complete_record.water_level = ctx->bucket_id ? g_State.SaveWarterB : g_State.SaveWarterA;
        complete_record.result = 2;  // 中止
        complete_record.error_code = (uint8_t)ctx->stage;  // 记录在哪个阶段中止

        // 写入TSDB
        if (tsdb_is_ready())
        {
            tsdb_event_append(LOG_SAMPLING_COMPLETE, &complete_record, sizeof(SamplingCompleteRecord));
            printf("[采样TSDB] 写入中止记录：sample_id=%s, 中止阶段=%u\r\n",
                   water_ctx->sample_id, complete_record.error_code);
        }
    }

    // 更新上下文
    ctx->result = 2;
    ctx->stage = SAMP_IDLE;

    printf("[采样] 采样已中止\r\n");
}

/**
 * @brief 推进状态机（task3调用）
 */
void sampling_step_if_active(void)
{
    if (g_sampling_ctx.stage == SAMP_IDLE ||
            g_sampling_ctx.stage == SAMP_COMPLETED ||
            g_sampling_ctx.stage == SAMP_ABORTED)
    {
        return; // 空闲状态，无需推进
    }

    _sampling_step(&g_sampling_ctx);
}

/**
 * @brief 查询采样状态
 */
uint8_t sampling_get_status(void)
{
    if (g_sampling_ctx.stage == SAMP_IDLE)
    {
        return 0; // 空闲
    }
    else if (g_sampling_ctx.stage == SAMP_COMPLETED)
    {
        return g_sampling_ctx.result == 1 ? 2 : 3; // 2=成功, 3=失败
    }
    else if (g_sampling_ctx.stage == SAMP_ABORTED)
    {
        return 4; // 中止
    }
    else
    {
        return 1; // 运行中
    }
}

/**
 * @brief 获取采样结果
 */
uint8_t sampling_get_result(uint8_t *result, uint8_t *error_code)
{
    if (g_sampling_ctx.stage == SAMP_COMPLETED || g_sampling_ctx.stage == SAMP_ABORTED)
    {
        if (result)
            *result = g_sampling_ctx.result;
        if (error_code)
            *error_code = g_sampling_ctx.error_code;
        return 1; // 有效结果
    }
    return 0; // 还在运行
}

/**
 * @brief 等待采样完成（测试函数使用）
 */
uint8_t sampling_wait(uint32_t timeout_ms)
{
    uint32_t start_tick = xTaskGetTickCount();

    while (1)
    {
        uint8_t status = sampling_get_status();

        if (status == 2)
        {
            return 1; // 成功
        }
        else if (status == 3 || status == 4)
        {
            return 0; // 失败或中止
        }

        // 检查超时
        if (timeout_ms > 0)
        {
            uint32_t elapsed = xTaskGetTickCount() - start_tick;
            if (elapsed >= pdMS_TO_TICKS(timeout_ms))
            {
                printf("[采样等待] 超时\r\n");
                return 0;
            }
        }

        // 继续推进状态机
        sampling_step_if_active();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
//------------------------------------------------------------------------------
// 送样状态机实现
//------------------------------------------------------------------------------

// 前向声明
static uint8_t _delivery_step(DeliveryContext *ctx);
static uint8_t _delivery_stage_pre_blowback(DeliveryContext *ctx);
static uint8_t _delivery_stage_stabilize(DeliveryContext *ctx);
static uint8_t _delivery_stage_start_mix(DeliveryContext *ctx);
static uint8_t _delivery_stage_measure(DeliveryContext *ctx);
static uint8_t _delivery_stage_backdraw(DeliveryContext *ctx);
static uint8_t _delivery_stage_delay(DeliveryContext *ctx, uint32_t delay_ms, DeliveryStage next_stage);
static void _delivery_complete(DeliveryContext *ctx);
static void _delivery_abort(DeliveryContext *ctx);

// 注意：calc_delivery_time_by_volume 已在 work.h 中声明并在 work.c 中实现

/**
 * @brief 启动送样流程
 */
uint8_t delivery_start(uint8_t bucket_id, uint16_t target_volume, uint8_t is_manual)
{
    // 检查水量充足性
    uint16_t current_water = (bucket_id == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;

    // 通讯触发模式(SamplingMode==2)下跳过水量检查，收到送样指令直接执行
    if (g_SampleConfig.SamplingMode == 2)
    {
        printf("[送样启动] 通讯触发模式，跳过水量检查，当前水量=%d ml\r\n", current_water);
        // 如果目标体积为0或大于当前水量，使用当前水量
        if (target_volume == 0 || target_volume > current_water)
        {
            target_volume = current_water;
        }
    }
    else if (current_water < target_volume)
    {
        if (is_manual)
        {
            // 测试模式: 返回特殊码，由测试函数嵌套采样
            printf("[送样启动] 手动模式水量不足（%d < %d）\r\n",
                   current_water, target_volume);
            return 2; // 特殊码: 需要先采样
        }
        else
        {
            // 周期模式: 水量不足是故障
            printf("[送样启动] 错误：周期模式水量不足（%d < %d）\r\n",
                   current_water, target_volume);
            return 0; // 失败
        }
    }

    // 水量充足，继续初始化
    g_delivery_ctx.bucket_id = bucket_id;
    g_delivery_ctx.target_volume = target_volume;
    g_delivery_ctx.is_manual = is_manual;
    // 状态
    if (bucket_id == 0)
    {
        g_State.ABucketState = 19;
        g_State.CurrentBucketRunState = 19;
        g_State.ABucketCountDown[0] = 0;
        g_State.ABucketCountDown[1] = 0;
        g_State.ABucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
        g_State.SamplingTotalTimeCountDown[0] = 0;  // 时
        g_State.SamplingTotalTimeCountDown[1] = 0;  // 分
        g_State.SamplingTotalTimeCountDown[2] = 0;  // 秒

    }
    else
    {
        g_State.BBucketState = 19;
        g_State.CurrentBucketRunState = 19;
        g_State.BBucketCountDown[0] = 0;
        g_State.BBucketCountDown[1] = 0;
        g_State.BBucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
        g_State.SamplingTotalTimeCountDown[0] = 0;  // 时
        g_State.SamplingTotalTimeCountDown[1] = 0;  // 分
        g_State.SamplingTotalTimeCountDown[2] = 0;  // 秒
    }

    // 参数快照
    g_delivery_ctx.blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000);
    g_delivery_ctx.deliver_time = g_DeliveryConfig.Duration; // 直接使用配置的送样时长（秒）
    g_delivery_ctx.backdraw_time = _clamp_u16(g_DeliveryConfig.Interval, 0, 36000);
    g_delivery_ctx.rpm = g_SystemSettingConfig.Motorspeed;

    // 初始化状态
    g_delivery_ctx.stage = DELIV_PRE_BLOWBACK;
    g_delivery_ctx.stage_start_time = 0;
    g_delivery_ctx.delay_start_ms = 0;
    g_delivery_ctx.total_start_time = rtc_counter_get(); // 统一使用1970年基准时间戳
    g_delivery_ctx.actual_volume = target_volume;
    g_delivery_ctx.result = 0;

    // 设置出水阀
    if (bucket_id == 0)
    {
        OutletThreeWayValveA();
        g_State.OutletThreeWayValve = 0;
    }
    else
    {
        OutletThreeWayValveB();
        g_State.OutletThreeWayValve = 1;
    }
    SampleThreeWayValveSample;
    g_State.SampleThreeWayValve = 0;

    // 设置延时状态（非阻塞方式），等待阀门到位
    g_delivery_ctx.stage = DELIV_DELAY_VALVE_SETUP;
    g_delivery_ctx.delay_start_ms = g_tmr3_milliseconds;
    printf("[送样启动] 等待出水阀和采样阀动作到位（非阻塞，10秒）...\r\n");

    // ====================================================================
    // 写入送样开始记录到TSDB（新split模式，继承sample_id）
    // ====================================================================
    WaterSampleContext *water_ctx = (bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    if (!water_ctx->valid)
    {
        printf("[送样启动] 警告：桶%c的sample_id无效，无法继承到送样记录\r\n", bucket_id ? 'B' : 'A');
    }
    else
    {
        // 构造送样开始记录（继承sample_id）
        DeliveryStartRecord start_record = {0};
        strcpy(start_record.sample_id, water_ctx->sample_id);
        start_record.delivery_mode = is_manual ? 2 : 1;  // 1-定时 2-手动
        start_record.bucket_id = bucket_id;
        start_record.start_time = rtc_counter_get();
        start_record.target_volume = target_volume;
        start_record.is_manual = is_manual;

        // 写入TSDB
        if (tsdb_is_ready())
        {
            tsdb_event_append(LOG_DELIVERY_START, &start_record, sizeof(DeliveryStartRecord));
            printf("[送样TSDB] 写入开始记录：sample_id=%s, 目标量=%u ml\r\n",
                   water_ctx->sample_id, target_volume);
        }
    }

    printf("[送样启动] 已启动：桶=%c, 体积=%d ml\r\n", bucket_id ? 'B' : 'A', target_volume);

    return 1;
}

/**
 * @brief 主状态机推进函数
 */
static uint8_t _delivery_step(DeliveryContext *ctx)
{
    if (!ctx)
        return 3;

    // 统一中止检查
    if (g_manual_operation_abort_flag)
    {
        printf("[送样步骤] 检测到中止信号\r\n");
        _delivery_abort(ctx);
        return 2; // 中止
    }

    // 状态机分发
    switch (ctx->stage)
    {
    case DELIV_IDLE:
        return 0;

    case DELIV_PRE_BLOWBACK:
        return _delivery_stage_pre_blowback(ctx);

    case DELIV_DELAY_500MS_AFTER_PRE_BLOW:
        return _delivery_stage_delay(ctx, 500, DELIV_STABILIZE);

    case DELIV_STABILIZE:
        return _delivery_stage_stabilize(ctx);

    case DELIV_START_MIX:
        return _delivery_stage_start_mix(ctx);

    case DELIV_MEASURE:
        return _delivery_stage_measure(ctx);

    case DELIV_DELAY_500MS_AFTER_MEASURE:
        return _delivery_stage_delay(ctx, 500, DELIV_BACKDRAW);

    case DELIV_DELAY_VALVE_SETUP:
        // 等待阀门到位（10秒）
        return _delivery_stage_delay(ctx, 10000, DELIV_PRE_BLOWBACK);

    case DELIV_BACKDRAW:
        return _delivery_stage_backdraw(ctx);

    case DELIV_COMPLETED:
        return 1; // 完成

    case DELIV_ABORTED:
        return 2; // 中止

    default:
        return 3; // 错误
    }
}

/**
 * @brief 阶段1：反吹清线
 */
static uint8_t _delivery_stage_pre_blowback(DeliveryContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动送样蠕动泵反转
        MotorRun(2, 0, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.DeliveryMotor = 1;
        g_State.CurrentBucketRunState = 6; // 反吹
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 6;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 6;
        //        }

        printf("[送样] 前反吹阶段已开始：%u 秒\r\n", ctx->blowback_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->blowback_time)
    {
        // 时间到
        MotorStop(2);
        g_State.DeliveryMotor = 0;

        printf("[送样] 前反吹阶段已完成：%lu 秒\r\n", elapsed);

        // 关键时间点：送样反吹结束，启动数采仪触发信号
        TriggerRun; // 通知数采仪开始采集数据

        printf("[送样] TriggerRun信号已激活（20秒后将停止）\r\n");

        // 转到延时状态
        ctx->stage = DELIV_DELAY_500MS_AFTER_PRE_BLOW;
        ctx->stage_start_time = 0;
        ctx->delay_start_ms = g_tmr3_milliseconds;

        return 0;
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(2, 0, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 延时状态通用处理
 */
static uint8_t _delivery_stage_delay(DeliveryContext *ctx, uint32_t delay_ms, DeliveryStage next_stage)
{
    if (ctx->delay_start_ms == 0)
    {
        ctx->delay_start_ms = g_tmr3_milliseconds;
        return 0;
    }

    uint32_t elapsed = g_tmr3_milliseconds - ctx->delay_start_ms;

    // 处理计时器溢出
    if (elapsed > 0x80000000UL)
    {
        elapsed = g_tmr3_milliseconds + (0xFFFFFFFFUL - ctx->delay_start_ms);
    }

    if (elapsed >= delay_ms)
    {
        ctx->stage = next_stage;
        ctx->delay_start_ms = 0;
        printf("[送样] 延时 %lu 毫秒已完成，下一阶段=%d\r\n", delay_ms, next_stage);
    }

    return 0;
}

/**
 * @brief 阶段2：稳定等待
 */
static uint8_t _delivery_stage_stabilize(DeliveryContext *ctx)
{
    if (ctx->delay_start_ms == 0)
    {
        ctx->delay_start_ms = g_tmr3_milliseconds;
        printf("[送样] 稳定阶段已开始：2000 毫秒\r\n");
        return 0;
    }

    uint32_t elapsed = g_tmr3_milliseconds - ctx->delay_start_ms;
    if (elapsed > 0x80000000UL)
    {
        elapsed = g_tmr3_milliseconds + (0xFFFFFFFFUL - ctx->delay_start_ms);
    }

    if (elapsed >= 2000)
    {
        ctx->stage = DELIV_START_MIX;
        ctx->delay_start_ms = 0;
        printf("[送样] 稳定阶段已完成\r\n");
    }

    return 0;
}

/**
 * @brief 阶段3：启动混样
 */
static uint8_t _delivery_stage_start_mix(DeliveryContext *ctx)
{
    // 启动混样电机
    if (ctx->bucket_id == 0)
    {
        MixARun;
    }
    else
    {
        MixBRun;
    }

    printf("[送样] 启动混样阶段：搅拌器已启动\r\n");

    // 立即转到计量阶段
    ctx->stage = DELIV_MEASURE;
    ctx->stage_start_time = 0;

    return 0;
}

/**
 * @brief 阶段4：计量送样
 */
static uint8_t _delivery_stage_measure(DeliveryContext *ctx)
{
    // 静态变量用于跟踪TriggerStop状态
    static uint8_t trigger_stopped = 0;

    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 重置trigger_stopped标志
        trigger_stopped = 0;

        // 启动送样蠕动泵正转
        MotorRun(2, 1, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.DeliveryMotor = 1;
        g_State.CurrentBucketRunState = 7; // 送样
        //        if (ctx->bucket_id == 0)
        //        {
        //            g_State.ABucketState = 7;
        //        }
        //        else
        //        {
        //            g_State.BBucketState = 7;
        //        }

        printf("[送样] 计量阶段已开始：%u 秒, %u ml\r\n", ctx->deliver_time, ctx->target_volume);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    // ? 检查是否需要停止TriggerRun信号（20秒后）
    if (elapsed >= 20 && !trigger_stopped)
    {
        TriggerStop; // 停止数采仪触发信号
        trigger_stopped = 1;

        printf("[送样] TriggerStop信号已激活（20秒已过）\r\n");
    }

    if (elapsed >= ctx->deliver_time)
    {
        // 时间到
        MotorStop(2);
        g_State.DeliveryMotor = 0;

        printf("[送样] 计量阶段已完成：%lu 秒\r\n", elapsed);

        // 转到延时状态
        ctx->stage = DELIV_DELAY_500MS_AFTER_MEASURE;
        ctx->stage_start_time = 0;
        ctx->delay_start_ms = g_tmr3_milliseconds;

        return 0;
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(2, 1, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 阶段5：回抽
 */
static uint8_t _delivery_stage_backdraw(DeliveryContext *ctx)
{
    // 第一次进入
    if (ctx->stage_start_time == 0)
    {
        // 启动送样蠕动泵反转
        MotorRun(2, 0, ctx->rpm);
        ctx->stage_start_time = g_tmr2_seconds;

        // 更新屏幕状态
        g_State.DeliveryMotor = 1;

        printf("[送样] 回抽阶段已开始：%u 秒\r\n", ctx->backdraw_time);
        return 0;
    }

    // 检查时间
    uint32_t elapsed = g_tmr2_seconds - ctx->stage_start_time;

    if (elapsed >= ctx->backdraw_time)
    {
        // 时间到
        MotorStop(2);
        g_State.DeliveryMotor = 0;

        printf("[送样] 回抽阶段已完成：%lu 秒\r\n", elapsed);

        // 转到完成状态
        ctx->stage = DELIV_COMPLETED;
        _delivery_complete(ctx);

        return 1; // 返回完成
    }

    // 每200ms重发命令
    static uint32_t last_cmd = 0;
    if ((g_tmr2_seconds - last_cmd) >= 1 || last_cmd == 0)
    {
        last_cmd = g_tmr2_seconds;
        MotorRun(2, 0, ctx->rpm);
    }

    return 0;
}

/**
 * @brief 完成处理
 */
static void _delivery_complete(DeliveryContext *ctx)
{
    printf("[送样] 正在完成送样流程...\r\n");

    // 停止送样蠕动泵
    MotorStop(2);
    g_State.DeliveryMotor = 0;

    // 停止混样电机
    if (ctx->bucket_id == 0)
    {
        MixAStop;
    }
    else
    {
        MixBStop;
    }

    // ? 确保数采仪触发信号关闭
    TriggerStop;

    printf("[送样] TriggerStop信号已确认（送样完成）\r\n");

    // ? 计算实际送样水量（根据送样时长和校准参数）
    // 使用大水量校准点3计算流速（更准确）
    uint16_t actual_delivery_volume = 0;
    if (g_CalibrationParams.retainSampleCalib.time3 > 0)
    {
        // 优先使用校准点3（大水量）计算流速
        // 流速（ml/s） = realValue3 / time3
        // 实际送样量 = Duration × 流速
        uint32_t flow_rate_x100 = ((uint32_t)g_CalibrationParams.retainSampleCalib.realValue3 * 100) / (g_CalibrationParams.retainSampleCalib.time3+4);
        actual_delivery_volume = (uint16_t)((ctx->deliver_time * flow_rate_x100) / 100);

        printf("[送样] 计算的送样体积：%u ml (时长=%u s, 流速=%.2f ml/s, 基于校准点3)\r\n",
               actual_delivery_volume, ctx->deliver_time,
               (float)flow_rate_x100 / 100.0f);
    }
    else if (g_CalibrationParams.retainSampleCalib.time2 > 0)
    {
        // 校准点3不可用，使用校准点2
        uint32_t flow_rate_x100 = ((uint32_t)g_CalibrationParams.retainSampleCalib.realValue2 * 100) / g_CalibrationParams.retainSampleCalib.time2;
        actual_delivery_volume = (uint16_t)((ctx->deliver_time * flow_rate_x100) / 100);

        printf("[送样] 计算的送样体积：%u ml (时长=%u s, 流速=%.2f ml/s, 基于校准点2)\r\n",
               actual_delivery_volume, ctx->deliver_time,
               (float)flow_rate_x100 / 100.0f);
    }
    else if (g_CalibrationParams.retainSampleCalib.time1 > 0)
    {
        // 校准点2、3都不可用，才使用校准点1
        uint32_t flow_rate_x100 = ((uint32_t)g_CalibrationParams.retainSampleCalib.realValue1 * 100) / g_CalibrationParams.retainSampleCalib.time1;
        actual_delivery_volume = (uint16_t)((ctx->deliver_time * flow_rate_x100) / 100);

        printf("[送样] 计算的送样体积：%u ml (时长=%u s, 流速=%.2f ml/s, 基于校准点1)\r\n",
               actual_delivery_volume, ctx->deliver_time,
               (float)flow_rate_x100 / 100.0f);
    }
    else
    {
        // 所有校准参数都无效，使用target_volume作为fallback
        actual_delivery_volume = ctx->target_volume;
        printf("[送样] 警告：所有校准点都无效，使用目标体积=%u ml\r\n",
               actual_delivery_volume);
    }

    // 扣除实际送样水量（而非target_volume）
    // 正常模式：扣除送样水量
    if (ctx->bucket_id == 0)
    {
        g_State.SaveWarterA = (g_State.SaveWarterA > actual_delivery_volume)
                              ? (g_State.SaveWarterA - actual_delivery_volume)
                              : 0;
        printf("[送样] A桶已更新：%d ml (-%d)\r\n", g_State.SaveWarterA, actual_delivery_volume);
    }
    else
    {
        g_State.SaveWarterB = (g_State.SaveWarterB > actual_delivery_volume)
                              ? (g_State.SaveWarterB - actual_delivery_volume)
                              : 0;
        printf("[送样] B桶已更新：%d ml (-%d)\r\n", g_State.SaveWarterB, actual_delivery_volume);
    }

    // ====================================================================
    // 写入送样完成记录到TSDB（新split模式，继承sample_id）
    // ====================================================================
    WaterSampleContext *water_ctx = (ctx->bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    if (!water_ctx->valid)
    {
        printf("[送样完成] 警告：桶%c的sample_id无效，无法写入完成记录\r\n", ctx->bucket_id ? 'B' : 'A');
    }
    else
    {
        // 构造送样完成记录（继承sample_id）
        DeliveryCompleteRecord complete_record = {0};
        strcpy(complete_record.sample_id, water_ctx->sample_id);
        complete_record.end_time = rtc_counter_get();
        complete_record.delivery_volume = actual_delivery_volume;
        complete_record.result = 1;  // 成功
        complete_record.error_code = 0;

        // 写入TSDB
        if (tsdb_is_ready())
        {
            tsdb_event_append(LOG_DELIVERY_COMPLETE, &complete_record, sizeof(DeliveryCompleteRecord));
            printf("[送样TSDB] 写入完成记录：sample_id=%s, 实际量=%u ml\r\n",
                   water_ctx->sample_id, actual_delivery_volume);
        }

        // 更新桶上下文的送样完成时间
        water_ctx->delivery_complete_time = complete_record.end_time;

        // 重新构造start_record（用于缓存添加）
        DeliveryStartRecord start_record = {0};
        strcpy(start_record.sample_id, water_ctx->sample_id);
        start_record.delivery_mode = ctx->is_manual ? 2 : 1;
        start_record.bucket_id = ctx->bucket_id;
        start_record.start_time = ctx->total_start_time;
        start_record.target_volume = ctx->target_volume;
        start_record.is_manual = ctx->is_manual;

        // 添加到缓存
        cache_add_delivery(&start_record, &complete_record);
    }

    // 更新西安协议送样日志
    extern XianDeliveryLog_t g_XianDeliveryLog;
    RtcDateTimeComponents dt_xian_d;
    rtc_seconds_to_datetime(unix_to_2000(rtc_counter_get()), &dt_xian_d);

    g_XianDeliveryLog.mode = ctx->is_manual ? 99 : g_SampleConfig.SamplingMode;
    g_XianDeliveryLog.bucketId = (ctx->bucket_id == 0) ? 1 : 2; // 1=A桶, 2=B桶
    g_XianDeliveryLog.year = dt_xian_d.year;
    g_XianDeliveryLog.month = dt_xian_d.month;
    g_XianDeliveryLog.day = dt_xian_d.day;
    g_XianDeliveryLog.hour = dt_xian_d.hour;
    g_XianDeliveryLog.minute = dt_xian_d.minute;
    g_XianDeliveryLog.volume = actual_delivery_volume;
    g_XianDeliveryLog.result = 1; // 1=成功

    // 更新最近送样记录（用于留样时确定对应的送样桶）
    g_last_delivery_bucket = ctx->bucket_id;
    g_last_delivery_time = rtc_counter_get(); // ★ 统一基准：使用1970年Unix时间，与Task4窗口检查保持一致
    printf("[送样] 已更新最近送样记录: 桶=%c, 时间=%lu\r\n",
           ctx->bucket_id ? 'B' : 'A', g_last_delivery_time);

    // 记录水量变化
    uint16_t water_after = ctx->bucket_id ? g_State.SaveWarterB : g_State.SaveWarterA;
    log_water_volume_change(ctx->bucket_id, water_after, "DELIVERY");

    // 更新屏幕状态
    g_State.CurrentBucketRunState = 0;
    if (ctx->bucket_id == 0)
    {
        g_State.ABucketState = 0;
    }
    else
    {
        g_State.BBucketState = 0;
    }

    // 更新上下文
    ctx->result = 1;
    ctx->stage = DELIV_IDLE;

    flow_trigger_handle_delivery_complete(ctx->bucket_id, actual_delivery_volume);

    // ? 检查是否是首次送样完成（首次送样使用A桶且first_delivery_done=0）
    if (ctx->bucket_id == 0 && !g_tp_scheduler.first_delivery_done && !ctx->is_manual)
    {
        g_tp_scheduler.first_delivery_done = 1;
        printf("[送样] 首次送样完成，周期将在 %02d:00 开始\r\n",
               g_tp_scheduler.cycle_start_hour);
    }

    // 通知task4进行留样判定
    notify_task4_delivery_complete(ctx->bucket_id);

    if (g_tp_scheduler.is_running && !ctx->is_manual && g_tp_scheduler.cycle_idx != 0xFFFFFFFF)
    {
        g_tp_scheduler.delivery_done = 1;
    }

    // 送样完成，清除水样准备好标志（水样已被送出）
    if (ctx->bucket_id == 0)
    {
        g_water_sample_ready_A = 0;
    }
    else
    {
        g_water_sample_ready_B = 0;
    }
    printf("[送样] %c桶水样准备好标志已清除\r\n", ctx->bucket_id ? 'B' : 'A');

    printf("[送样] 送样成功完成\r\n");
}

/**
 * @brief 中止处理
 */
static void _delivery_abort(DeliveryContext *ctx)
{
    printf("[送样] 正在中止送样流程...\r\n");

    // 停止送样蠕动泵
    MotorStop(2);
    g_State.DeliveryMotor = 0;

    // 停止混样电机
    if (ctx->bucket_id == 0)
    {
        MixAStop;
    }
    else
    {
        MixBStop;
    }

    // ? 确保数采仪触发信号关闭
    TriggerStop;

    printf("[送样] TriggerStop信号已确认（送样已中止）\r\n");

    g_State.CurrentBucketRunState = 0;
    if (ctx->bucket_id == 0)
    {
        g_State.ABucketState = 0;
    }
    else
    {
        g_State.BBucketState = 0;
    }

    // ====================================================================
    // 写入送样中止记录到TSDB（新split模式）
    // ====================================================================
    WaterSampleContext *water_ctx = (ctx->bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    if (water_ctx->valid)
    {
        // 构造送样完成记录（result=2表示中止）
        DeliveryCompleteRecord complete_record = {0};
        strcpy(complete_record.sample_id, water_ctx->sample_id);
        complete_record.end_time = rtc_counter_get();
        complete_record.delivery_volume = 0;  // 中止时无送样量
        complete_record.result = 2;  // 中止
        complete_record.error_code = (uint8_t)ctx->stage;  // 记录在哪个阶段中止

        // 写入TSDB
        if (tsdb_is_ready())
        {
            tsdb_event_append(LOG_DELIVERY_COMPLETE, &complete_record, sizeof(DeliveryCompleteRecord));
            printf("[送样TSDB] 写入中止记录：sample_id=%s, 中止阶段=%u\r\n",
                   water_ctx->sample_id, complete_record.error_code);
        }
    }

    // 更新上下文
    ctx->result = 2;
    ctx->stage = DELIV_IDLE;

    printf("[送样] 送样已中止\r\n");
}

/**
 * @brief 推进状态机（task3调用）
 */
void delivery_step_if_active(void)
{
    if (g_delivery_ctx.stage == DELIV_IDLE ||
            g_delivery_ctx.stage == DELIV_COMPLETED ||
            g_delivery_ctx.stage == DELIV_ABORTED)
    {
        return; // 空闲状态，无需推进
    }

    _delivery_step(&g_delivery_ctx);
}

/**
 * @brief 查询送样状态
 */
uint8_t delivery_get_status(void)
{
    if (g_delivery_ctx.stage == DELIV_IDLE)
    {
        return 0; // 空闲
    }
    else if (g_delivery_ctx.stage == DELIV_COMPLETED)
    {
        return g_delivery_ctx.result == 1 ? 2 : 3; // 2=成功, 3=失败
    }
    else if (g_delivery_ctx.stage == DELIV_ABORTED)
    {
        return 4; // 中止
    }
    else
    {
        return 1; // 运行中
    }
}

/**
 * @brief 强制中止采样（如果正在进行）
 */
void sampling_force_abort_if_active(void)
{
    if (sampling_get_status() == 1) {
        printf("[采样] 强制中止采样\r\n");
        extern volatile uint8_t g_manual_operation_abort_flag;
        g_manual_operation_abort_flag = 1;
    }
}

/**
 * @brief 强制中止送样（如果正在进行）
 */
void delivery_force_abort_if_active(void)
{
    if (delivery_get_status() == 1) {
        printf("[送样] 强制中止送样\r\n");
        extern volatile uint8_t g_manual_operation_abort_flag;
        g_manual_operation_abort_flag = 1;
    }
}

/**
 * @brief 获取送样结果
 */
uint8_t delivery_get_result(uint8_t *result, uint8_t *error_code)
{
    if (g_delivery_ctx.stage == DELIV_COMPLETED || g_delivery_ctx.stage == DELIV_ABORTED)
    {
        if (result)
            *result = g_delivery_ctx.result;
        if (error_code)
            *error_code = g_delivery_ctx.error_code;
        return 1; // 有效结果
    }
    return 0; // 还在运行
}

/**
 * @brief 等待送样完成（测试函数使用）
 */
uint8_t delivery_wait(uint32_t timeout_ms)
{
    uint32_t start_tick = xTaskGetTickCount();

    while (1)
    {
        uint8_t status = delivery_get_status();

        if (status == 2)
        {
            return 1; // 成功
        }
        else if (status == 3 || status == 4)
        {
            return 0; // 失败或中止
        }

        // 检查超时
        if (timeout_ms > 0)
        {
            uint32_t elapsed = xTaskGetTickCount() - start_tick;
            if (elapsed >= pdMS_TO_TICKS(timeout_ms))
            {
                printf("[送样等待] 超时\r\n");
                return 0;
            }
        }

        // 继续推进状态机
        delivery_step_if_active();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void scheduler_dispatcher(void)
{
    switch (g_SampleConfig.SamplingMode)
    {
    case 0: // 时间等比模式
        scheduler_time_proportional();
        break;
    case 1: // 定时投加
        scheduler_fixed_time();
        break;
    case 2: // 通讯触发
        scheduler_comm_trigger();
        break;
    case 3: // 流量触发
        scheduler_flow_trigger();
        break;
    case 4: // 开关量触发
        scheduler_switch_trigger();
        break;
    default:
        scheduler_time_proportional();
        break;
    }
}

void notify_task4_delivery_complete(uint8_t bucket_id)
{
    if (task4_handle == NULL)
    {
        printf("[task3] task4_handle 未准备好, 无法通知保留任务\r\n");
        return;
    }

    uint32_t notify_value = (bucket_id <= 1u) ? (uint32_t)(bucket_id + 1u) : 0u;

    if (notify_value == 0u)
    {
        printf("[task3] 非法的桶号(%u), 发送全局停止通知\r\n", bucket_id);
        xTaskNotify(task4_handle, 0xFFu, eSetValueWithOverwrite);
        return;
    }

    BaseType_t r = xTaskNotify(task4_handle, notify_value, eSetValueWithOverwrite);
    if (r != pdPASS)
    {
        printf("[task3] 通知task4失败, 桶=%u\r\n", bucket_id);
    }
}

void analysis_report_switch(uint8_t level, uint32_t ts)
{
    retain_judge_notify_switch(level, ts);
}

void analysis_report_modbus(uint8_t action_code, uint32_t ts)
{
    retain_judge_notify_modbus(action_code, ts);
}

void analysis_report_analog(const float ch_values[6], uint32_t ts)
{
    (void)ch_values;
    retain_judge_check_analog(ts);
}


static void increment_time_forward(uint8_t time[3])
{
    time[2]++;
    if (time[2] >= 60) {
        time[2] = 0;
        time[1]++;
        if (time[1] >= 60) {
            time[1] = 0;
            time[0]++;
            if (time[0] >= 100) {
                time[0] = 99;
            }
        }
    }
}

void update_all_timers(void)
{
    static uint32_t last_update = 0;
    static uint8_t last_hour = 255;

    if (g_tmr2_seconds > last_update) {
        last_update = g_tmr2_seconds;

        // Update A bucket timer
        if (g_State.ABucketState != 0 && g_State.ABucketState != 45) {
            increment_time_forward(g_State.ABucketCountDown);
        }

        // Update B bucket timer
        if (g_State.BBucketState != 0 && g_State.BBucketState != 45) {
            increment_time_forward(g_State.BBucketCountDown);
        }

        // Sync current bucket timer
        if (g_State.CurrentBucket == 0) {
            g_State.CurrentBucketCountDown[0] = g_State.ABucketCountDown[0];
            g_State.CurrentBucketCountDown[1] = g_State.ABucketCountDown[1];
            g_State.CurrentBucketCountDown[2] = g_State.ABucketCountDown[2];
        } else {
            g_State.CurrentBucketCountDown[0] = g_State.BBucketCountDown[0];
            g_State.CurrentBucketCountDown[1] = g_State.BBucketCountDown[1];
            g_State.CurrentBucketCountDown[2] = g_State.BBucketCountDown[2];
        }

        // Update sampling interval timer
        if (g_State.State == 1) {
            increment_time_forward(g_State.SamplingIntervalCountDown);
            increment_time_forward(g_State.SamplingTotalTimeCountDown);
        }

        // Check if it's a new hour and reset total timer at midnight
        rtc_time_get();
        if (calendar.hour != last_hour) {
            last_hour = calendar.hour;
            if (calendar.hour == 0) {
                // Reset at midnight
                g_State.SamplingTotalTimeCountDown[0] = 0;
                g_State.SamplingTotalTimeCountDown[1] = 0;
                g_State.SamplingTotalTimeCountDown[2] = 0;
            }
        }
    }
}
//==============================================================================
// 调度器必需函数实现（临时添加以确保链接可用）
// 调度器函数实现已移至 sampling_time.c

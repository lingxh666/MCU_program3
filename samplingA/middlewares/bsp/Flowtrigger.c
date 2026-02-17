#include "Flowtrigger.h"
#include "sampling.h"
#include "sampling_time.h"
#include "freertos_app.h"
#include "work.h"
#include "screen.h"
#include "retain_judge.h"
#include "queue.h"
#include "semphr.h"
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "event_groups.h"
#include <string.h>

/* Instant delivery context is local to flow trigger */

typedef enum {
    INSTANT_IDLE = 0,
    INSTANT_VALVE_SETUP,
    INSTANT_RUNNING,
    INSTANT_COMPLETED,
    INSTANT_ABORTED
} InstantDeliveryState;

typedef struct {
    InstantDeliveryState state;
    uint32_t start_time;
    uint16_t duration;
    uint32_t timestamp_start;
    float flow_value_start;
    uint32_t valve_setup_start_ms;
} InstantDeliveryContext;

FlowTriggerSchedulerState g_ft_scheduler = {.state = FT_IDLE};
static InstantDeliveryContext g_instant_ctx = {.state = INSTANT_IDLE};

#define FT_NOTIFY_FLOW_START  (1u << 0)
#define FT_NOTIFY_FLOW_STOP   (1u << 1)

static volatile uint32_t g_ft_notify_flags = 0;

extern volatile uint8_t g_manual_operation_abort_flag;

static void ft_compute_sample_offsets(void);
static void ft_handle_new_cycle(void);
static int8_t ft_check_sample_trigger(uint16_t now_min);
static void ft_check_cycle_delivery_trigger(uint32_t cycle_idx);
static uint8_t calculate_cycle_start_hour(uint32_t instant_end_time);

// 状态机处理函数声明
static void ft_process_idle(uint32_t events);
static void ft_process_start_instant(void);
static void ft_process_start_full_sample(void);
static void ft_process_wait_hour(void);
static void ft_process_cycle_running(void);
static void ft_process_force_stop_drain(void);

//==============================================================================
// 流量触发模式实现
//==============================================================================

//------------------------------------------------------------------------------
// 瞬时送样状态机实现
//------------------------------------------------------------------------------

/**
 * @brief 启动瞬时送样（非阻塞）
 *
 * @return 1=成功启动, 0=失败
 */
uint8_t instant_delivery_start(void) {
    // 1. 检查状态
    if (g_instant_ctx.state != INSTANT_IDLE) {
        printf("[瞬时送样] 错误：已在进行中\r\n");
        return 0;
    }

    // 2. 设置阀位
    InstantThreeWayValveInstant;
    g_State.InstantThreeWayValve = 1;
    SampleThreeWayValveSample;
    g_State.SampleThreeWayValve = 0;

    // 设置延时状态（非阻塞方式），等待阀门到位
    g_instant_ctx.state = INSTANT_VALVE_SETUP;
    g_instant_ctx.valve_setup_start_ms = g_tmr3_milliseconds;
    g_instant_ctx.duration = g_DeliveryConfig.Duration;
    g_instant_ctx.timestamp_start = rtc_counter_get();
    g_instant_ctx.flow_value_start = g_RetainSampleConfig.channelData[7];  // 记录流量值
    printf("[即时送样] 等待三通阀动作到位（非阻塞，10秒）...\r\n");

    // TSDB事件将在延时完成后记录（见instant_delivery_update函数）

    printf("[INSTANT_DELIVERY] Started: duration=%u sec, flow=%.2f m3/h (waiting for valve setup)\r\n",
           g_instant_ctx.duration, g_instant_ctx.flow_value_start);

    return 1;
}
/**
 * @brief 瞬时送样状态机更新（task3周期调用）
 *
 * @return 0=运行中, 1=完成, 2=中止
 */
uint8_t instant_delivery_update(void) {
    // 处理阀门到位延时状态
    if (g_instant_ctx.state == INSTANT_VALVE_SETUP) {
        if (g_instant_ctx.valve_setup_start_ms == 0) {
            g_instant_ctx.valve_setup_start_ms = g_tmr3_milliseconds;
            return 0;
        }
        uint32_t elapsed = g_tmr3_milliseconds - g_instant_ctx.valve_setup_start_ms;
        if (elapsed > 0x80000000UL) {
            elapsed = g_tmr3_milliseconds + (0xFFFFFFFFUL - g_instant_ctx.valve_setup_start_ms);
        }
        if (elapsed >= 10000) {  // 10秒延时完成
            printf("[即时送样] 阀动作完成\r\n");
            // 启动送样泵
            g_instant_ctx.start_time = g_tmr2_seconds;
            g_instant_ctx.state = INSTANT_RUNNING;

            MotorRun(2, 1, g_SystemSettingConfig.Motorspeed);  // 送样泵，正转，使用配置的转速
            g_State.DeliveryMotor = 1;

            // 记录TSDB开始事件（延时完成后才记录）
            struct {
                uint8_t event_type;       // 0x01 = 瞬时送样开始
                uint32_t timestamp;
                uint16_t duration;
                float flow_value;
            } ev = { 0x01, g_instant_ctx.timestamp_start, g_instant_ctx.duration, g_instant_ctx.flow_value_start };
            tsdb_event_append(0x80, &ev, sizeof(ev));  // 0x80 = 流量触发模式事件组

            return 0;
        }
        return 0;  // 等待延时完成
    }

    if (g_instant_ctx.state != INSTANT_RUNNING) {
        return (g_instant_ctx.state == INSTANT_COMPLETED) ? 1 : 2;
    }

    // 中止检查
    if (g_manual_operation_abort_flag) {
        MotorStop(2);
        g_State.DeliveryMotor = 0;
        InstantThreeWayValveDirect;
        g_State.InstantThreeWayValve = 0;
        g_instant_ctx.state = INSTANT_ABORTED;
        printf("[瞬时送样] 已中止\r\n");
        return 2;
    }

    // 时间检查
    uint32_t elapsed = g_tmr2_seconds - g_instant_ctx.start_time;
    if (elapsed >= g_instant_ctx.duration) {
        // 完成
        MotorStop(2);
        g_State.DeliveryMotor = 0;
        InstantThreeWayValveDirect;
        g_State.InstantThreeWayValve = 0;
        g_instant_ctx.state = INSTANT_COMPLETED;

        // 记录TSDB完成事件
        struct {
            uint8_t event_type;       // 0x02 = 瞬时送样完成
            uint32_t timestamp;
            uint16_t actual_duration;
        } ev = { 0x02, rtc_counter_get(), (uint16_t)elapsed };
        tsdb_event_append(0x80, &ev, sizeof(ev));

        printf("[瞬时送样] 已完成：实际=%u 秒\r\n", (uint16_t)elapsed);
        return 1;
    }

    return 0;  // 运行中
}

/**
 * @brief 获取瞬时送样状态
 */
uint8_t instant_delivery_get_status(void) {
    return g_instant_ctx.state;
}

/**
 * @brief 重置瞬时送样状态
 */
void instant_delivery_reset(void) {
    g_instant_ctx.state = INSTANT_IDLE;
}

//------------------------------------------------------------------------------
// 流量触发调度器核心函数
//------------------------------------------------------------------------------

/**
 * @brief 初始化流量触发调度器
 */
void ft_scheduler_init(void) {
    // 防止重复初始化
    if (g_ft_scheduler.is_initialized && g_ft_scheduler.state != FT_IDLE) {
        return;
    }

    memset(&g_ft_scheduler, 0, sizeof(g_ft_scheduler));
    g_ft_scheduler.state = FT_IDLE;
    g_ft_scheduler.active_bucket = 0;  // 初始化为A桶
    g_ft_scheduler.is_initialized = 1;
    printf("[流量调度] 初始化完成，状态=IDLE\r\n");
}

/**
 * @brief 启动流量触发调度器
 */
void ft_scheduler_start(void) {
    // 清除中止标志
    g_manual_operation_abort_flag = 0;

    // 重置所有阶段状态
    g_ft_scheduler.state = FT_START_INSTANT;
    g_ft_scheduler.pending_events = 0;
    g_ft_scheduler.flow_start_time = rtc_counter_get();
    g_ft_scheduler.active_bucket = 0;  // 从A桶开始
    g_ft_scheduler.cycle_idx = 0;
    g_ft_scheduler.sample_done_mask = 0;
    g_ft_scheduler.delivery_done = 0;
    g_ft_scheduler.drain_in_progress = 0;
    g_ft_scheduler.waiting_retention = 0;
    g_ft_scheduler.retention_wait_start = 0;
    g_ft_scheduler.instant_delivery_end_time = 0;
    g_ft_scheduler.cycle_start_hour = 0;
    g_ft_scheduler.first_cycle_delayed = 0;
    g_ft_scheduler.delay_offset_min = 0;

    printf("[流量调度] 调度器已启动，状态=START_INSTANT\r\n");
}

/**
 * @brief 停止流量触发调度器
 */
void ft_scheduler_stop(void) {
    g_ft_scheduler.state = FT_IDLE;
    g_ft_scheduler.pending_events = 0;
    g_ft_scheduler.drain_in_progress = 0;
    g_ft_scheduler.waiting_retention = 0;
    printf("[流量调度] 调度器已停止，状态=IDLE\r\n");
}

/**
 * @brief 检查调度器是否处于IDLE状态
 */
uint8_t ft_scheduler_is_idle(void) {
    return (g_ft_scheduler.state == FT_IDLE) ? 1 : 0;
}

/**
 * @brief 获取调度器当前状态
 */
FlowTriggerState ft_scheduler_get_state(void) {
    return g_ft_scheduler.state;
}

/**
 * @brief 计算周期起点（考虑蠕动泵冲突）
 *
 * 功能：
 * - 根据实际校准参数计算足量采样耗时（非固定12分钟）
 * - 判断足量采样完成时间是否超过最近整点
 * - 若超过，推迟周期起点到下一个整点
 *
 * @param instant_end_time 瞬时送样完成时间（秒级时间戳）
 * @return 周期起点小时数（0-23）
 */
static uint8_t calculate_cycle_start_hour(uint32_t instant_end_time) {
    // 1. 计算足量采样耗时（根据配置动态计算）
    //    total_volume = SampleVolume × (CycleTime / SampleInterval)
    uint16_t cycle_time = g_SampleConfig.CycleTime;
    uint16_t interval = g_SampleConfig.SampleInterval;
    uint16_t single_volume = g_SampleConfig.SampleVolume;
    uint16_t total_volume = single_volume * (cycle_time / interval);

    // 使用校准参数计算采样时间（秒）
    uint16_t sampling_duration_sec = calc_sampling_time_by_volume(total_volume);

    printf("[FT_SCHED] Full sampling calculation: volume=%u ml, duration=%u sec\r\n",
           total_volume, sampling_duration_sec);

    // 2. 计算足量采样完成时间
    uint32_t full_samp_end_time = instant_end_time + sampling_duration_sec;

    // 3. 将时间戳转换为时间分量
    RtcDateTimeComponents dt_full;
    rtc_seconds_to_datetime(full_samp_end_time, &dt_full);  // 使用1970基准，无需偏移

    uint8_t hour_full = dt_full.hour;
    uint8_t min_full = dt_full.minute;
    uint8_t sec_full = dt_full.second;

    // 4. 计算周期起点（整点）：默认向上取整到下一整点
    uint8_t nearest_hour = (min_full > 0 || sec_full > 0) ? ((hour_full + 1) % 24) : hour_full;

    // 5. 蠕动泵冲突判断（保护窗口）
    //    若足量采样结束距离下一整点过近，则再额外延后1小时，避免切换点附近资源冲突。
    //    保护窗口默认2分钟：例如 12:58 结束 -> 13:00 可接受；12:59 结束 -> 推迟到 14:00。
    const uint32_t cycle_start_guard_sec = 120u;
    if (min_full > 0 || sec_full > 0) {
        uint32_t sec_to_next_hour = (uint32_t)(60u - (uint32_t)min_full) * 60u - (uint32_t)sec_full;
        if (sec_to_next_hour < cycle_start_guard_sec) {
            uint8_t delayed_hour = (nearest_hour + 1) % 24;

            // 记录TSDB事件
            struct {
                uint8_t event_type;       // 0x05 = 周期起点计算
                uint8_t cycle_hour;
                uint8_t conflict;         // 1=有冲突
                uint8_t reason;           // 推迟原因
            } ev = { 0x05, delayed_hour, 1, 0x01 };
            tsdb_event_append(0x80, &ev, sizeof(ev));

            printf("[流量调度] 蠕动泵冲突：足量采样结束=%02d:%02d:%02d，距下整点仅%lu秒 < %lu秒\r\n",
                   hour_full, min_full, sec_full,
                   (unsigned long)sec_to_next_hour, (unsigned long)cycle_start_guard_sec);
            printf("[流量调度] 周期开始延迟到 %02d:00\r\n", delayed_hour);

            return delayed_hour;
        }
    }

    // 无冲突：按向上取整整点启动
    {
        struct {
            uint8_t event_type;
            uint8_t cycle_hour;
            uint8_t conflict;
            uint8_t reason;
        } ev = { 0x05, nearest_hour, 0, 0x00 };
        tsdb_event_append(0x80, &ev, sizeof(ev));

        printf("[FT_SCHED] Cycle start aligned to %02d:00 (full_samp_end=%02d:%02d:%02d)\r\n",
               nearest_hour, hour_full, min_full, sec_full);
    }

    return nearest_hour;
}
/**
 * @brief 处理流量触发通知（task3周期调用）
 */
static void handle_flow_trigger_notifications(void) {
    // 原子读取并清除事件标志
    taskENTER_CRITICAL();
    uint32_t events = g_ft_notify_flags;
    g_ft_notify_flags = 0;
    taskEXIT_CRITICAL();

    // 将事件合并到待处理事件
    if (events & FT_NOTIFY_FLOW_START) {
        g_ft_scheduler.pending_events |= FT_EVT_FLOW_START;
        rtc_time_get();
        printf("[%02d:%02d:%02d][流量调度] 收到流量开始事件\r\n",
               calendar.hour, calendar.min, calendar.sec);
    }
    if (events & FT_NOTIFY_FLOW_STOP) {
        g_ft_scheduler.pending_events |= FT_EVT_FLOW_STOP;
        rtc_time_get();
        printf("[%02d:%02d:%02d][流量调度] 收到流量停止事件\r\n",
               calendar.hour, calendar.min, calendar.sec);
    }
}
/**
 * @brief 流量触发调度器主函数
 */
void scheduler_flow_trigger(void) {
    // 1. 收集事件
    handle_flow_trigger_notifications();

    // 2. 读取并消费事件（原子操作）
    taskENTER_CRITICAL();
    uint32_t events = g_ft_scheduler.pending_events;
    g_ft_scheduler.pending_events = 0;
    taskEXIT_CRITICAL();

    // 3. 停流事件优先处理（任何非IDLE状态）
    if ((events & FT_EVT_FLOW_STOP) && g_ft_scheduler.state != FT_IDLE) {
        rtc_time_get();
        printf("[%02d:%02d:%02d][流量调度] ========== 检测到流量停止 ==========\r\n",
               calendar.hour, calendar.min, calendar.sec);
        printf("[流量调度] 收到停流事件，进入强制停机\r\n");
        g_ft_scheduler.state = FT_FORCE_STOP_DRAIN;
        events &= ~FT_EVT_FLOW_START;  // 清除同时存在的启动事件
    }

    // 4. 状态机主循环
    switch (g_ft_scheduler.state) {
        case FT_IDLE:
            ft_process_idle(events);
            break;
        case FT_START_INSTANT:
            ft_process_start_instant();
            break;
        case FT_START_FULL_SAMPLE:
            ft_process_start_full_sample();
            break;
        case FT_WAIT_HOUR:
            ft_process_wait_hour();
            break;
        case FT_CYCLE_RUNNING:
            ft_process_cycle_running();
            break;
        case FT_FORCE_STOP_DRAIN:
            ft_process_force_stop_drain();
            break;
        default:
            g_ft_scheduler.state = FT_IDLE;
            break;
    }
}

//------------------------------------------------------------------------------
// 状态机处理函数
//------------------------------------------------------------------------------

/**
 * @brief IDLE状态处理 - 等待流量开始
 */
static void ft_process_idle(uint32_t events) {
    if (events & FT_EVT_FLOW_START) {
        rtc_time_get();
        printf("[%02d:%02d:%02d][流量调度] ========== 检测到流量开始 ==========\r\n",
               calendar.hour, calendar.min, calendar.sec);

        // 启动流量触发序列
        ft_scheduler_start();

        // 启动瞬时送样
        printf("[流量调度] 步骤1：瞬时送样启动中...\r\n");
        if (!instant_delivery_start()) {
            printf("[流量调度] 瞬时送样启动失败\r\n");
            ft_scheduler_stop();
        }
    }
}

/**
 * @brief 瞬时送样阶段处理
 */
static void ft_process_start_instant(void) {
    uint8_t status = instant_delivery_update();

    if (status == 1) {  // 完成
        g_ft_scheduler.instant_delivery_end_time = rtc_counter_get();
        g_ft_scheduler.total_deliveries++;
        instant_delivery_reset();

        // ★ 通知task4为A桶设置留样窗口
        // 瞬时送样 = A桶的第一次送样（先送后采）
        g_last_delivery_bucket = 0;  // A桶
        g_last_delivery_time = g_ft_scheduler.instant_delivery_end_time;
        notify_task4_delivery_complete(0);  // 通知task4：A桶送样完成
        printf("[流量调度] 已通知task4为A桶设置留样窗口\r\n");

        // ★ 提前计算周期起点（用于检测是否超过整点）
        uint8_t cycle_start_hour = calculate_cycle_start_hour(
            g_ft_scheduler.instant_delivery_end_time
        );
        g_ft_scheduler.cycle_start_hour = cycle_start_hour;
        ft_compute_sample_offsets();
        printf("[流量调度] 周期起点预计为 %02d:00\r\n", cycle_start_hour);

        // 进入留样采样阶段（只需留样量+200ml，不需要满量）
        g_ft_scheduler.state = FT_START_FULL_SAMPLE;
        rtc_time_get();
        uint16_t retain_sample_volume = g_RetainSampleConfig.SampleVolume + 200;
        printf("[%02d:%02d:%02d][流量调度] 步骤2：留样采样启动中（桶A，%u ml）...\r\n",
               calendar.hour, calendar.min, calendar.sec, retain_sample_volume);

        if (!sampling_start(0, retain_sample_volume, 0, 0)) {
            printf("[流量调度] 留样采样启动失败\r\n");
            ft_scheduler_stop();
        }
    } else if (status == 2) {  // 中止
        rtc_time_get();
        printf("[%02d:%02d:%02d][流量调度] 瞬时送样已中止\r\n",
               calendar.hour, calendar.min, calendar.sec);
        instant_delivery_reset();
        ft_scheduler_stop();
    }
}

/**
 * @brief 留样采样阶段处理
 *
 * 同时检查：
 * 1. A桶采样是否完成
 * 2. 是否已到达/超过周期起点整点
 */
static void ft_process_start_full_sample(void) {
    uint8_t samp_status = sampling_get_status();
    rtc_time_get();

    // 调试打印：每30秒打印一次采样状态
    static uint16_t debug_cnt = 0;
    if (++debug_cnt >= 30) {
        debug_cnt = 0;
        printf("[流量调度] 等待留样采样完成, samp_status=%d, 当前时间=%02d:%02d\r\n",
               samp_status, calendar.hour, calendar.min);
    }

    // 检查是否已到达/超过周期起点
    uint8_t past_cycle_start = 0;
    if (calendar.hour == g_ft_scheduler.cycle_start_hour) {
        past_cycle_start = 1;  // 已到达周期起点小时
    }
    // 处理跨天情况
    int16_t hour_diff = (int16_t)calendar.hour - (int16_t)g_ft_scheduler.cycle_start_hour;
    if (hour_diff < 0) hour_diff += 24;
    if (hour_diff > 0 && hour_diff < 12) {
        past_cycle_start = 1;
    }

    if (samp_status == 0 || samp_status == 2) {  // 采样完成
        if (past_cycle_start) {
            // 已超过整点，直接进入周期运行
            g_ft_scheduler.state = FT_CYCLE_RUNNING;
            g_ft_scheduler.cycle_idx = 0;
            g_ft_scheduler.active_bucket = 1;  // B桶
            g_ft_scheduler.sample_done_mask = 0;
            g_ft_scheduler.delivery_done = 0;

            printf("[流量调度] 留样采样完成，已超过整点，直接进入周期运行\r\n");

            // ★ 立即补采B桶第一次采样
            printf("[流量调度] 补采B桶第一次采样...\r\n");
            if (sampling_start(1, g_SampleConfig.SampleVolume, 0, 0)) {
                g_ft_scheduler.sample_done_mask |= (1u << 0);  // 标记第1次采样已触发
                g_ft_scheduler.total_samples++;
                printf("[流量调度] B桶补采已启动\r\n");
            } else {
                printf("[流量调度] B桶补采启动失败\r\n");
            }
        } else {
            // 未到整点，进入等待整点阶段
            g_ft_scheduler.state = FT_WAIT_HOUR;
            printf("[流量调度] 留样采样完成，等待周期起点 %02d:00\r\n",
                   g_ft_scheduler.cycle_start_hour);
        }

    } else if (samp_status == 3 || samp_status == 4) {  // 失败/中止
        printf("[%02d:%02d:%02d][流量调度] 留样采样失败/中止 (status=%d)\r\n",
               calendar.hour, calendar.min, calendar.sec, samp_status);
        ft_scheduler_stop();
    }
}

/**
 * @brief 等待整点阶段处理
 */
static void ft_process_wait_hour(void) {
    rtc_time_get();

    if (calendar.hour == g_ft_scheduler.cycle_start_hour && calendar.min == 0) {
        // 进入周期运行阶段
        g_ft_scheduler.state = FT_CYCLE_RUNNING;
        g_ft_scheduler.cycle_idx = 0;
        g_ft_scheduler.active_bucket = 1;  // B桶（A桶已完成满量采样）
        g_ft_scheduler.sample_done_mask = 0;
        g_ft_scheduler.delivery_done = 0;

        // 记录TSDB事件
        struct {
            uint8_t event_type;
            uint32_t timestamp;
            uint8_t cycle_idx;
            uint8_t active_bucket;
        } ev = { 0x06, rtc_counter_get(), 0, 1 };
        tsdb_event_append(0x80, &ev, sizeof(ev));

        printf("[流量调度] 周期开始，活跃桶=B\r\n");
    }
}

/**
 * @brief 周期运行阶段处理
 */
static void ft_process_cycle_running(void) {
    rtc_time_get();

    // 周期计算（复用时间等比逻辑）
    int16_t hours_since_start = calendar.hour - g_ft_scheduler.cycle_start_hour;
    if (hours_since_start < 0) hours_since_start += 24;

    if (hours_since_start >= 23) return;

    uint16_t minutes_since_start = hours_since_start * 60 + calendar.min;
    uint16_t cycle_time = g_SampleConfig.CycleTime;
    uint32_t cycle_idx = minutes_since_start / cycle_time;
    uint16_t cycle_off_min = minutes_since_start % cycle_time;

    // 周期切换检测
    if (cycle_idx != g_ft_scheduler.cycle_idx) {
        ft_handle_new_cycle();
        g_ft_scheduler.cycle_idx = cycle_idx;
    }

    // 采样触发检测
    if (calendar.sec == 0) {
        int8_t sample_idx = ft_check_sample_trigger(cycle_off_min);
        if (sample_idx >= 0) {
            uint32_t mask = (1u << sample_idx);
            if ((g_ft_scheduler.sample_done_mask & mask) == 0) {
                uint8_t bucket = g_ft_scheduler.active_bucket;
                if (sampling_start(bucket, g_SampleConfig.SampleVolume, 0, 0)) {
                    g_ft_scheduler.sample_done_mask |= mask;
                    g_ft_scheduler.total_samples++;
                    printf("[FT_SCHED] 样本 #%d 触发: 周期=%lu, 桶=%c\r\n",
                           sample_idx + 1, (unsigned long)cycle_idx, bucket ? 'B' : 'A');
                }
            }
        }

        // 送样触发检测
        ft_check_cycle_delivery_trigger(cycle_idx);
    }
}

/**
 * @brief 强制停机排空阶段处理
 *
 * 业务逻辑：
 * - 如果正在留样，等待留样完成再停止电机
 * - 如果不在留样，直接停止，不等待留样窗口
 */
static void ft_process_force_stop_drain(void) {
    // 检查是否有桶正在留样
    uint8_t retention_active = (g_host_status.bucket_a_state == BUCKET_STATE_RETENTION) ||
                               (g_host_status.bucket_b_state == BUCKET_STATE_RETENTION);

    // 阶段1：等待留样完成（如果正在留样）
    if (!g_ft_scheduler.drain_in_progress) {
        if (retention_active) {
            // 首次检测到留样进行中
            if (!g_ft_scheduler.waiting_retention) {
                g_ft_scheduler.waiting_retention = 1;
                g_ft_scheduler.retention_wait_start = g_tmr2_seconds;
                printf("[流量调度] 检测到留样进行中，等待留样完成...\r\n");
            }

            // 检查等待超时（5分钟）
            uint32_t wait_elapsed = g_tmr2_seconds - g_ft_scheduler.retention_wait_start;
            if (wait_elapsed >= 300) {
                printf("[流量调度] 等待留样超时（%lu秒），强制停止\r\n", wait_elapsed);
                g_ft_scheduler.waiting_retention = 0;
                // 继续执行停机操作
            } else {
                // 继续等待
                return;
            }
        } else {
            // 不在留样，清除等待标志
            g_ft_scheduler.waiting_retention = 0;
        }

        // 阶段2：执行停机操作
        printf("[流量调度] ========== 执行强制停机 ==========\r\n");

        // 1. 设置中止标志
        g_manual_operation_abort_flag = 1;

        // 2. 强制中止瞬时送样
        instant_delivery_reset();

        // 3. 强制中止采样/送样
        sampling_force_abort_if_active();
        delivery_force_abort_if_active();

        // 4. 停止电机
        MotorStop(1);
        MotorStop(2);
        g_State.SamplingMotor = 0;
        g_State.DeliveryMotor = 0;

        // 5. 恢复阀门
        InstantThreeWayValveDirect;
        g_State.InstantThreeWayValve = 0;
        SampleThreeWayValveSTAY;
        g_State.SampleThreeWayValve = 0;

        // 6. 启动排空（留样完成后桶已排空，这里确保排空）
        printf("[流量调度] 排空AB桶...\r\n");
        DrainARun;
        DrainBRun;

        // 7. 记录排空开始时间
        g_ft_scheduler.drain_start_time = g_tmr2_seconds;
        g_ft_scheduler.drain_in_progress = 1;

        // 8. 记录TSDB事件
        struct {
            uint8_t event_type;
            uint32_t timestamp;
        } ev = { 0x0F, rtc_counter_get() };
        tsdb_event_append(0x80, &ev, sizeof(ev));
    }

    // 阶段3：检查排空是否完成
    uint16_t drain_duration = g_SampleConfig.BucketDrainTime;
    if (drain_duration == 0 || drain_duration > 600) {
        drain_duration = 60;
    }

    if ((g_tmr2_seconds - g_ft_scheduler.drain_start_time) >= drain_duration) {
        // 排空完成
        DrainAStop;
        DrainBStop;

        // 清零存水量
        g_State.SaveWarterA = 0;
        g_State.SaveWarterB = 0;
        g_State.ABucketState = 0;
        g_State.BBucketState = 0;
        printf("[流量调度] AB桶存水量已清零\r\n");

        // 清除中止标志
        g_manual_operation_abort_flag = 0;

        // 回到IDLE
        ft_scheduler_stop();
        printf("[流量调度] 强制停机完成，进入待机状态\r\n");
    }
}



// 流量触发周期采样逻辑（复用时间等比模式）
//------------------------------------------------------------------------------

/**
 * @brief 计算流量触发模式的采样时间点数组（复用时间等比逻辑）
 */
static void ft_compute_sample_offsets(void) {
    uint16_t cycle_time_min = g_SampleConfig.CycleTime;  // 周期时长（分钟）
    uint16_t sample_interval = g_SampleConfig.SampleInterval;  // 采样间隔（分钟）
    g_ft_scheduler.sample_count = cycle_time_min / sample_interval;

    // 计算采样推迟时间和间隔调整（仅在CycleTime=60分钟时应用）
    uint16_t first_sample_offset_min = 0;
    uint16_t adjusted_sample_interval = sample_interval;

    if (g_SampleConfig.CycleTime == 60) {
        // 仅在60分钟周期时检查是否需要推迟
        uint16_t analysis_time_min = g_SampleConfig.AnalysisTime ;

        if (analysis_time_min > 58) {
            // 需要应用采样推迟机制
            g_ft_scheduler.first_cycle_delayed = 1;

            if (analysis_time_min <= 64) {
                // AnalysisTime = 59-64分钟：推迟时间 = AnalysisTime - 58
                first_sample_offset_min = analysis_time_min - 58;  // 1-6分钟
                g_ft_scheduler.delay_offset_min = first_sample_offset_min;
            } else {
                // AnalysisTime > 64分钟：固定推迟6分钟，同时减少采样间隔
                first_sample_offset_min = 6;
                adjusted_sample_interval = 13;  // 从15分钟减少到13分钟
                g_ft_scheduler.delay_offset_min = 6;
            }

            printf("[FT_SCHED] Sampling offset applied: CycleTime=%u, AnalysisTime=%u min, FirstOffset=%u min, Interval=%u min\r\n",
                   g_SampleConfig.CycleTime, analysis_time_min, first_sample_offset_min, adjusted_sample_interval);
        } else {
            g_ft_scheduler.first_cycle_delayed = 0;
            g_ft_scheduler.delay_offset_min = 0;
        }
    } else {
        g_ft_scheduler.first_cycle_delayed = 0;
        g_ft_scheduler.delay_offset_min = 0;
    }

    // 计算采样时间点数组（使用推迟时间和调整后的间隔）
    for (int i = 0; i < g_ft_scheduler.sample_count; i++) {
        g_ft_scheduler.sample_offsets[i] = first_sample_offset_min + i * adjusted_sample_interval;  // 存储分钟
    }

    // 保存配置的送样分钟
    g_ft_scheduler.configured_delivery_min = g_DeliveryConfig.StartMin;

    // 输出采样时间点（调试用）
    if (first_sample_offset_min > 0 || adjusted_sample_interval != sample_interval) {
        printf("[FT_SCHED] 采样时间：");
        for (int i = 0; i < g_ft_scheduler.sample_count; i++) {
            printf("%u:00 ", g_ft_scheduler.sample_offsets[i]);  // 直接输出分钟数
        }
        printf("\r\n");
    }
}

/**
 * @brief 流量触发模式检查采样触发（复用时间等比逻辑）
 * @param now_min 周期内偏移分钟数
 * @return 采样索引，>=0表示需要触发，-1表示不需要
 */
static int8_t ft_check_sample_trigger(uint16_t now_min) {
    for (uint8_t i = 0; i < g_ft_scheduler.sample_count; i++) {
        uint16_t target_offset = g_ft_scheduler.sample_offsets[i];

        // 首周期第一个采样点推迟逻辑
        if (g_ft_scheduler.first_cycle_delayed &&
                g_ft_scheduler.cycle_idx == 0 &&
                i == 0) {
            // 推迟delay_offset_min分钟
            target_offset += g_ft_scheduler.delay_offset_min;

            if (now_min == target_offset) {
                printf("[FT_SCHED] 首次采样延迟：%u 分钟从周期开始\r\n", target_offset);
                return (int8_t)i;
            }
        } else {
            if (now_min == target_offset) {
                return (int8_t)i;
            }
        }
    }
    return -1;
}

/**
 * @brief 流量触发模式处理新周期（复用时间等比逻辑）
 */
static void ft_handle_new_cycle(void) {
    // 清零采样完成掩码
    g_ft_scheduler.sample_done_mask = 0;

    // 切换活跃桶（AB交替）
    g_ft_scheduler.active_bucket = (g_ft_scheduler.active_bucket == 0) ? 1 : 0;

    // 更新统计
    g_ft_scheduler.total_cycles++;

    printf("[FT_SCHED] 新周期开始：周期=%lu, 活跃桶=%c\r\n",
           (unsigned long)g_ft_scheduler.cycle_idx,
           g_ft_scheduler.active_bucket ? 'B' : 'A');
}

/**
 * @brief 流量触发模式检查周期送样触发（复用时间等比逻辑）
 * @param cycle_idx 当前周期索引
 */
static void ft_check_cycle_delivery_trigger(uint32_t cycle_idx) {
    // 计算当前周期的送样时间：(周期结束小时 - 1):StartMin
    uint16_t cycle_hours = g_SampleConfig.CycleTime / 60;
    uint8_t cycle_end_hour = (g_ft_scheduler.cycle_start_hour +
                              (cycle_idx + 1) * cycle_hours) % 24;
    uint8_t delivery_hour = (cycle_end_hour + 24 - 1) % 24;  // 周期结束前1小时

    if (calendar.hour == delivery_hour && calendar.min == g_ft_scheduler.configured_delivery_min) {
        // 检查送样是否已经在运行
        if (delivery_get_status() == 1) {
            printf("[FT_SCHED] 周期送样已在进行中\r\n");
            return;
        }

        // 检查所有采样是否完成
        uint32_t expected_mask = (1u << g_ft_scheduler.sample_count) - 1;
        if (g_ft_scheduler.sample_done_mask != expected_mask) {
            printf("[FT_SCHED] 周期送样跳过：采样未完成\r\n");
            return;
        }

        // 检查水量
        #define MINIMUM_DELIVERY_WATER_FL 50  // 最小送样量50ml
        uint16_t current_water = g_ft_scheduler.active_bucket ? g_State.SaveWarterB : g_State.SaveWarterA;

        // ★ 修改：只要有水就送，目标量=当前水量（与TP模式一致）
        if (current_water < MINIMUM_DELIVERY_WATER_FL) {
            printf("[FT_SCHED] 周期送样跳过：水量不足（%u < %d ml）\r\n",
                   current_water, MINIMUM_DELIVERY_WATER_FL);
            return;
        }

        // 触发周期送样
        uint8_t result = delivery_start(g_ft_scheduler.active_bucket, current_water, 0);

        if (result) {
            g_ft_scheduler.total_deliveries++;
            printf("[FT_SCHED] 周期送样已启动：桶=%c, 体积=%u\r\n",
                   g_ft_scheduler.active_bucket ? 'B' : 'A', current_water);
        }
    }
}

void flow_trigger_handle_delivery_complete(uint8_t bucket_id, uint16_t delivered_volume) {
    // 废弃函数 - 保留空实现，下版本删除
    (void)bucket_id;
    (void)delivered_volume;
}

//------------------------------------------------------------------------------
// 流量触发通知接口
//------------------------------------------------------------------------------

/**
 * @brief 流量上升沿通知（由retain_judge.c/task6调用）
 *
 * ?? **任务安全**：使用标志位通知，由task3处理
 *
 * @param timestamp 时间戳（秒）
 */

void flow_trigger_notify_start(uint32_t timestamp) {
    printf("[流量通知] 检测到流量开始于 %lu\r\n", (unsigned long)timestamp);

    // 设置通知标志（使用临界区保护）
    taskENTER_CRITICAL();
    g_ft_notify_flags |= FT_NOTIFY_FLOW_START;
    taskEXIT_CRITICAL();
}

/**
 * @brief 流量下降沿通知（由retain_judge.c/task6调用）
 *
 * @param timestamp 时间戳（秒）
 */
void flow_trigger_notify_stop(uint32_t timestamp) {
    printf("[流量通知] 检测到流量停止于 %lu\r\n", (unsigned long)timestamp);

    // 设置通知标志（使用临界区保护）
    taskENTER_CRITICAL();
    g_ft_notify_flags |= FT_NOTIFY_FLOW_STOP;
    taskEXIT_CRITICAL();
}

/**
 * @brief 留样任务完成回调（由留样任务调用）
 *
 * ?? **调用时机**：留样任务完成所有留样判定和排空操作后
 *
 * 功能：
 * - 通知流量触发调度器可以安全停止
 * - 调度器收到此回调后，清理状态并停止运行
 */
void flow_trigger_retention_complete_callback(void) {
    // 废弃函数 - 保留空实现，下版本删除
}

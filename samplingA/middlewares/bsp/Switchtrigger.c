#include "Switchtrigger.h"
#include "sampling.h"
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

#define ST_NOTIFY_SWITCH_TRIGGER  (1u << 0)

static volatile uint32_t g_st_notify_flags = 0;
static SwitchTriggerSchedulerState g_st_scheduler = {0};

//==============================================================================
// 开关量触发调度器实现
//==============================================================================

/**
 * @brief 开关量通知信号处理（由BTN09_PRESS_DOWN_Handler调用）
 *
 * 防抖机制：
 * - 如果距离上次通知时间小于1秒，忽略（防止频繁触发）
 * - 使用边沿触发：只在信号从"未处理"到"已处理"时记录一次
 */
void switch_trigger_notify_signal(uint32_t timestamp) {
    // ★ 注意：此函数在中断上下文中调用，禁止使用printf或其他阻塞操作

    // 防抖：1秒内只接受一次信号
    uint32_t now = rtc_counter_get();
    if (g_st_scheduler.last_notify_time != 0 &&
            (now - g_st_scheduler.last_notify_time) < 1) {
        // 距离上次通知不到1秒，忽略（防止频繁触发导致系统卡死）
        return;
    }

    // 边沿触发：只在信号从"已处理"到"未处理"时设置标志
    // 如果上一个信号还未处理，不重复设置
    if (g_st_scheduler.last_signal_processed) {
        g_st_notify_flags |= ST_NOTIFY_SWITCH_TRIGGER;
        g_st_scheduler.last_notify_time = now;
        g_st_scheduler.last_signal_processed = 0;  // 标记为待处理
    }
    // 其他情况：静默忽略，避免中断中打印导致卡死
}

/**
 * @brief 处理开关量触发通知（task3周期调用）
 */
static void handle_switch_trigger_notifications(void) {
    // 检查开关量通知
    if (g_st_notify_flags & ST_NOTIFY_SWITCH_TRIGGER) {
        g_st_notify_flags &= ~ST_NOTIFY_SWITCH_TRIGGER;

        uint32_t timestamp = rtc_counter_get();
        g_st_scheduler.switch_signal_received = 1;
        g_st_scheduler.switch_signal_time = timestamp;
        g_st_scheduler.switch_trigger_count++;
        g_st_scheduler.last_signal_processed = 1;  // 标记信号已处理

        // 记录TSDB事件（开关量触发专用）
        struct {
            uint8_t event_type;       // 0x01 = 开关量信号触发
            uint32_t timestamp;
            uint8_t bucket_id;        // 目标桶（0=A, 1=B，首次触发时为等待状态）
        } ev = { ST_EVT_SWITCH_TRIGGER, timestamp, g_st_scheduler.active_bucket };
        tsdb_event_append(0x90, &ev, sizeof(ev));  // 0x90 = 开关量触发模式事件组

        printf("[ST_SCHED] ========== SWITCH TRIGGER DETECTED ==========\r\n");
        printf("[ST_SCHED] Timestamp: %lu, Bucket: %c, TriggerCount: %lu\r\n",
               (unsigned long)timestamp, g_st_scheduler.active_bucket ? 'B' : 'A',
               (unsigned long)g_st_scheduler.switch_trigger_count);
    }
}

/**
 * @brief 计算开关量检测窗口
 */
static void st_calculate_window(uint16_t sample_offset_min, uint32_t cycle_start_sec,
                                uint32_t *window_start, uint32_t *window_end) {
    // 采样时间点 = 周期起点 + 分钟偏移
    uint32_t sample_time = cycle_start_sec + (uint32_t)sample_offset_min * 60;

    // 窗口：采样时间点前后1分钟（共2分钟）
    *window_start = sample_time - 60;  // 前1分钟
    *window_end = sample_time + 60;    // 后1分钟
}

/**
 * @brief 初始化开关量触发调度器
 * @param is_power_recovery 1=复位后初始化, 0=正常初始化
 */
void st_scheduler_init(uint8_t is_power_recovery) {
    // ★ 防止重复初始化
    if (g_st_scheduler.is_initialized && g_st_scheduler.is_running) {
        return;
    }

    memset(&g_st_scheduler, 0, sizeof(g_st_scheduler));

    // 参数验证
    if (g_SampleConfig.CycleTime == 0 || g_SampleConfig.SampleInterval == 0) {
        return;
    }

    // 计算采样时间点（复用时间等比逻辑）
    g_st_scheduler.sample_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
    if (g_st_scheduler.sample_count > 24) g_st_scheduler.sample_count = 24;

    uint16_t sample_interval = g_SampleConfig.SampleInterval;
    for (uint8_t i = 0; i < g_st_scheduler.sample_count; i++) {
        g_st_scheduler.sample_offsets[i] = i * sample_interval;  // 从0分钟开始
    }

    // 保存配置参数
    g_st_scheduler.configured_delivery_min = g_DeliveryConfig.StartMin;

    // 正常初始化时计算采样时间，复位后不计算（因为可能会调整设置项）
    if (!is_power_recovery) {
        extern uint16_t calc_sampling_time_by_volume(uint16_t target_ml);
        calc_sampling_time_by_volume(g_SampleConfig.SampleVolume);
    }

    // 等待首次触发
    g_st_scheduler.waiting_first_trigger = 1;
    g_st_scheduler.active_bucket = 0;  // 默认从A桶开始

    // 初始化边沿触发状态（允许第一次信号通过）
    g_st_scheduler.last_signal_processed = 1;
    g_st_scheduler.last_notify_time = 0;

    // 记录TSDB事件
    struct {
        uint8_t event_type;       // 0x02 = 调度器初始化
        uint16_t cycle_time;
        uint16_t interval;
        uint8_t delivery_hour;
        uint8_t delivery_min;
    } ev = {
        ST_EVT_INIT,
        g_SampleConfig.CycleTime,
        g_SampleConfig.SampleInterval,
        g_DeliveryConfig.StartHour,
        g_DeliveryConfig.StartMin
    };
    tsdb_event_append(0x90, &ev, sizeof(ev));

    g_st_scheduler.is_initialized = 1;
}

/**
 * @brief 启动开关量触发调度器
 */
void st_scheduler_start(void) {
    if (!g_st_scheduler.is_initialized) {
        // 如果未初始化，默认按正常初始化处理（不传入复位标志）
        st_scheduler_init(0);
    }

    g_st_scheduler.is_running = 1;
    g_st_scheduler.waiting_first_trigger = 1;

    // 记录TSDB事件
    struct {
        uint8_t event_type;       // 0x03 = 调度器启动
        uint32_t timestamp;
    } ev = { ST_EVT_START, rtc_counter_get() };
    tsdb_event_append(0x90, &ev, sizeof(ev));

    printf("[开关量调度] 开关量触发调度器已启动\r\n");
}

/**
 * @brief 停止开关量触发调度器
 */
void st_scheduler_stop(void) {
    g_st_scheduler.is_running = 0;
    printf("[开关量调度] 开关量触发调度器已停止\r\n");
}
/**
 * @brief 开关量触发调度器主函数（task3周期调用）
 */
void scheduler_switch_trigger(void) {
    // 1. 检查运行状态
    if (!g_st_scheduler.is_running) {
        return;
    }

    // 2. 读取当前时间
    rtc_time_get();
    uint32_t now_sec = rtc_counter_get();

    // 3. 直接读取GPIO检测开关量信号（替代按钮库回调）
    extern uint8_t read_trigger_sampling_signal(void);

    // ★ 读取GPIO状态（低电平触发：GPIO=0表示触发）
    uint8_t gpio_state = read_trigger_sampling_signal();

    // ★★ 启动阶段检测：等待首次触发信号
    if (!g_st_scheduler.first_trigger_done) {
        // 检测低电平（触发状态）
        if (gpio_state == 0) {
            // 首次检测到低电平，记录信号并停止启动阶段的检测
            g_st_scheduler.switch_signal_received = 1;
            g_st_scheduler.switch_signal_time = now_sec;
            g_st_scheduler.switch_trigger_count++;

            rtc_time_get();
            printf("[%02d:%02d:%02d][开关量调度] 启动阶段检测到首次触发信号\r\n",
                   calendar.hour, calendar.min, calendar.sec);

            // 记录TSDB事件
            struct {
                uint8_t event_type;
                uint32_t timestamp;
                uint8_t bucket_id;
            } ev = { ST_EVT_SWITCH_TRIGGER, now_sec, g_st_scheduler.active_bucket };
            tsdb_event_append(0x90, &ev, sizeof(ev));
        }
        // 如果未检测到信号，继续等待（不调用handle_switch_trigger_notifications）
        // 只有检测到信号后才会进入后续流程
    }

    // 4. 处理开关量通知
    handle_switch_trigger_notifications();

    // 4. 首次运行处理
    if (!g_st_scheduler.first_trigger_done) {
        // 等待首次开关量信号
        if (!g_st_scheduler.switch_signal_received) {
            return;  // 继续等待
        }

        // 收到首次信号，记录首次触发时间
        g_st_scheduler.first_trigger_time = g_st_scheduler.switch_signal_time;
        g_st_scheduler.waiting_first_trigger = 0;

        // 简化处理：直接使用时间不足模式，等待整点开始周期循环
        rtc_time_get();
        uint8_t delivery_mode = 2;  // 固定使用时间不足模式
        uint8_t first_delivery_hour = (calendar.hour + 1) % 24;  // 下一个整点

        rtc_time_get();
        printf("[%02d:%02d:%02d][开关量调度] ========== 首次触发：简化模式 ==========\r\n",
               calendar.hour, calendar.min, calendar.sec);
        printf("[%02d:%02d:%02d][开关量调度] 策略：直接等待整点开始周期循环\r\n",
               calendar.hour, calendar.min, calendar.sec);

        g_st_scheduler.first_delivery_mode = delivery_mode;
        g_st_scheduler.first_delivery_hour = first_delivery_hour;
        g_st_scheduler.cycle_start_hour = (first_delivery_hour + 1) % 24;

        // 简化模式：不需要计算启动采样间隔
        g_st_scheduler.startup_sample_interval_sec = 0;
        printf("[开关量调度] 简化模式，不计算启动采样间隔\r\n");

        // 记录TSDB事件：首次触发检测
        struct {
            uint8_t event_type;
            uint32_t timestamp;
            uint8_t mode;
        } ev = { ST_EVT_FIRST_TRIGGER, g_st_scheduler.first_trigger_time, delivery_mode };
        tsdb_event_append(0x90, &ev, sizeof(ev));

        // 简化模式：直接等待整点开始周期循环
        g_st_scheduler.first_trigger_done = 1;
        g_st_scheduler.first_delivery_done = 1;  // 标记为已处理，不执行首次送样
        g_st_scheduler.cycle_started = 1;
        g_st_scheduler.cycle_idx = 0xFFFFFFFF;  // 启动阶段
        g_st_scheduler.active_bucket = 0;  // ★ 确保首次触发使用A桶
        g_st_scheduler.sample_done_mask = 0;  // 清零采样完成掩码

        printf("[开关量调度] 首次触发完成，等待整点开始周期循环\r\n");

        return;
    }

    // 5. 时间不够情况已移除瞬时送样和满量采样逻辑
    // 时间不够时直接进入周期循环，等待第一个采样时间点的窗口检测

    // 6. 等待开关量恢复处理
    if (g_st_scheduler.waiting_switch_resume) {
        if (g_st_scheduler.switch_signal_received) {
            // 收到开关量信号，重新启动
            g_st_scheduler.waiting_switch_resume = 0;
            g_st_scheduler.sampling_stopped = 0;
            g_st_scheduler.first_trigger_done = 0;  // 重新作为首次触发处理
            g_st_scheduler.switch_signal_received = 0;  // 清除信号，等待新的首次触发

            uint8_t resume_bucket = 0;
            if (g_State.SaveWarterA > 0 && g_State.SaveWarterB > 0) {
                resume_bucket = g_st_scheduler.active_bucket ^ 1;
            }

            struct {
                uint8_t event_type;
                uint32_t timestamp;
                uint8_t bucket_id;
                uint8_t resume_reason;
            } ev = { ST_EVT_SWITCH_RESUME, now_sec, resume_bucket, 1 };
            tsdb_event_append(0x90, &ev, sizeof(ev));

            printf("[开关量调度] 开关量恢复，重新启动采样，桶=%c\r\n", resume_bucket ? 'B' : 'A');
        }
        return;
    }

    // 7. 周期循环处理（复用时间等比逻辑）
    if (g_st_scheduler.sampling_stopped) {
        return;  // 采样已停止，等待恢复
    }

    // ★ 周期索引计算必须在首次触发完成后才能进行，否则cycle_start_hour为0
    if (!g_st_scheduler.first_trigger_done) {
        return;  // 等待首次触发完成
    }

    // ★ 检查是否已到达周期起点（首次触发后，周期起点是送样后的下一个整点）
    // 如果还没到周期起点，保持在启动阶段
    uint32_t cycle_start_sec_total = (uint32_t)g_st_scheduler.cycle_start_hour * 3600;
    uint32_t current_sec_total = (uint32_t)calendar.hour * 3600 + (uint32_t)calendar.min * 60 + (uint32_t)calendar.sec;

    // ★ 考虑跨天情况：如果周期起点小于当前时间（差值较大），说明是昨天的周期起点，需要加24小时
    // 但如果周期起点大于当前时间，说明是今天的周期起点，不需要加
    if (cycle_start_sec_total < current_sec_total) {
        // 检查是否是昨天的时间（差值小于12小时认为是同一天，大于12小时认为是跨天）
        uint32_t diff = current_sec_total - cycle_start_sec_total;
        if (diff > 12 * 3600) {
            cycle_start_sec_total += 24 * 3600;  // 昨天的周期起点，加24小时变成今天的
        }
    }

    uint32_t cycle_idx;
    if (current_sec_total < cycle_start_sec_total) {
        // 还没到周期起点，保持在启动阶段
        cycle_idx = 0xFFFFFFFF;  // 启动阶段
    } else {
        // 已到达周期起点，计算周期索引
        uint32_t seconds_since_start = current_sec_total - cycle_start_sec_total;
        uint32_t minutes_since_start = seconds_since_start / 60;
        cycle_idx = minutes_since_start / g_SampleConfig.CycleTime;

        // ★ 安全检查：如果计算出的周期索引异常大（>100），说明周期起点计算有误，保持在启动阶段
        if (cycle_idx > 100) {
            printf("[开关量调度] 警告：周期索引异常大(%lu)，可能是周期起点计算错误，保持在启动阶段\r\n",
                   (unsigned long)cycle_idx);
            cycle_idx = 0xFFFFFFFF;  // 保持在启动阶段
        }
    }

    // 周期切换检测
    // ★ 注意：从启动阶段（0xFFFFFFFF）切换到周期0时：
    //    - 如果时间不足模式（瞬时送样启动），首周期切换到B桶
    //    - 否则保持A桶
    if (cycle_idx != g_st_scheduler.cycle_idx && cycle_idx != 0xFFFFFFFF) {
        // 检查是否是从启动阶段切换到周期0
        uint8_t is_first_cycle = (g_st_scheduler.cycle_idx == 0xFFFFFFFF && cycle_idx == 0);

        g_st_scheduler.cycle_idx = cycle_idx;

        if (!is_first_cycle) {
            // 非首次周期才切换桶
            g_st_scheduler.active_bucket ^= 1;  // AB桶切换
        } else {
            // 首次周期：根据启动模式决定桶
            if (g_st_scheduler.first_delivery_mode == 1) {
                // 时间不足模式（瞬时送样启动）：切换到B桶
                g_st_scheduler.active_bucket = 1;
                printf("[开关量调度] 首周期使用B桶（瞬时送样启动模式）\r\n");
            } else {
                // 时间充足模式：保持A桶
                g_st_scheduler.active_bucket = 0;
                printf("[开关量调度] 首周期保持A桶（启动阶段已使用A桶）\r\n");
            }
        }

        g_st_scheduler.sample_done_mask = 0;
        g_st_scheduler.delivery_done = 0;
        g_st_scheduler.total_cycles++;

        printf("[开关量调度] 新周期开始：周期=%lu, 桶=%c\r\n",
               (unsigned long)cycle_idx, g_st_scheduler.active_bucket ? 'B' : 'A');
    }

    // 8. 采样时间点检测和窗口判断
    // ★ 如果还在启动阶段（cycle_idx == 0xFFFFFFFF），执行启动阶段采样检测
    // ★ 必须在首次触发完成后才能进入启动阶段窗口检测，否则first_trigger_time为0导致窗口计算错误
    if (cycle_idx == 0xFFFFFFFF && g_st_scheduler.first_trigger_done) {
        // ========== 启动阶段采样检测 ==========

        if (g_st_scheduler.first_delivery_mode == 0) {
            // 【时间充足模式】使用计算好的采样间隔，窗口检测触发采样
            // 检查是否到达下次采样时间
            if (now_sec >= g_st_scheduler.next_startup_sample_time) {
                // 查找下一个未完成的采样
                uint8_t next_sample_idx = 0xFF;
                for (uint8_t i = 0; i < g_st_scheduler.sample_count; i++) {
                    uint32_t mask = (1u << i);
                    if ((g_st_scheduler.sample_done_mask & mask) == 0) {
                        next_sample_idx = i;
                        break;
                    }
                }

                // 如果找到未完成的采样
                if (next_sample_idx != 0xFF) {
                    // 计算采样时间点（使用采样间隔计算采样时间点）
                    uint32_t sample_time = g_st_scheduler.first_trigger_time +
                                           next_sample_idx * g_st_scheduler.startup_sample_interval_sec;

                    // 直接计算窗口时间（采样时间点前后1分钟）
                    uint32_t window_start = sample_time - 60;  // 前1分钟
                    uint32_t window_end = sample_time + 60;    // 后1分钟

                    // 检查是否在窗口时间内
                    if (now_sec >= window_start && now_sec <= window_end) {
                        if (!g_st_scheduler.window_checking[next_sample_idx]) {
                            // 首次进入窗口，初始化窗口状态
                            g_st_scheduler.window_checking[next_sample_idx] = 1;
                            g_st_scheduler.window_triggered[next_sample_idx] = 0;
                            g_st_scheduler.window_start_time[next_sample_idx] = window_start;
                            g_st_scheduler.window_end_time[next_sample_idx] = window_end;

                            struct {
                                uint8_t event_type;
                                uint8_t sample_idx;
                                uint32_t window_start;
                                uint32_t window_end;
                            } ev = { ST_EVT_WINDOW_START, next_sample_idx, window_start, window_end };
                            tsdb_event_append(0x90, &ev, sizeof(ev));
                        }

                        // 窗口内检测GPIO低电平
                        if (!g_st_scheduler.window_triggered[next_sample_idx]) {
                            extern uint8_t read_trigger_sampling_signal(void);
                            uint8_t gpio_state = read_trigger_sampling_signal();

                            // 检测到低电平（触发状态）
                            if (gpio_state == 0) {
                                g_st_scheduler.window_triggered[next_sample_idx] = 1;

                                rtc_time_get();
                                printf("[%02d:%02d:%02d][开关量调度] 采样 #%d 检测到触发信号（GPIO低电平）\r\n",
                                       calendar.hour, calendar.min, calendar.sec, next_sample_idx + 1);

                                struct {
                                    uint8_t event_type;
                                    uint8_t sample_idx;
                                    uint32_t trigger_time;
                                } ev = { ST_EVT_SWITCH_TRIGGER, next_sample_idx, now_sec };
                                tsdb_event_append(0x90, &ev, sizeof(ev));
                            }
                        }

                        // 如果已触发且采样未完成，执行采样
                        uint32_t mask = (1u << next_sample_idx);
                        if (g_st_scheduler.window_triggered[next_sample_idx] &&
                                (g_st_scheduler.sample_done_mask & mask) == 0) {
                            // 检查采样是否空闲
                            if (sampling_get_status() == 0) {
                                uint8_t result = sampling_start(0, g_SampleConfig.SampleVolume, 0, 0);  // A桶
                                if (result) {
                                    g_st_scheduler.sample_done_mask |= mask;
                                    g_st_scheduler.total_samples++;
                                    g_st_scheduler.window_triggered[next_sample_idx] = 0;  // 清除触发标记

                                    rtc_time_get();
                                    printf("[%02d:%02d:%02d][开关量调度] 启动采样 #%d 已触发：桶=A (窗口检测，立即启动)\r\n",
                                           calendar.hour, calendar.min, calendar.sec, next_sample_idx + 1);

                                    struct {
                                        uint8_t event_type;
                                        uint8_t sample_idx;
                                        uint8_t bucket_id;
                                        uint32_t timestamp;
                                    } ev = { ST_EVT_SAMPLE_EXECUTE, next_sample_idx, 0, now_sec };
                                    tsdb_event_append(0x90, &ev, sizeof(ev));
                                }
                            }
                        }
                    }

                    // 更新下次采样时间
                    g_st_scheduler.next_startup_sample_time = now_sec + g_st_scheduler.startup_sample_interval_sec;
                }
            }
        }
    } else if (g_st_scheduler.first_trigger_done) {
        // 已进入周期循环，执行采样时间点检测
        // ★ 必须在首次触发完成后才能进入，否则cycle_start_hour为0导致窗口计算错误
        for (uint8_t i = 0; i < g_st_scheduler.sample_count; i++) {
            uint16_t sample_offset = g_st_scheduler.sample_offsets[i];
            uint32_t mask = 1u << i;

            // 检查采样是否已完成
            if (g_st_scheduler.sample_done_mask & mask) {
                continue;
            }

            // 计算采样时间点（相对于周期起点）
            uint32_t cycle_start_sec_total = (uint32_t)g_st_scheduler.cycle_start_hour * 3600;

            // 计算窗口时间
            uint32_t window_start, window_end;
            st_calculate_window(sample_offset, cycle_start_sec_total, &window_start, &window_end);

            // 窗口开始检测：如果当前时间在窗口内但window_checking还未设置，也要设置它
            if (now_sec >= window_start && now_sec <= window_end) {
                if (!g_st_scheduler.window_checking[i]) {
                    // 首次进入窗口，初始化窗口状态
                    g_st_scheduler.window_checking[i] = 1;
                    g_st_scheduler.window_triggered[i] = 0;
                    g_st_scheduler.window_start_time[i] = window_start;
                    g_st_scheduler.window_end_time[i] = window_end;

                    struct {
                        uint8_t event_type;
                        uint8_t sample_idx;
                        uint32_t window_start;
                        uint32_t window_end;
                    } ev = { ST_EVT_WINDOW_START, i, window_start, window_end };
                    tsdb_event_append(0x90, &ev, sizeof(ev));
                }

                // ★★ 窗口内检测GPIO低电平（只检测一次）
                if (!g_st_scheduler.window_triggered[i]) {
                    // 直接读取GPIO状态
                    extern uint8_t read_trigger_sampling_signal(void);
                    uint8_t gpio_state = read_trigger_sampling_signal();

                    // 检测到低电平（触发状态）
                    if (gpio_state == 0) {
                        g_st_scheduler.window_triggered[i] = 1;

                        rtc_time_get();
                        printf("[%02d:%02d:%02d][开关量调度] 周期循环窗口 #%d 检测到触发信号（GPIO低电平）\r\n",
                               calendar.hour, calendar.min, calendar.sec, i + 1);

                        struct {
                            uint8_t event_type;
                            uint8_t sample_idx;
                            uint32_t trigger_time;
                        } ev = { ST_EVT_WINDOW_TRIGGERED, i, now_sec };
                        tsdb_event_append(0x90, &ev, sizeof(ev));
                    }
                }

                // 窗口内已触发且采样未完成，立即检查并启动采样
                if (g_st_scheduler.window_triggered[i] &&
                        (g_st_scheduler.sample_done_mask & mask) == 0) {
                    // 检查采样是否空闲
                    if (sampling_get_status() == 0) {
                        uint8_t bucket = g_st_scheduler.active_bucket;
                        uint8_t result = sampling_start(bucket, g_SampleConfig.SampleVolume, 0, 0);

                        if (result) {
                            g_st_scheduler.sample_done_mask |= mask;
                            g_st_scheduler.total_samples++;
                            g_st_scheduler.window_triggered[i] = 0;  // 清除触发标志

                            rtc_time_get();
                            printf("[%02d:%02d:%02d][开关量调度] 周期采样 #%d 已触发：桶=%c (窗口检测，立即启动)\r\n",
                                   calendar.hour, calendar.min, calendar.sec,
                                   i + 1, bucket ? 'B' : 'A');

                            struct {
                                uint8_t event_type;
                                uint8_t sample_idx;
                                uint8_t bucket_id;
                                uint32_t timestamp;
                            } ev = { ST_EVT_SAMPLE_EXECUTE, i, bucket, now_sec };
                            tsdb_event_append(0x90, &ev, sizeof(ev));
                        }
                    }
                }
            }
        }
    }
    
    // 首次送样触发检测
    if (calendar.sec == 0 && !g_st_scheduler.first_delivery_done) {
        // ★ 跳过启动阶段模式：不执行首次送样
        if (g_st_scheduler.first_delivery_mode == 2) {
            printf("[开关量调度] 跳过启动阶段模式：不执行首次送样，等待周期循环\r\n");
            g_st_scheduler.first_delivery_done = 1;  // 标记为已处理
            return;
        }

        // 【时间充足模式】检查所有采样是否完成
        uint32_t expected_mask = (1u << g_st_scheduler.sample_count) - 1;
        if (g_st_scheduler.sample_done_mask != expected_mask) {
            printf("[开关量调度] 警告：启动采样未完成，跳过首次送样\r\n");
            g_st_scheduler.first_delivery_done = 1;  // 标记为已处理，避免重复触发
            return;
        }

        #define MINIMUM_DELIVERY_WATER_ST 50  // 最小送样量50ml
        uint16_t current_water = g_State.SaveWarterA;

        // ★ 修改：只要有水就送，目标量=当前水量（与TP模式一致）
        if (current_water >= MINIMUM_DELIVERY_WATER_ST) {
            if (delivery_start(0, current_water, 0)) {
                g_st_scheduler.first_delivery_done = 1;
                g_st_scheduler.total_deliveries++;
                notify_task4_delivery_complete(0);
            }
        }
    }
    
    // 周期送样触发检测
    if (calendar.sec == 0 && g_st_scheduler.cycle_started && g_st_scheduler.first_trigger_done) {
        // ★ 必须在首次触发完成后才能进入，否则cycle_start_hour为0导致计算错误
        uint16_t cycle_hours = g_SampleConfig.CycleTime / 60;
        uint8_t cycle_end_hour = (g_st_scheduler.cycle_start_hour +
                                  (g_st_scheduler.cycle_idx + 1) * cycle_hours) % 24;
        uint8_t delivery_hour = (cycle_end_hour + 24 - 1) % 24;

        if (calendar.hour == delivery_hour &&
                calendar.min == g_st_scheduler.configured_delivery_min &&
                !g_st_scheduler.delivery_done) {
            uint8_t bucket = g_st_scheduler.active_bucket;
            uint16_t current_water = bucket ? g_State.SaveWarterB : g_State.SaveWarterA;

            // ★ 修改：只要有水就送，目标量=当前水量（与TP模式一致）
            if (current_water >= MINIMUM_DELIVERY_WATER_ST) {
                if (delivery_start(bucket, current_water, 0)) {
                    g_st_scheduler.delivery_done = 1;
                    g_st_scheduler.total_deliveries++;
                    notify_task4_delivery_complete(bucket);
                }
            }
        }
    }
}


uint8_t st_scheduler_is_running(void) {
    return g_st_scheduler.is_running;
}

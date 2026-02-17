#include "Timetrigger.h"
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

static FixedTimeSchedulerState g_fixed_time_scheduler = {0};

//==============================================================================
// 定时采样调度器实现
//==============================================================================

/**
 * @brief 计算采样开始小时（考虑周期长度）
 * @param delivery_hour 送样小时
 * @param delivery_min 送样分钟
 * @param cycle_time 周期时间（分钟）
 * @return 采样开始小时
 */

/**
 * @brief 初始化定时采样调度器
 */
void fixed_time_scheduler_init(void) {
    // ★ 防止重复初始化
    if (g_fixed_time_scheduler.is_initialized && g_fixed_time_scheduler.is_running) {
        return;
    }

    memset(&g_fixed_time_scheduler, 0, sizeof(g_fixed_time_scheduler));

    // 参数验证
    if (g_SampleConfig.CycleTime == 0 || g_SampleConfig.SampleInterval == 0) {
        return;
    }

    // 1. 解析有效送样时间点
    g_fixed_time_scheduler.delivery_min = g_DeliveryConfig.fixedmin;
    g_fixed_time_scheduler.delivery_count = 0;

    // 遍历fixedhour数组，收集有效送样小时
    for (uint8_t i = 0; i < 24; i++) {
        if (g_DeliveryConfig.fixedhour[i] == 1) {
            g_fixed_time_scheduler.delivery_hours[i] = 1;
            g_fixed_time_scheduler.delivery_indices[g_fixed_time_scheduler.delivery_count] = i;
            g_fixed_time_scheduler.delivery_count++;
        } else {
            g_fixed_time_scheduler.delivery_hours[i] = 0;
        }
    }

    // 检查是否有有效送样时间点
    if (g_fixed_time_scheduler.delivery_count == 0) {
        return;
    }

    // 2. 保存周期参数
    g_fixed_time_scheduler.cycle_time = g_SampleConfig.CycleTime;
    g_fixed_time_scheduler.sample_interval = g_SampleConfig.SampleInterval;

    // 3. 计算采样次数
    g_fixed_time_scheduler.sample_count = g_fixed_time_scheduler.cycle_time / g_fixed_time_scheduler.sample_interval;
    if (g_fixed_time_scheduler.sample_count > 24) g_fixed_time_scheduler.sample_count = 24;

    // 4. 计算采样时间点偏移
    for (uint8_t i = 0; i < g_fixed_time_scheduler.sample_count; i++) {
        g_fixed_time_scheduler.sample_offsets[i] = i * g_fixed_time_scheduler.sample_interval;
    }

    // 5. 初始化状态
    g_fixed_time_scheduler.current_delivery_idx = 0xFF;  // 未设置（0xFF表示无效索引）
    g_fixed_time_scheduler.current_delivery_hour = 0xFF;  // 未设置（0xFF表示无效）
    g_fixed_time_scheduler.active_bucket = 0;  // 从A桶开始
    g_fixed_time_scheduler.last_check_day = 0;  // 日期检测初始化

    // 记录TSDB事件：调度器初始化
    struct {
        uint8_t event_type;
        uint8_t delivery_count;
        uint8_t fixedmin;
        uint16_t cycle_time;
        uint16_t interval;
    } ev = {
        FIXED_TIME_EVT_INIT,
        g_fixed_time_scheduler.delivery_count,
        g_fixed_time_scheduler.delivery_min,
        g_fixed_time_scheduler.cycle_time,
        g_fixed_time_scheduler.sample_interval
    };
    tsdb_event_append(0xA0, &ev, sizeof(ev));

    // 记录TSDB事件：有效送样时间点列表
    struct {
        uint8_t event_type;
        uint8_t count;
        uint8_t hours[24];
    } ev_list = { FIXED_TIME_EVT_DELIVERY_LIST, g_fixed_time_scheduler.delivery_count };
    for (uint8_t i = 0; i < 24; i++) {
        ev_list.hours[i] = g_fixed_time_scheduler.delivery_hours[i];
    }
    tsdb_event_append(0xA0, &ev_list, sizeof(ev_list));

    g_fixed_time_scheduler.is_initialized = 1;
}
/**
 * @brief 启动定时采样调度器
 */
void fixed_time_scheduler_start(void) {
    if (!g_fixed_time_scheduler.is_initialized) {
        fixed_time_scheduler_init();
    }

    if (g_fixed_time_scheduler.delivery_count == 0) {
        printf("[定时采样调度] 无法启动：没有有效送样时间点\r\n");
        return;
    }

    g_fixed_time_scheduler.is_running = 1;
    g_fixed_time_scheduler.current_delivery_idx = 0xFF;  // 未设置，等待查找（0xFF表示无效索引）

    // 记录TSDB事件
    struct {
        uint8_t event_type;
        uint32_t timestamp;
    } ev = { FIXED_TIME_EVT_START, rtc_counter_get() };
    tsdb_event_append(0xA0, &ev, sizeof(ev));

    printf("[定时采样调度] 定时采样调度器已启动\r\n");
}

/**
 * @brief 停止定时采样调度器
 */
void fixed_time_scheduler_stop(void) {
    g_fixed_time_scheduler.is_running = 0;
    printf("[定时采样调度] 定时采样调度器已停止\r\n");
}
/**
 * @brief 定时采样调度器主函数（task3周期调用）
 */
static uint8_t find_current_delivery_period(uint8_t current_hour, uint8_t current_min,
                                             uint8_t *delivery_hour, uint8_t *delivery_min,
                                             uint8_t *cycle_start_hour) {
    if (g_fixed_time_scheduler.delivery_count == 0) {
        return 0;
    }
    // Case 1: still before today's delivery minute at a configured hour
    if (g_DeliveryConfig.fixedhour[current_hour] == 1 && current_min < g_fixed_time_scheduler.delivery_min) {
        *delivery_hour = current_hour;
        *delivery_min = g_fixed_time_scheduler.delivery_min;
        *cycle_start_hour = (*delivery_min >= 50) ? current_hour : (uint8_t)((current_hour + 23) % 24);
        return 1;
    }
    // Case 2: find the next configured hour later today
    for (uint8_t i = 0; i < g_fixed_time_scheduler.delivery_count; i++) {
        uint8_t hour = g_fixed_time_scheduler.delivery_indices[i];
        if (hour > current_hour || (hour == current_hour && g_fixed_time_scheduler.delivery_min > current_min)) {
            *delivery_hour = hour;
            *delivery_min = g_fixed_time_scheduler.delivery_min;
            *cycle_start_hour = (*delivery_min >= 50) ? hour : (uint8_t)((hour + 23) % 24);
            return 1;
        }
    }
    // Case 3: wrap to the first configured hour of next day
    *delivery_hour = g_fixed_time_scheduler.delivery_indices[0];
    *delivery_min = g_fixed_time_scheduler.delivery_min;
    *cycle_start_hour = (*delivery_min >= 50) ? *delivery_hour : (uint8_t)((*delivery_hour + 23) % 24);
    return 1;
}

void scheduler_fixed_time(void) {
    // 1. 检查运行状态
    if (!g_fixed_time_scheduler.is_running) {
        return;
    }

    // 2. 读取当前时间
    rtc_time_get();
    uint8_t current_hour = calendar.hour;
    uint8_t current_min = calendar.min;
    uint32_t now_sec = rtc_counter_get();

    // ★ 新增：日期检测，跨天时强制重置状态
    uint8_t current_day = calendar.date;
    if (g_fixed_time_scheduler.last_check_day != 0 &&
        g_fixed_time_scheduler.last_check_day != current_day) {
        // 跨天：重置掩码和送样状态
        g_fixed_time_scheduler.sample_done_mask = 0;
        g_fixed_time_scheduler.current_delivery_done = 0;
        g_fixed_time_scheduler.current_delivery_hour = 0xFF;  // 强制触发周期切换
        printf("[定时采样调度] 检测到跨天，重置状态\r\n");
    }
    g_fixed_time_scheduler.last_check_day = current_day;

    // 3. 查找当前送样周期
    uint8_t delivery_hour, delivery_min, cycle_start_hour;
    if (!find_current_delivery_period(current_hour, current_min, &delivery_hour, &delivery_min,&cycle_start_hour)) {
        return;  // 没有有效送样时间点
    }

    // 4. 周期切换检测
    if (g_fixed_time_scheduler.current_delivery_idx == 0xFF ||
            g_fixed_time_scheduler.current_delivery_hour != delivery_hour) {
        // 切换到新周期
        uint8_t old_hour = g_fixed_time_scheduler.current_delivery_hour;
        g_fixed_time_scheduler.current_delivery_hour = delivery_hour;
        g_fixed_time_scheduler.cycle_start_hour = cycle_start_hour;

        // 查找delivery_indices中的索引
        for (uint8_t i = 0; i < g_fixed_time_scheduler.delivery_count; i++) {
            if (g_fixed_time_scheduler.delivery_indices[i] == delivery_hour) {
                g_fixed_time_scheduler.current_delivery_idx = i;
                break;
            }
        }

        // AB桶切换（首次切换时不执行，只在新周期切换时执行）
        if (g_fixed_time_scheduler.current_delivery_idx != 0xFF &&
                old_hour != 0xFF) {
            g_fixed_time_scheduler.active_bucket ^= 1;  // AB桶切换

            // 记录TSDB事件：周期切换
            struct {
                uint8_t event_type;
                uint8_t old_hour;
                uint8_t new_hour;
                uint8_t active_bucket;
            } ev_switch = { FIXED_TIME_EVT_CYCLE_SWITCH, old_hour, delivery_hour,
                            g_fixed_time_scheduler.active_bucket
                          };
            tsdb_event_append(0xA0, &ev_switch, sizeof(ev_switch));
        }

        g_fixed_time_scheduler.sample_done_mask = 0;
        g_fixed_time_scheduler.current_delivery_done = 0;
        g_fixed_time_scheduler.total_cycles++;

        // 记录TSDB事件：送样周期开始
        struct {
            uint8_t event_type;
            uint8_t delivery_hour;
            uint8_t delivery_min;
            uint8_t bucket_id;
            uint8_t cycle_start_hour;
        } ev = { FIXED_TIME_EVT_CYCLE_START, delivery_hour, delivery_min,
                 g_fixed_time_scheduler.active_bucket, cycle_start_hour
               };
        tsdb_event_append(0xA0, &ev, sizeof(ev));

        rtc_time_get();
        printf("[%02d:%02d:%02d][定时采样调度] 新周期开始：送样时间=%02d:%02d, 桶=%c, 采样开始小时=%02d\r\n",calendar.hour, calendar.min, calendar.sec, delivery_hour, delivery_min, g_fixed_time_scheduler.active_bucket ? 'B' : 'A',cycle_start_hour);
    }

    // 5. 采样时间点检测
    if (!g_fixed_time_scheduler.current_delivery_done) {
        for (uint8_t i = 0; i < g_fixed_time_scheduler.sample_count; i++) {
            uint32_t mask = 1u << i;
            if (g_fixed_time_scheduler.sample_done_mask & mask) {
                continue;  // 已采样
            }

            uint8_t sample_hour = g_fixed_time_scheduler.cycle_start_hour;
            uint8_t sample_min = g_fixed_time_scheduler.sample_offsets[i];

            // 处理跨小时情况
            if (sample_min >= 60) {
                sample_hour = (sample_hour + 1) % 24;
                sample_min = sample_min % 60;
            }

            // 检查是否到达采样时间点
            if (calendar.hour == sample_hour && calendar.min == sample_min && calendar.sec == 0) {
                uint8_t bucket = g_fixed_time_scheduler.active_bucket;

                // 检查是否有采样/送样正在运行
                if (sampling_get_status() == 1 || delivery_get_status() == 1) {
                    // 状态机正在运行，跳过本次采样
                    printf("[定时采样调度] 采样跳过：状态机正在运行\r\n");
                    continue;
                }

                uint8_t result = sampling_start(bucket, g_SampleConfig.SampleVolume, 0, 0);

                if (result) {
                    g_fixed_time_scheduler.sample_done_mask |= mask;
                    g_fixed_time_scheduler.total_samples++;

                    // 记录TSDB事件：采样触发
                    struct {
                        uint8_t event_type;
                        uint8_t sample_idx;
                        uint8_t bucket_id;
                        uint32_t timestamp;
                        uint8_t offset_min;
                    } ev = { FIXED_TIME_EVT_SAMPLE_TRIGGER, i, bucket, now_sec,
                             (uint8_t)g_fixed_time_scheduler.sample_offsets[i]
                           };
                    tsdb_event_append(0xA0, &ev, sizeof(ev));

                    rtc_time_get();
                    printf("[%02d:%02d:%02d][定时采样调度] 采样触发：样本 #%d, 桶=%c, 时间=%02d:%02d\r\n",
                           calendar.hour, calendar.min, calendar.sec,
                           i + 1, bucket ? 'B' : 'A', sample_hour, sample_min);
                }
            }
        }

        // 采样完成事件在采样触发时已记录，这里不需要额外处理
    }

    // 6. 送样时间点检测
    if (calendar.hour == delivery_hour &&
            calendar.min == delivery_min &&
            calendar.sec == 0 &&
            !g_fixed_time_scheduler.current_delivery_done) {

        uint8_t bucket = g_fixed_time_scheduler.active_bucket;
        uint16_t current_water = bucket ? g_State.SaveWarterB : g_State.SaveWarterA;
        #define MINIMUM_DELIVERY_WATER_FT 50  // 最小送样量50ml

        // 记录TSDB事件：送样触发
        struct {
            uint8_t event_type;
            uint8_t delivery_hour;
            uint8_t delivery_min;
            uint8_t bucket_id;
            uint32_t timestamp;
        } ev_trigger = { FIXED_TIME_EVT_DELIVERY_TRIGGER, delivery_hour, delivery_min, bucket, now_sec };
        tsdb_event_append(0xA0, &ev_trigger, sizeof(ev_trigger));

        // ★ 修改：只要有水就送，目标量=当前水量（与TP模式一致）
        if (current_water >= MINIMUM_DELIVERY_WATER_FT) {
            if (delivery_start(bucket, current_water, 0)) {
                g_fixed_time_scheduler.current_delivery_done = 1;
                g_fixed_time_scheduler.total_deliveries++;
                notify_task4_delivery_complete(bucket);

                // 记录TSDB事件：送样完成
                struct {
                    uint8_t event_type;
                    uint8_t delivery_hour;
                    uint8_t delivery_min;
                    uint8_t bucket_id;
                    uint32_t timestamp;
                    uint8_t result;
                } ev_complete = { FIXED_TIME_EVT_DELIVERY_COMPLETE, delivery_hour, delivery_min,
                                  bucket, now_sec, 1
                                };
                tsdb_event_append(0xA0, &ev_complete, sizeof(ev_complete));

                rtc_time_get();
                printf("[%02d:%02d:%02d][定时采样调度] 送样触发：桶=%c, 时间=%02d:%02d\r\n",
                       calendar.hour, calendar.min, calendar.sec,
                       bucket ? 'B' : 'A', delivery_hour, delivery_min);
            }
        } else {
            // 水量不足，跳过送样
            g_fixed_time_scheduler.current_delivery_done = 1;  // 标记为已处理

            struct {
                uint8_t event_type;
                uint8_t delivery_hour;
                uint8_t reason;
            } ev_skip = { FIXED_TIME_EVT_DELIVERY_SKIP, delivery_hour, 1 };  // 1=水量不足
            tsdb_event_append(0xA0, &ev_skip, sizeof(ev_skip));

            rtc_time_get();
            printf("[%02d:%02d:%02d][定时采样调度] 送样跳过：水量不足（%u < %d ml）\r\n",
                   calendar.hour, calendar.min, calendar.sec, current_water, MINIMUM_DELIVERY_WATER_FT);
        }
    }
}

/**
 * @brief 检查定时采样调度器是否正在运行
 * @return 1=运行中, 0=未运行
 */
uint8_t fixed_time_scheduler_is_running(void) {
    return g_fixed_time_scheduler.is_running;
}

/**
 * @brief 如果定时采样调度器正在运行，则重新初始化
 */
void fixed_time_scheduler_reinit_if_running(void) {
    if (!g_fixed_time_scheduler.is_running) {
        return;
    }

    printf("[定时采样调度] 配置变更，重新初始化调度器\r\n");

    // 保存运行状态
    uint8_t was_running = g_fixed_time_scheduler.is_running;

    // 停止并重新初始化
    g_fixed_time_scheduler.is_running = 0;
    g_fixed_time_scheduler.is_initialized = 0;

    fixed_time_scheduler_init();

    // 恢复运行状态
    if (was_running && g_fixed_time_scheduler.is_initialized) {
        fixed_time_scheduler_start();
    }
}


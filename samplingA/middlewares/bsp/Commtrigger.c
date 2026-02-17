#include "Commtrigger.h"
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

CommTriggerRequest g_comm_trigger_request = {
    .request_type = COMM_REQ_NONE,
    .bucket_selector = 2,
    .volume = 0,
    .pending = 0
};

CommTriggerSchedulerState g_comm_scheduler_state = {
    .active_bucket = 0,
    .is_initialized = 0,
    .last_delivery_time = 0,
    .cycle_count = 0,
    .total_samples = 0
};

//==============================================================================
// 通讯触发调度器实现
//==============================================================================

/**
 * @brief 初始化通讯触发调度器状态
 */
static void init_comm_trigger_scheduler(void) {
    if (!g_comm_scheduler_state.is_initialized) {
        g_comm_scheduler_state.is_initialized = 1;
        g_comm_scheduler_state.last_delivery_time = 0;
        g_comm_scheduler_state.total_samples = 0;

        // 启动时总是从A桶开始，记录当前周期编号用于后续切换检测
        g_comm_scheduler_state.active_bucket = 0;  // 固定从A桶开始
        rtc_time_get();
        if (g_SampleConfig.CycleTime > 0) {
            uint32_t current_minutes = calendar.hour * 60 + calendar.min;
            g_comm_scheduler_state.cycle_count = current_minutes / g_SampleConfig.CycleTime;
        } else {
            g_comm_scheduler_state.cycle_count = 0;
        }
    }
}

/**
 * @brief 通讯触发调度器（由task3周期性调用）
 *
 * 功能：
 * - 检查通讯触发请求标志
 * - 根据请求类型执行采样/排空/送样操作
 * - 清除已处理的请求标志
 * - 实现AB桶轮换机制（每4次采样后切换桶）
 */
void scheduler_comm_trigger(void) {
    // 1. 检查系统运行状态（通讯触发只要系统在运行就响应）
    if (g_State.State != 1) {
        return;
    }

    // 2. 初始化
    init_comm_trigger_scheduler();

    // 3. 检查是否有待处理的请求
    if (!g_comm_trigger_request.pending) {
        return;  // 无请求
    }

    // 4. 根据请求类型处理
    switch (g_comm_trigger_request.request_type) {
    case COMM_REQ_SAMPLING:
        // 采样请求：使用当前活跃桶
    {
        uint16_t volume = g_comm_trigger_request.volume;

        // AB自动模式：基于时间周期整点切换桶
        if (g_comm_trigger_request.bucket_selector == 2 && g_SampleConfig.CycleTime > 0) {
            rtc_time_get();
            // 计算当前时间所属的周期编号（从0点开始，按CycleTime分钟划分）
            uint32_t current_minutes = calendar.hour * 60 + calendar.min;
            uint32_t current_cycle = current_minutes / g_SampleConfig.CycleTime;

            // 检查是否进入新周期（与上次记录的周期不同）
            if (current_cycle != g_comm_scheduler_state.cycle_count) {
                uint8_t old_bucket = g_comm_scheduler_state.active_bucket;
                g_comm_scheduler_state.active_bucket = 1 - old_bucket;  // 0→1, 1→0
                g_comm_scheduler_state.cycle_count = current_cycle;

                printf("[%02d:%02d:%02d][通讯调度] 周期整点到达（周期%d分钟），桶切换: %c → %c (周期#%lu)\r\n",
                       calendar.hour, calendar.min, calendar.sec,
                       g_SampleConfig.CycleTime,
                       old_bucket ? 'B' : 'A',
                       g_comm_scheduler_state.active_bucket ? 'B' : 'A',
                       g_comm_scheduler_state.cycle_count);
            }
        }

        uint8_t bucket = g_comm_scheduler_state.active_bucket;

        // 如果指定了具体桶（不是自动），则使用指定桶
        if (g_comm_trigger_request.bucket_selector == 0) {
            bucket = 0;  // 强制使用A桶
            printf("[通讯调度] 强制使用A桶采样（覆盖轮换逻辑）\r\n");
        } else if (g_comm_trigger_request.bucket_selector == 1) {
            bucket = 1;  // 强制使用B桶
            printf("[通讯调度] 强制使用B桶采样（覆盖轮换逻辑）\r\n");
        } else {
            // bucket_selector == 2：AB自动轮换
            printf("[通讯调度] 使用当前活跃桶: %c\r\n",
                   bucket ? 'B' : 'A');
        }

        // 调用采样函数（非手动模式，跳过桶状态检查）
        uint8_t result = sampling_start(bucket, volume, 0, 1);
        if (result) {
            g_comm_scheduler_state.total_samples++;
            rtc_time_get();
            printf("[%02d:%02d:%02d][通讯调度] 采样已启动: 桶=%c, 体积=%d ml, 累计采样=%lu\r\n",
                   calendar.hour, calendar.min, calendar.sec,
                   bucket ? 'B' : 'A', volume, g_comm_scheduler_state.total_samples);
            
        } else {
            printf("[通讯调度] 采样启动失败: 可能已有流程运行\r\n");
        }
    }
    break;

    case COMM_REQ_DRAIN:
        // 排空请求
    {
        uint8_t bucket_sel = g_comm_trigger_request.bucket_selector;

        if (bucket_sel == 0) {
            // A桶排空 - 检查是否已经空了
            if (g_State.SaveWarterA == 0 && g_State.DrainAComplete == 1) {
                printf("[通讯调度] A桶已空，跳过排空（应答成功）\r\n");
            } else {
                uint8_t result = drain_execute(0);
                printf("[通讯调度] A桶排空: %s\r\n", result ? "成功" : "失败");
            }
        } else if (bucket_sel == 1) {
            // B桶排空 - 检查是否已经空了
            if (g_State.SaveWarterB == 0 && g_State.DrainBComplete == 1) {
                printf("[通讯调度] B桶已空，跳过排空（应答成功）\r\n");
            } else {
                uint8_t result = drain_execute(1);
                printf("[通讯调度] B桶排空: %s\r\n", result ? "成功" : "失败");
            }
        } else {
            // AB自动排空：检查各桶状态后再排空
            if (g_State.SaveWarterA == 0 && g_State.DrainAComplete == 1) {
                printf("[通讯调度] A桶已空，跳过排空\r\n");
            } else {
                drain_execute(0);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            if (g_State.SaveWarterB == 0 && g_State.DrainBComplete == 1) {
                printf("[通讯调度] B桶已空，跳过排空\r\n");
            } else {
                drain_execute(1);
            }
            printf("[通讯调度] AB桶排空完成\r\n");
        }
    }
    break;

    case COMM_REQ_DELIVERY:
        // 送样请求：使用非活跃桶（刚采完的那个桶）
    {
        uint8_t bucket;
        uint16_t volume = 0;

        // 如果指定了具体桶（不是自动），则使用指定桶
        if (g_comm_trigger_request.bucket_selector == 0) {
            bucket = 0;  // 强制使用A桶
            volume = g_State.SaveWarterA;
            printf("[通讯调度] 强制使用A桶送样（覆盖轮换逻辑）\r\n");
        } else if (g_comm_trigger_request.bucket_selector == 1) {
            bucket = 1;  // 强制使用B桶
            volume = g_State.SaveWarterB;
            printf("[通讯调度] 强制使用B桶送样（覆盖轮换逻辑）\r\n");
        } else {
            // bucket_selector == 2：使用活跃桶（有水的桶）
            bucket = g_comm_scheduler_state.active_bucket;
            volume = (bucket == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;
            printf("[通讯调度] 使用活跃桶送样: %c, 水量=%d ml\r\n",
                   bucket ? 'B' : 'A',
                   volume);
        }

        // 验证水量
        if (volume > 0) {
            // 调用送样函数（非手动模式）
            uint8_t result = delivery_start(bucket, volume, 0);
            if (result) {
                rtc_time_get();
                printf("[%02d:%02d:%02d][通讯调度] 送样已启动: 桶=%c, 体积=%d ml\r\n",
                       calendar.hour, calendar.min, calendar.sec,
                       bucket ? 'B' : 'A', volume);

                // 记录送样时间
                g_comm_scheduler_state.last_delivery_time = rtc_counter_get();

                // 通知task4进行留样判定
                notify_task4_delivery_complete(bucket);
            } else {
                printf("[通讯调度] 送样启动失败: 可能水量不足或已有流程运行\r\n");
            }
        } else {
            printf("[通讯调度] 送样失败: 桶水量为0\r\n");
        }
    }
    break;

    default:
        printf("[通讯调度] 未知请求类型: %d\r\n", g_comm_trigger_request.request_type);
        break;
    }

    // 5. 清除请求标志
    g_comm_trigger_request.pending = 0;
    g_comm_trigger_request.request_type = COMM_REQ_NONE;
}

#ifndef __FLOWTRIGGER_H__
#define __FLOWTRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// 流量触发状态机状态定义
typedef enum {
    FT_IDLE = 0,              // 待机状态
    FT_START_INSTANT,         // 瞬时送样阶段
    FT_START_FULL_SAMPLE,     // 满量采样阶段
    FT_WAIT_HOUR,             // 等待周期整点
    FT_CYCLE_RUNNING,         // 周期运行（采样/送样）
    FT_FORCE_STOP_DRAIN       // 强制停机排空（过渡态）
} FlowTriggerState;

// 流量触发事件位定义
#define FT_EVT_FLOW_START  (1u << 0)
#define FT_EVT_FLOW_STOP   (1u << 1)

typedef struct {
    // === 状态机核心 ===
    FlowTriggerState state;           // 当前状态
    uint32_t pending_events;          // 待处理事件位

    // === 基本信息 ===
    uint8_t is_initialized;
    uint32_t flow_start_time;         // 流量开始时间戳

    // === 启动阶段 ===
    uint32_t instant_delivery_end_time;

    // === 周期阶段 ===
    uint8_t cycle_start_hour;         // 周期起点小时
    uint32_t cycle_idx;               // 当前周期索引
    uint8_t active_bucket;            // 当前活跃桶 (0=A, 1=B)
    uint32_t sample_done_mask;        // 采样完成掩码
    uint8_t sample_count;             // 每周期采样次数
    uint16_t sample_offsets[24];      // 采样时间点数组
    uint8_t delivery_done;            // 本周期送样完成标志
    uint8_t configured_delivery_min;  // 配置的送样分钟

    // === 首周期延迟（保留） ===
    uint8_t first_cycle_delayed;
    uint16_t delay_offset_min;

    // === 统计信息 ===
    uint32_t total_cycles;
    uint32_t total_samples;
    uint32_t total_deliveries;

    // === 排空控制 ===
    uint32_t drain_start_time;        // 排空开始时间
    uint8_t drain_in_progress;        // 排空进行中标志
    uint8_t waiting_retention;        // 等待留样完成标志
    uint32_t retention_wait_start;    // 等待留样开始时间
} FlowTriggerSchedulerState;

extern FlowTriggerSchedulerState g_ft_scheduler;

void ft_scheduler_init(void);
void ft_scheduler_start(void);
void ft_scheduler_stop(void);
void scheduler_flow_trigger(void);

// 状态查询接口
uint8_t ft_scheduler_is_idle(void);
FlowTriggerState ft_scheduler_get_state(void);

// 瞬时送样接口
uint8_t instant_delivery_start(void);
uint8_t instant_delivery_update(void);
uint8_t instant_delivery_get_status(void);
void instant_delivery_reset(void);

// 流量通知接口
void flow_trigger_notify_start(uint32_t timestamp);
void flow_trigger_notify_stop(uint32_t timestamp);

// 废弃接口（保留空实现，下版本删除）
void flow_trigger_handle_delivery_complete(uint8_t bucket_id, uint16_t delivered_volume);
void flow_trigger_retention_complete_callback(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLOWTRIGGER_H__ */

/**
 * @file    app_scheduler.h
 * @brief   自动调度器接口（5种采样触发模式）
 */
#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 采样触发模式 */
typedef enum {
    SCHED_MODE_TIME_PROP  = 0,  /* 时间等比 */
    SCHED_MODE_FIXED_TIME = 1,  /* 定时触发 */
    SCHED_MODE_FLOW       = 2,  /* 流量触发 */
    SCHED_MODE_SWITCH     = 3,  /* 开关量触发 */
    SCHED_MODE_COMM       = 4,  /* 通信触发 */
} sched_mode_t;

/* 调度阶段 */
typedef enum {
    PHASE_IDLE    = 0,  /* 空闲 */
    PHASE_STARTUP = 1,  /* 启动阶段 */
    PHASE_CYCLING = 2,  /* 周期循环 */
    PHASE_STOPPED = 3,  /* 已停止(等待恢复) */
} sched_phase_t;

/* ======================== 时间等比增强（S3-1） ======================== */

/* 时间点 */
typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t bucket_id;   /* 0=A桶 1=B桶 0xFF=未指定 */
} TpTimePoint;

/* 操作时段 */
typedef struct {
    TpTimePoint sample_time;
    TpTimePoint delivery_time;
    int16_t     delay_offset_min;  /* 推迟偏移(分钟) */
    uint8_t     is_valid;
} TpOperationSlot;

/* 全天时间表 */
#define TP_MAX_SLOTS  48   /* 每桶最多48个采样时段 */
typedef struct {
    uint8_t       cycle_count;
    TpTimePoint   cycle_start_times[24];
    TpTimePoint   delivery_times[24];
    /* A/B桶采样时段 */
    uint8_t       bucket_a_sample_count;
    TpOperationSlot bucket_a_slots[TP_MAX_SLOTS];
    uint8_t       bucket_b_sample_count;
    TpOperationSlot bucket_b_slots[TP_MAX_SLOTS];
    /* 推迟累计 */
    int16_t       total_delay_offset_min;
    /* 起始桶 */
    uint8_t       start_bucket;
    uint8_t       is_valid;
} DailyTimeSchedule;

/* 4种启动模式 */
typedef enum {
    STARTUP_FULL_SAMPLING     = 0,  /* 满量采样 */
    STARTUP_INSTANT_DELIVERY  = 1,  /* 瞬时送样 */
    STARTUP_SKIP_TO_CYCLE     = 2,  /* 跳过到周期 */
    STARTUP_INSTANT_SAMPLING  = 3,  /* 瞬时采样 */
} startup_sampling_mode_t;

/* ======================== 通信触发 ======================== */

/* 通信触发请求类型 */
typedef enum {
    COMM_REQ_NONE     = 0,
    COMM_REQ_SAMPLING = 1,
    COMM_REQ_DRAIN    = 2,
    COMM_REQ_DELIVERY = 3,
} comm_req_type_t;

/* 通信触发请求 */
typedef struct {
    comm_req_type_t type;
    uint8_t  bucket;    /* 0=A, 1=B, 2=自动 */
    uint16_t volume;
    uint8_t  pending;
} comm_trigger_req_t;

extern comm_trigger_req_t g_comm_trigger_req;

/* ======================== 调度器接口 ======================== */

void    scheduler_init(sched_mode_t mode);
void    scheduler_start(void);
void    scheduler_stop(void);
void    scheduler_pause(void);
void    scheduler_resume(void);
void    scheduler_run(void);          /* Task02调用, 非阻塞 */
uint8_t scheduler_is_running(void);
sched_phase_t scheduler_get_phase(void);
sched_mode_t  scheduler_get_mode(void);

/* 统计查询 */
uint32_t scheduler_get_total_cycles(void);
uint32_t scheduler_get_total_samples(void);
uint32_t scheduler_get_total_deliveries(void);
uint8_t  scheduler_get_active_bucket(void);

/* 时间等比专用查询 */
startup_sampling_mode_t scheduler_get_startup_mode(void);
const DailyTimeSchedule *scheduler_get_daily_schedule(void);

/* 外部事件通知 */
void scheduler_notify_flow_start(uint32_t timestamp);
void scheduler_notify_flow_stop(uint32_t timestamp);
void scheduler_notify_switch_signal(uint32_t timestamp);
void scheduler_notify_comm(comm_req_type_t req,
                           uint8_t bucket, uint16_t vol);

/* 流量触发：送样/留样完成回调 */
void scheduler_flow_delivery_complete(uint8_t bucket_id, uint16_t volume);
void scheduler_flow_retention_complete(void);

/* 送样完成通知Task04（由调度器内部调用） */
void scheduler_notify_task4_delivery(uint8_t bucket_id);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */

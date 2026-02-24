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

/* 调度器接口 */
void    scheduler_init(sched_mode_t mode);
void    scheduler_start(void);
void    scheduler_stop(void);
void    scheduler_run(void);          /* Task02调用, 非阻塞 */
uint8_t scheduler_is_running(void);
sched_phase_t scheduler_get_phase(void);

/* 外部事件通知 */
void scheduler_notify_flow(uint8_t active);
void scheduler_notify_switch(void);
void scheduler_notify_comm(comm_req_type_t req,
                           uint8_t bucket, uint16_t vol);

/* 送样完成通知Task04（由调度器内部调用） */
void scheduler_notify_task4_delivery(uint8_t bucket_id);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */

/**
 * @file    app_sampling.h
 * @brief   采样/排水状态机接口定义
 */
#ifndef APP_SAMPLING_H
#define APP_SAMPLING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 桶状态码 ======================== */
typedef enum {
    BUCKET_IDLE = 0,
    BUCKET_SAMPLING,
    BUCKET_DELIVERY,
    BUCKET_RETENTION,
    BUCKET_DRAINING,
    BUCKET_MIXING
} bucket_state_t;

/* ======================== 采样阶段 ======================== */
typedef enum {
    SAMP_IDLE = 0,
    SAMP_PRE_BLOW,        /* 前反吹 */
    SAMP_DELAY_AFTER_PRE, /* 前反吹后延时 */
    SAMP_IMPROVE,         /* 外接泵提升 */
    SAMP_TUBE_HOLD,       /* 管存静置 */
    SAMP_MEASURE,         /* 计量采样 */
    SAMP_DELAY_AFTER_MEAS,/* 采样后延时 */
    SAMP_POST_BLOW,       /* 后反吹 */
    SAMP_DONE,
    SAMP_ABORT
} samp_stage_t;

/* ======================== 排水阶段 ======================== */
typedef enum {
    DRAIN_IDLE = 0,
    DRAIN_OPEN_VALVE,     /* 开排水阀 */
    DRAIN_MIXING,         /* 搅拌(可选) */
    DRAIN_WAIT,           /* 等待排空 */
    DRAIN_DONE
} drain_stage_t;

/* ======================== 采样电机ID ======================== */
#define MOTOR_ID_SAMPLING     0   /* 采样蠕动泵 */
#define MOTOR_ID_DELIVERY     1   /* 送留样蠕动泵 */
#define MOTOR_ID_TURNTABLE    2   /* 留样转盘 */

/* ======================== 送样阶段 ======================== */
typedef enum {
    DELIV_IDLE = 0,
    DELIV_PRE_BLOW,       /* 反吹清线 */
    DELIV_DELAY_AFTER_PRE,/* 反吹后延时 */
    DELIV_STABILIZE,      /* 稳定等待(2s) */
    DELIV_MIX,            /* 启动搅拌 */
    DELIV_MEASURE,        /* 计量送样 */
    DELIV_DELAY_AFTER_MEAS,/* 送样后延时 */
    DELIV_BACKDRAW,       /* 回抽 */
    DELIV_DONE,
    DELIV_ABORT
} deliv_stage_t;

/* ======================== 留样阶段 ======================== */
typedef enum {
    RETAIN_IDLE = 0,
    RETAIN_MOVE_BOTTLE,   /* 转盘定位 */
    RETAIN_PUMP,          /* 泵送留样 */
    RETAIN_DONE,
    RETAIN_ABORT
} retain_stage_t;

/* ======================== 非阻塞状态机接口 ======================== */

/* 采样 */
uint8_t sampling_start(uint8_t bucket, uint8_t is_manual);
void    sampling_step(void);       /* Task02 周期调用 */
uint8_t sampling_is_active(void);
uint8_t sampling_get_result(void); /* 0=fail 1=ok 2=abort */
void    sampling_abort(void);

/* 排水 */
uint8_t drain_start(uint8_t bucket);
void    drain_step(void);
uint8_t drain_is_active(void);

/* 送样 */
uint8_t delivery_start(uint8_t bucket, uint8_t is_manual);
void    delivery_step(void);
uint8_t delivery_is_active(void);
uint8_t delivery_get_result(void);

/* 留样 */
uint8_t retain_start(uint8_t bottle_target, uint8_t is_manual);
void    retain_step(void);
uint8_t retain_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SAMPLING_H */

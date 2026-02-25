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

/* ======================== 水样上下文（跟踪采样→送样关联） ======================== */
typedef struct {
    char     sample_id[18];              /* 当前采样ID "YYYYMMDDHHmmss-SSS" */
    uint8_t  bucket_id;                  /* 桶号 0=A 1=B */
    uint32_t sampling_complete_time;     /* 采样完成时间戳 */
    uint32_t delivery_complete_time;     /* 送样完成时间戳 */
    uint8_t  valid;                      /* 0=无效 1=有效 */
} WaterSampleContext;

/* ======================== 手动操作类型 ======================== */
typedef enum {
    MANUAL_NONE = 0,
    /* 流程类 (1-9) */
    MANUAL_SAMPLING          = 1,
    MANUAL_DELIVERY          = 2,
    MANUAL_RETENTION         = 3,
    MANUAL_INSTANT_DELIVERY  = 4,
    MANUAL_INSTANT_RETENTION = 5,
    /* 单点控制 (10-19) */
    MANUAL_MOTOR_CONTROL     = 10,
    MANUAL_PUMP_CONTROL      = 11,
    MANUAL_VALVE_CONTROL     = 12,
    MANUAL_MIXER_CONTROL     = 13,
    MANUAL_DRAIN_CONTROL     = 14,
    /* 瓶控制 (20-29) */
    MANUAL_BOTTLE_RESET      = 20,
    MANUAL_BOTTLE_MOVE       = 21,
    MANUAL_BOTTLE_EMPTY      = 22,
    /* 系统级 (90+) */
    MANUAL_SYSTEM_RESET      = 90,
    MANUAL_SYSTEM_START      = 91
} ManualOperationType;

/* ======================== 手动操作影响评估 ======================== */
typedef struct {
    ManualOperationType operation;
    uint32_t timestamp;
    int16_t  water_delta_a;     /* A桶水量变化(ml) */
    int16_t  water_delta_b;     /* B桶水量变化(ml) */
    uint8_t  skip_sampling;     /* 跳过周期采样 */
    uint8_t  skip_delivery;     /* 跳过周期送样 */
    uint8_t  skip_retention;    /* 跳过周期留样 */
    uint8_t  bottle_changed;    /* 瓶位是否改变 */
    uint8_t  bottle_after;      /* 操作后瓶号 */
    uint8_t  has_impact;        /* 是否有影响 */
} ManualOperationImpact;

/* ======================== 系统启动模式（S8使用） ======================== */
typedef enum {
    START_MODE_MANUAL = 0,               /* 手动启动 */
    START_MODE_POWER_RECOVERY            /* 断电恢复 */
} SystemStartMode;

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

/* 水样上下文 */
extern WaterSampleContext g_water_ctx[2];  /* [0]=A桶 [1]=B桶 */
void water_ctx_reset(uint8_t bucket_id);

/* 桶状态更新 */
void update_bucket_state(uint8_t bucket_id, bucket_state_t state);

/* 手动操作影响评估 */
void manual_operation_assess_impact(ManualOperationType op, ManualOperationImpact *out);

/* 系统启动序列（S8实现） */
void system_start_sequence(SystemStartMode mode);

#ifdef __cplusplus
}
#endif

#endif /* APP_SAMPLING_H */

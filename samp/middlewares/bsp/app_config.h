/**
 * @file    app_config.h
 * @brief   系统配置结构体 + KVDB存取接口
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 系统运行状态 ======================== */
typedef struct {
    uint8_t running;          /* 0=停止 1=运行 */
    uint8_t bucket_a_state;   /* bucket_state_t */
    uint8_t bucket_b_state;   /* bucket_state_t */
    uint8_t current_bucket;   /* 当前活跃桶 0=A 1=B */
} SystemState;

/* ======================== 采样配置（从KVDB加载） ======================== */
typedef struct {
    uint8_t  mode;            /* 1=时间等比 2=流量 3=开关量 4=直接 */
    uint16_t interval_min;    /* 采样间隔(分钟) */
    uint16_t volume_ml;       /* 单次采样量(ml) */
    uint16_t blowback_sec;    /* 反吹时长(s) */
    uint16_t improve_sec;     /* 提升时长(s) */
    uint16_t tube_hold_sec;   /* 管存时长(s) */
    uint16_t motor_rpm;       /* 采样电机转速 */
} SamplingConfig;

/* ======================== 送样配置 ======================== */
typedef struct {
    uint16_t volume_ml;       /* 送样量(ml) */
    uint16_t motor_rpm;       /* 送样电机转速 */
    uint16_t backdraw_sec;    /* 回抽时长(s) */
} DeliveryConfig;

/* ======================== 留样配置 ======================== */
typedef struct {
    uint8_t  mode;            /* 留样模式 */
    uint16_t volume_ml;       /* 留样量(ml) */
    uint8_t  bottle_count;    /* 留样瓶数(1-24) */
    uint16_t motor_rpm;       /* 留样电机转速 */
} RetainConfig;

/* ======================== KVDB 存取接口 ======================== */
void cfg_init_load(void);     /* 上电加载所有配置 */
void cfg_save_all(void);      /* 保存所有配置到KVDB */

/* ======================== 全局实例 ======================== */
extern SystemState        g_state;
extern SamplingConfig     g_sampling_cfg;
extern DeliveryConfig     g_delivery_cfg;
extern RetainConfig       g_retain_cfg;

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */

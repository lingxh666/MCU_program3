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
    uint8_t  running;          /* 0=停止 1=运行 */
    uint8_t  bucket_a_state;   /* bucket_state_t */
    uint8_t  bucket_b_state;   /* bucket_state_t */
    uint8_t  current_bucket;   /* 当前活跃桶 0=A 1=B */
    /* 设备状态 */
    uint8_t  sampling_motor;   /* 采样电机状态 */
    uint8_t  delivery_motor;   /* 送样电机状态 */
    uint8_t  inlet_valve;      /* 进水阀状态 */
    uint8_t  outlet_valve;     /* 出水阀状态 */
    uint8_t  sample_valve;     /* 采样阀状态 */
    uint8_t  instant_valve;    /* 即时阀状态 */
    uint8_t  drain_a;          /* A桶排水阀状态 */
    uint8_t  drain_b;          /* B桶排水阀状态 */
    uint16_t water_a;          /* A桶存水量(ml) */
    uint16_t water_b;          /* B桶存水量(ml) */
    /* 调度器状态 */
    uint8_t  current_mode;     /* 当前采样模式 */
    uint8_t  current_phase;    /* 当前调度阶段 */
    uint32_t cycle_count;      /* 周期计数 */
    uint32_t sample_count;     /* 采样计数 */
    uint32_t delivery_count;   /* 送样计数 */
    /* 留样瓶 */
    uint8_t  bottle_current;   /* 当前留样瓶号 */
    uint8_t  bottle_next;      /* 下一留样瓶号 */
    uint8_t  bottle_empty;     /* 空瓶标志 */
    /* 时间 */
    uint8_t  time[6];          /* 年月日时分秒 */
} SystemState;

/* ======================== 采样配置（从KVDB加载） ======================== */
typedef struct {
    uint8_t  mode;            /* 0=时间等比 1=定时 2=流量 3=开关量 4=通信 */
    uint16_t interval_min;    /* 采样间隔(分钟) */
    uint16_t volume_ml;       /* 单次采样量(ml) */
    uint16_t blowback_sec;    /* 反吹时长(s) */
    uint16_t improve_sec;     /* 提升时长(s) */
    uint16_t tube_hold_sec;   /* 管存时长(s) */
    uint16_t motor_rpm;       /* 采样电机转速 */
    uint16_t cycle_time_min;  /* 周期时间(分钟) */
    uint16_t analysis_time_min; /* 仪器分析时间(分钟) */
    uint16_t flow_start;      /* 流量触发值(m³/h) */
    uint16_t flow_stop;       /* 流量停止值(m³/h) */
} SamplingConfig;

/* ======================== 送样配置 ======================== */
typedef struct {
    uint16_t volume_ml;       /* 送样量(ml) */
    uint16_t motor_rpm;       /* 送样电机转速 */
    uint16_t backdraw_sec;    /* 回抽时长(s) */
    uint8_t  enable;          /* 是否启用定时送样 */
    uint8_t  start_hour;      /* 送样小时 */
    uint8_t  start_min;       /* 送样分钟 */
    uint16_t duration_sec;    /* 送样时长(s) */
    uint8_t  fixedhour[24];   /* 定时触发小时数组 */
    uint8_t  fixedmin;        /* 定时触发分钟 */
} DeliveryConfig;

/* ======================== 留样配置 ======================== */
typedef struct {
    uint8_t  mode;            /* 留样模式(0-6) */
    uint16_t volume_ml;       /* 留样量(ml) */
    uint8_t  bottle_count;    /* 留样瓶数(1-24) */
    uint16_t motor_rpm;       /* 留样电机转速 */
    uint8_t  enable;          /* 是否留样 */
    uint8_t  parallel_count;  /* 平行样数量 */
    uint8_t  mix_count;       /* 混样次数 */
    uint8_t  enable_acid;     /* 是否加酸 */
    uint16_t tube_hold_sec;   /* 留样管存时间(s) */
    uint16_t blowback_sec;    /* 留样反吹时间(s) */
    uint16_t backdraw_sec;    /* 留样回抽时间(s) */
} RetainConfig;

/* ======================== 通讯配置 ======================== */
typedef struct {
    uint8_t  protocol;        /* 通讯协议(0=大岳 1=大湖 2=四川 3=西安) */
    uint8_t  device_addr;     /* 设备地址 */
    uint16_t flow_ad_lower;   /* 流量AD下限(0=0-20mA, 非0=4-20mA) */
    float    flow_meter_base; /* 流量计量程(m³/h) */
} CommConfig;

/* ======================== 通道限值配置 ======================== */
typedef struct {
    uint8_t  enable;          /* 是否启用 */
    uint8_t  factor_type;     /* 因子类型 */
    float    lower_limit;     /* 超标下限 */
    float    upper_limit;     /* 超标上限 */
} ChannelLimitConfig;

/* ======================== KVDB 存取接口 ======================== */
void cfg_init_load(void);     /* 上电加载所有配置 */
void cfg_save_all(void);      /* 保存所有配置到KVDB */

/* ======================== 全局实例 ======================== */
extern SystemState        g_state;
extern SamplingConfig     g_sampling_cfg;
extern DeliveryConfig     g_delivery_cfg;
extern RetainConfig       g_retain_cfg;
extern CommConfig         g_comm_cfg;
extern ChannelLimitConfig g_ch_limits[6];

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */

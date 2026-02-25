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
    /* MQTT/4G */
    char     mqtt_ip[32];     /* MQTT服务器IP */
    char     device_id[24];   /* 设备ID */
} CommConfig;

/* ======================== 系统设置配置（从KVDB加载） ======================== */
typedef struct {
    /* 时间设置 */
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    /* 模式设置 */
    uint8_t  water_station_mode;  /* 水站模式(1=启用 0=禁用) */
    uint8_t  auto_run_mode;       /* 自动运行(1=启用 0=禁用) */
    /* 软件信息 */
    char     sw_serial[24];       /* 软件序列号 */
    char     sw_core_ver[16];     /* 软件核心板版本 */
    char     sw_lcd_ver[16];      /* 软件液晶屏版本 */
    /* 硬件信息 */
    char     hw_base_ver[16];     /* 硬件底板版本 */
    char     hw_core_ver[16];     /* 硬件核心板版本 */
    char     hw_lcd_ver[16];      /* 硬件液晶屏版本 */
    /* 门禁卡号 */
    uint32_t card_id[10];
    /* 电机 */
    uint8_t  motor_speed;         /* 蠕动泵转速 */
} SystemSettingConfig;

/* ======================== 精度校准参数（从KVDB加载） ======================== */
typedef struct {
    /* 采样量校准 */
    struct {
        uint16_t time1;       uint16_t real_value1;
        uint16_t time2;       uint16_t real_value2;
        uint16_t time3;       uint16_t real_value3;
    } sampling;
    /* 留样量校准 */
    struct {
        uint16_t time1;       uint16_t real_value1;
        uint16_t time2;       uint16_t real_value2;
        uint16_t time3;       uint16_t real_value3;
    } retain;
    /* 加酸量校准 */
    struct {
        uint16_t time1;       uint16_t real_value1;
        uint16_t time2;       uint16_t real_value2;
        uint16_t time3;       uint16_t real_value3;
    } acid;
    /* 温度校准 */
    struct {
        uint16_t input_ad;
        uint16_t zero_point_ad;
        uint16_t calib_ad;
        uint16_t calib_value;   /* 实际值 = calib_value / 100.0f */
        uint16_t set_temp;
        uint16_t upper_dev;
        uint16_t lower_dev;
        uint16_t zero_temp;     /* 实际值 = zero_temp / 100.0f */
    } temp;
} CalibrationParams;

/* ======================== 留样瓶位状态（持久化到KVDB） ======================== */
typedef struct {
    uint8_t  current_bottle;    /* 当前瓶号 1..24 */
    uint32_t used_mask;         /* bit0→瓶1, bit23→瓶24 */
} RetainBottleState;

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
extern SystemSettingConfig  g_system_setting_cfg;
extern CalibrationParams    g_calib_params;
extern RetainBottleState    g_retain_bottle_state;

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */

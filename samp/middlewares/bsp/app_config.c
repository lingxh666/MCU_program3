/**
 * @file    app_config.c
 * @brief   系统配置管理 — 全局实例 + KVDB加载/保存
 */
#include "app_config.h"
#include "app_flashdb.h"
#include <stdio.h>
#include <string.h>

/* ======================== 全局配置实例 ======================== */
SystemState        g_state;
SamplingConfig     g_sampling_cfg;
DeliveryConfig     g_delivery_cfg;
RetainConfig       g_retain_cfg;
CommConfig         g_comm_cfg;
ChannelLimitConfig g_ch_limits[6];

/* ======================== 默认值 ======================== */
static const SamplingConfig s_samp_default = {
    .mode             = 1,       /* 时间等比 */
    .interval_min     = 60,
    .volume_ml        = 100,
    .blowback_sec     = 5,
    .improve_sec      = 30,
    .tube_hold_sec    = 10,
    .motor_rpm        = 1000,
    .cycle_time_min   = 60,
    .analysis_time_min = 30,
    .flow_start       = 10,
    .flow_stop        = 5,
};

static const DeliveryConfig s_deliv_default = {
    .volume_ml     = 500,
    .motor_rpm     = 1000,
    .backdraw_sec  = 3,
    .enable        = 1,
    .start_hour    = 0,
    .start_min     = 58,
    .duration_sec  = 120,
    .fixedhour     = {0},
    .fixedmin      = 58,
};

static const RetainConfig s_retain_default = {
    .mode           = 0,
    .volume_ml      = 200,
    .bottle_count   = 24,
    .motor_rpm      = 800,
    .enable         = 1,
    .parallel_count = 1,
    .mix_count      = 1,
    .enable_acid    = 0,
    .tube_hold_sec  = 10,
    .blowback_sec   = 5,
    .backdraw_sec   = 3,
};

static const CommConfig s_comm_default = {
    .protocol        = 0,       /* 大岳 */
    .device_addr     = 1,
    .flow_ad_lower   = 0,
    .flow_meter_base = 100.0f,
};

static const ChannelLimitConfig s_ch_limit_default = {
    .enable      = 0,
    .factor_type = 0,
    .lower_limit = 0.0f,
    .upper_limit = 0.0f,
};

/* ======================== 上电加载所有配置 ======================== */
void cfg_init_load(void)
{
    /* KVDB已在 settings_init_load() 中初始化 */

    /* 清零运行状态 */
    memset(&g_state, 0, sizeof(g_state));

    /* 加载采样配置，失败则写入默认值 */
    if (!cfg_load_sample(&g_sampling_cfg)) {
        g_sampling_cfg = s_samp_default;
        cfg_save_sample(&g_sampling_cfg);
        printf("[CFG] 采样配置: 使用默认值\r\n");
    } else {
        printf("[CFG] 采样配置: 从KVDB加载\r\n");
    }

    /* 加载送样配置 */
    if (!cfg_load_delivery(&g_delivery_cfg)) {
        g_delivery_cfg = s_deliv_default;
        cfg_save_delivery(&g_delivery_cfg);
        printf("[CFG] 送样配置: 使用默认值\r\n");
    } else {
        printf("[CFG] 送样配置: 从KVDB加载\r\n");
    }

    /* 加载留样配置 */
    if (!cfg_load_retain(&g_retain_cfg)) {
        g_retain_cfg = s_retain_default;
        cfg_save_retain(&g_retain_cfg);
        printf("[CFG] 留样配置: 使用默认值\r\n");
    } else {
        printf("[CFG] 留样配置: 从KVDB加载\r\n");
    }

    /* 加载通讯配置 */
    if (!cfg_load_comm(&g_comm_cfg)) {
        g_comm_cfg = s_comm_default;
        cfg_save_comm(&g_comm_cfg);
        printf("[CFG] 通讯配置: 使用默认值\r\n");
    } else {
        printf("[CFG] 通讯配置: 从KVDB加载\r\n");
    }

    /* 初始化通道限值配置 */
    {
        uint8_t i;
        for (i = 0; i < 6; i++)
            g_ch_limits[i] = s_ch_limit_default;
    }
}

/* ======================== 保存所有配置到KVDB ======================== */
void cfg_save_all(void)
{
    cfg_save_sample(&g_sampling_cfg);
    cfg_save_delivery(&g_delivery_cfg);
    cfg_save_retain(&g_retain_cfg);
    cfg_save_comm(&g_comm_cfg);
}

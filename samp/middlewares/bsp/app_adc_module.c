/**
 * @file    app_adc_module.c
 * @brief   外部AD模块数据接收 — UART8帧解析 + 流量换算 + 边沿检测
 */
#include "app_adc_module.h"
#include "app_config.h"
#include "app_scheduler.h"
#include "bsp_uart.h"
#include <stdio.h>

/* 外部定时器变量 */
extern volatile uint32_t g_tmr4_milliseconds;

/* 全局数据实例 */
adc_module_data_t g_adc_module;

/* 前向声明 */
static void adc_module_update_flow(void);

void adc_module_init(void)
{
    uint8_t i;
    for (i = 0; i < ADC_MOD_CH_COUNT; i++) {
        g_adc_module.raw[i] = 0;
        g_adc_module.ma[i]  = 0.0f;
    }
    g_adc_module.flow_value       = 0.0f;
    g_adc_module.flow_active      = 0;
    g_adc_module.valid            = 0;
    g_adc_module.last_update_tick = 0;
    g_adc_module.rx_count         = 0;
    g_adc_module.err_count        = 0;
    printf("[ADC_MOD] 初始化完成\r\n");
}

void adc_module_poll(void)
{
    uint8_t buf[48];
    uint16_t len;
    uint16_t i;
    uint8_t ch;

    if (!bsp_uart_rx_available(UART_PORT_ADMODULE))
        return;

    len = bsp_uart_get_rxdata(UART_PORT_ADMODULE, buf, sizeof(buf));
    if (len < ADC_MOD_FRAME_LEN)
        return;

    /* 在接收数据中搜索帧头 */
    for (i = 0; i + ADC_MOD_FRAME_LEN <= len; i++) {
        if (buf[i]      == ADC_MOD_HEAD1 &&
            buf[i + 1]  == ADC_MOD_HEAD2 &&
            buf[i + 14] == ADC_MOD_TAIL1 &&
            buf[i + 15] == ADC_MOD_TAIL2)
        {
            /* 解析6通道 */
            for (ch = 0; ch < ADC_MOD_CH_COUNT; ch++) {
                uint16_t offset = i + 2 + ch * 2;
                g_adc_module.raw[ch] = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
                g_adc_module.ma[ch]  = (float)g_adc_module.raw[ch] / 1000.0f;
            }
            g_adc_module.valid            = 1;
            g_adc_module.last_update_tick = g_tmr4_milliseconds;
            g_adc_module.rx_count++;

            /* 流量换算 + 边沿检测 */
            adc_module_update_flow();
            return;
        }
    }
    g_adc_module.err_count++;
}

/* 流量换算 + 迟滞边沿检测 */
static void adc_module_update_flow(void)
{
    float current_ma = g_adc_module.ma[5]; /* CH6 = 流量 */
    float i_lower = (g_comm_cfg.flow_ad_lower == 0) ? 0.0f : 4.0f;
    float i_upper = 20.0f;
    float ratio;

    if (i_upper <= i_lower) {
        g_adc_module.flow_value = 0.0f;
        return;
    }

    ratio = (current_ma - i_lower) / (i_upper - i_lower);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    g_adc_module.flow_value = ratio * g_comm_cfg.flow_meter_base;

    /* 迟滞边沿检测 */
    if (!g_adc_module.flow_active &&
        g_adc_module.flow_value >= (float)g_sampling_cfg.flow_start)
    {
        g_adc_module.flow_active = 1;
        printf("[ADC_MOD] 流量开始: %.2f >= %u\r\n",
               g_adc_module.flow_value, g_sampling_cfg.flow_start);
        scheduler_notify_flow(1);
    }
    else if (g_adc_module.flow_active &&
             g_adc_module.flow_value <= (float)g_sampling_cfg.flow_stop)
    {
        g_adc_module.flow_active = 0;
        printf("[ADC_MOD] 流量停止: %.2f <= %u\r\n",
               g_adc_module.flow_value, g_sampling_cfg.flow_stop);
        scheduler_notify_flow(0);
    }
}

uint16_t adc_module_get_raw(uint8_t ch)
{
    if (ch >= ADC_MOD_CH_COUNT) return 0;
    return g_adc_module.raw[ch];
}

float adc_module_get_ma(uint8_t ch)
{
    if (ch >= ADC_MOD_CH_COUNT) return 0.0f;
    return g_adc_module.ma[ch];
}

float adc_module_get_flow(void)
{
    return g_adc_module.flow_value;
}

uint8_t adc_module_is_flow_active(void)
{
    return g_adc_module.flow_active;
}

uint8_t adc_module_is_valid(void)
{
    return g_adc_module.valid &&
           (g_tmr4_milliseconds - g_adc_module.last_update_tick < 2000);
}

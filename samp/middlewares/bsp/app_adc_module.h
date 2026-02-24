/**
 * @file    app_adc_module.h
 * @brief   外部AD模块数据接收（UART8 16字节帧解析 + 流量检测）
 */
#ifndef APP_ADC_MODULE_H
#define APP_ADC_MODULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_MOD_CH_COUNT    6
#define ADC_MOD_FRAME_LEN   16
#define ADC_MOD_HEAD1       0x6B
#define ADC_MOD_HEAD2       0xB6
#define ADC_MOD_TAIL1       0x8C
#define ADC_MOD_TAIL2       0xC8

typedef struct {
    uint16_t raw[ADC_MOD_CH_COUNT];   /* 原始值 (mA×1000) */
    float    ma[ADC_MOD_CH_COUNT];    /* 转换后mA值 */
    float    flow_value;               /* 换算后实际流量 (m³/h) */
    uint8_t  flow_active;              /* 流量信号状态 */
    uint8_t  valid;                    /* 数据有效标志 */
    uint32_t last_update_tick;         /* 最后更新时间(ms) */
    uint32_t rx_count;                 /* 接收帧计数 */
    uint32_t err_count;                /* 错误帧计数 */
} adc_module_data_t;

extern adc_module_data_t g_adc_module;

void     adc_module_init(void);
void     adc_module_poll(void);           /* Task05调用 */
uint16_t adc_module_get_raw(uint8_t ch);
float    adc_module_get_ma(uint8_t ch);
float    adc_module_get_flow(void);       /* 换算后流量(m³/h) */
uint8_t  adc_module_is_flow_active(void);
uint8_t  adc_module_is_valid(void);       /* 2秒超时 */

#ifdef __cplusplus
}
#endif

#endif /* APP_ADC_MODULE_H */

#ifndef BSP_ADC_H
#define BSP_ADC_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== ADC1 DMA Channel Index (12ch) ======================== */
typedef enum {
  ADC_CH_VREF = 0,        /* CH0  PA0  2.5V基准 */
  ADC_CH_BOTTLE_MOTOR,    /* CH2  PA2  瓶排空电机电流 */
  ADC_CH_OUTLET_VALVE_A,  /* CH3  PA3  出水阀A电流 */
  ADC_CH_STIR_B,          /* CH4  PA4  B搅拌电流 */
  ADC_CH_STIR_A,          /* CH5  PA5  A搅拌电流 */
  ADC_CH_DRAIN_B,         /* CH6  PA6  B排水电流 */
  ADC_CH_DRAIN_A,         /* CH7  PA7  A排水电流 */
  ADC_CH_INLET_VALVE,     /* CH10 PC0  进水阀电流 */
  ADC_CH_INSTANT_VALVE,   /* CH11 PC1  瞬时阀电流 */
  ADC_CH_DELIVER_VALVE,   /* CH14 PC4  送留样阀电流 */
  ADC_CH_TEMP1,           /* CH15 PC5  冰箱温度1 */
  ADC_CH_TEMP2,           /* CH8  PB0  冰箱温度2 */
  ADC1_CHANNEL_COUNT      /* 12 */
} adc1_ch_index_t;

/* ======================== ADC2 4-20mA Channel ======================== */
typedef enum {
  ADC_420MA_CH1 = 0,      /* CH12 PC2 */
  ADC_420MA_CH2,           /* CH1  PA1 */
  ADC2_CHANNEL_COUNT       /* 2 */
} adc2_ch_index_t;

/* ======================== ADC1 DMA Buffer ======================== */
#define ADC1_DMA_BUF_DEPTH   8   /* 每通道采样深度，用于滑动平均 */
extern volatile uint16_t adc1_dma_buf[ADC1_DMA_BUF_DEPTH][ADC1_CHANNEL_COUNT];

/* ======================== Calibration ======================== */
typedef struct {
  float vref_factor;       /* 校准系数 = 2.5 / (vref_raw * 3.3 / 4096) */
  uint16_t vref_raw;       /* 基准通道原始值 */
  uint8_t valid;           /* 校准是否有效 */
} adc_cal_t;

extern adc_cal_t adc_cal;

/* ======================== API ======================== */
/* ADC1 DMA 启动（在main初始化后调用） */
void bsp_adc1_dma_start(void);

/* 获取ADC1指定通道的滤波后原始值 */
uint16_t adc1_get_raw(adc1_ch_index_t ch);

/* 获取校准后的电压值(mV) */
float adc1_get_voltage_mv(adc1_ch_index_t ch);

/* 获取继电器/阀门电流(mA)，基于采样电阻和放大倍数 */
float adc1_get_current_ma(adc1_ch_index_t ch);

/* 判断阀门是否正常通电（电流>阈值） */
uint8_t adc1_is_active(adc1_ch_index_t ch);

/* NTC温度获取(°C, 精度0.1) */
float adc_get_temp(uint8_t ch);  /* ch: 0=温度1, 1=温度2 */

/* ADC2 4-20mA 单次转换 */
uint16_t adc2_read_raw(adc2_ch_index_t ch);
float adc2_get_420ma_current(adc2_ch_index_t ch);

/* 校准 */
void adc_cal_run(void);
float adc_cal_apply(uint16_t raw);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_H */

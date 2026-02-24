#include "bsp_adc.h"
#include <math.h>

/* ======================== ADC1 DMA Buffer ======================== */
volatile uint16_t adc1_dma_buf[ADC1_DMA_BUF_DEPTH][ADC1_CHANNEL_COUNT];

/* ======================== Calibration Data ======================== */
adc_cal_t adc_cal = { .vref_factor = 1.0f, .vref_raw = 0, .valid = 0 };

/* ======================== NTC3950 Parameters ======================== */
#define NTC_R25        10000.0f   /* 25°C标称阻值 */
#define NTC_B          3950.0f    /* B值 */
#define NTC_T25        298.15f    /* 25°C = 298.15K */
#define NTC_R_PULLUP   10000.0f   /* 上拉电阻 */
#define ADC_VREF       3.3f
#define ADC_MAX        4096.0f

/* 电流检测阈值(mA)，低于此值认为未通电 */
#define CURRENT_ACTIVE_THRESHOLD  5.0f

/* ======================== ADC1 DMA Start ======================== */
void bsp_adc1_dma_start(void)
{
  /* DMA2_CH1 已在 wk_config 中初始化为 loop mode, halfword */
  /* 配置传输参数：外设地址=ADC1->odt, 内存地址=adc1_dma_buf, 大小=深度*通道数 */
  dma_channel_enable(DMA2_CHANNEL1, FALSE);
  DMA2_CHANNEL1->dtcnt = ADC1_DMA_BUF_DEPTH * ADC1_CHANNEL_COUNT;
  DMA2_CHANNEL1->paddr = (uint32_t)&ADC1->odt;
  DMA2_CHANNEL1->maddr = (uint32_t)adc1_dma_buf;
  dma_channel_enable(DMA2_CHANNEL1, TRUE);

  /* 使能ADC1 DMA请求 */
  adc_dma_mode_enable(ADC1, TRUE);
  adc_dma_request_repeat_enable(ADC1, TRUE);

  /* 软件触发ADC1开始连续转换 */
  adc_ordinary_software_trigger_enable(ADC1, TRUE);
}

/* ======================== ADC1 Raw Value (滤波) ======================== */
uint16_t adc1_get_raw(adc1_ch_index_t ch)
{
  uint32_t sum = 0;
  uint8_t i;
  if(ch >= ADC1_CHANNEL_COUNT) return 0;
  for(i = 0; i < ADC1_DMA_BUF_DEPTH; i++)
    sum += adc1_dma_buf[i][ch];
  return (uint16_t)(sum / ADC1_DMA_BUF_DEPTH);
}

/* ======================== Calibration ======================== */
void adc_cal_run(void)
{
  uint16_t raw = adc1_get_raw(ADC_CH_VREF);
  if(raw > 100) /* 有效值 */
  {
    float measured_v = (float)raw * ADC_VREF / ADC_MAX;
    adc_cal.vref_factor = 2.5f / measured_v;
    adc_cal.vref_raw = raw;
    adc_cal.valid = 1;
  }
}

float adc_cal_apply(uint16_t raw)
{
  float v = (float)raw * ADC_VREF / ADC_MAX;
  if(adc_cal.valid)
    v *= adc_cal.vref_factor;
  return v;
}

/* ======================== Voltage / Current ======================== */
float adc1_get_voltage_mv(adc1_ch_index_t ch)
{
  return adc_cal_apply(adc1_get_raw(ch)) * 1000.0f;
}

float adc1_get_current_ma(adc1_ch_index_t ch)
{
  /* 继电器电流检测电路：INA180A2 增益50V/V, 采样电阻0.2Ω
   * V_adc = I * R_sense * Gain = I * 0.2 * 50 = I * 10
   * I = V_adc / 10 */
  float v = adc_cal_apply(adc1_get_raw(ch));
  return v * 100.0f;  /* V / 10 * 1000 = mA */
}

uint8_t adc1_is_active(adc1_ch_index_t ch)
{
  return (adc1_get_current_ma(ch) > CURRENT_ACTIVE_THRESHOLD) ? 1 : 0;
}

/* ======================== NTC Temperature ======================== */
float adc_get_temp(uint8_t ch)
{
  adc1_ch_index_t idx = (ch == 0) ? ADC_CH_TEMP1 : ADC_CH_TEMP2;
  uint16_t raw = adc1_get_raw(idx);
  if(raw < 10 || raw > 4085) return -999.0f; /* 开路/短路 */

  float v = adc_cal_apply(raw);
  /* NTC在下拉位置: V = Vcc * Rntc / (Rpullup + Rntc)
   * => Rntc = Rpullup * V / (Vcc - V) */
  float r_ntc = NTC_R_PULLUP * v / (ADC_VREF - v);
  if(r_ntc <= 0) return -999.0f;

  /* Steinhart-Hart simplified (B-parameter): 1/T = 1/T25 + (1/B)*ln(R/R25) */
  float t_kelvin = 1.0f / (1.0f / NTC_T25 + (1.0f / NTC_B) * logf(r_ntc / NTC_R25));
  return t_kelvin - 273.15f;
}

/* ======================== ADC2 4-20mA ======================== */
/* ADC2 通道映射 */
static const adc_channel_select_type adc2_ch_map[ADC2_CHANNEL_COUNT] = {
  ADC_CHANNEL_12,  /* PC2 */
  ADC_CHANNEL_1,   /* PA1 */
};

uint16_t adc2_read_raw(adc2_ch_index_t ch)
{
  if(ch >= ADC2_CHANNEL_COUNT) return 0;

  /* 配置ADC2单次转换通道 */
  adc_ordinary_channel_set(ADC2, adc2_ch_map[ch], 1, ADC_SAMPLETIME_247_5);

  /* 软件触发 */
  adc_ordinary_software_trigger_enable(ADC2, TRUE);

  /* 等待转换完成 */
  while(adc_flag_get(ADC2, ADC_OCCE_FLAG) == RESET);
  adc_flag_clear(ADC2, ADC_OCCE_FLAG);

  return (uint16_t)adc_ordinary_conversion_data_get(ADC2);
}

float adc2_get_420ma_current(adc2_ch_index_t ch)
{
  /* 多次采样取平均 */
  uint32_t sum = 0;
  uint8_t i;
  for(i = 0; i < 4; i++)
    sum += adc2_read_raw(ch);
  uint16_t raw = (uint16_t)(sum / 4);

  /* 硬件: INA180A2 增益50V/V, 采样电阻3Ω
   * V_adc = I * 3 * 50 = I * 150
   * I = V_adc / 150 */
  float v = adc_cal_apply(raw);
  float current_a = v / 150.0f;
  return current_a * 1000.0f;  /* mA */
}

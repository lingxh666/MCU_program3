/**
  ***************************************************************************
  * @file     motor_monitor.c
  * @brief    四路电机电流监测实现
  *           ADC DMA循环模式，HDT/FDT中断实现双缓冲
  *           去极值均值滤波（去掉最大最小值后取平均）
  ***************************************************************************
  */

#include "motor_monitor.h"
#include "stepper_motor.h"
#include <string.h>

/* 全局监测数据 */
motor_monitor_data_t motor_monitor[4] = {0};

/* ADC DMA循环缓冲区 */
uint16_t adc_dma_buf[ADC_DMA_TOTAL_SIZE];

/* 指向已完成的半缓冲区（供主循环处理） */
static volatile uint16_t *g_process_buf = NULL;
static volatile uint8_t g_half_ready = 0;

void MotorMonitorInit(void)
{
  memset(adc_dma_buf, 0, sizeof(adc_dma_buf));
  memset(motor_monitor, 0, sizeof(motor_monitor));
  g_process_buf = NULL;
  g_half_ready = 0;

  /* 启用ADC DMA请求 */
  adc_dma_mode_enable(ADC1, TRUE);

  /* 启动ADC软件触发（repeat模式会持续转换） */
  adc_ordinary_software_trigger_enable(ADC1, TRUE);
}

/**
 * @brief  DMA半传输完成中断回调（前半缓冲区已填满）
 */
void MotorMonitor_DmaHalfISR(void)
{
  g_process_buf = &adc_dma_buf[0];
  g_half_ready = 1;
}

/**
 * @brief  DMA全传输完成中断回调（后半缓冲区已填满）
 */
void MotorMonitor_DmaFullISR(void)
{
  g_process_buf = &adc_dma_buf[ADC_DMA_HALF_SIZE];
  g_half_ready = 1;
}

/**
 * @brief  对单通道数据做去极值均值滤波
 * @param  samples: 采样数据起始指针
 * @param  count: 采样组数
 * @param  ch_offset: 通道在每组中的偏移
 * @retval 滤波后的ADC值
 */
static uint16_t filter_channel(const volatile uint16_t *samples, uint16_t count, uint8_t ch_offset)
{
  uint32_t sum = 0;
  uint16_t min_val = 0xFFFF, max_val = 0;
  uint16_t i, val;

  for (i = 0; i < count; i++)
  {
    val = samples[i * ADC_CHANNEL_COUNT + ch_offset];
    sum += val;
    if (val < min_val) min_val = val;
    if (val > max_val) max_val = val;
  }

  if (count > 2)
  {
    sum = sum - min_val - max_val;
    return (uint16_t)(sum / (count - 2));
  }
  return (uint16_t)(sum / count);
}

/**
 * @brief  ADC值转电流(mA)
 */
static uint16_t adc_to_current_ma(uint16_t adc_val)
{
  /* I(mA) = adc_val * 3300 / 4096 */
  return (uint16_t)((uint32_t)adc_val * VREF_MV / ADC_FULL_SCALE);
}

/**
 * @brief  判断电机监测状态
 */
static monitor_status_t determine_status(motor_id_t mid, uint16_t current_ma)
{
  uint8_t is_running = (MotorGetState(mid) != MOTOR_STATE_STOPPED);
  uint8_t has_current = (current_ma > CURRENT_THRESHOLD_MA);

  if (is_running && has_current)   return MONITOR_STATUS_RUNNING;
  if (is_running && !has_current)  return MONITOR_STATUS_FAULT;
  if (!is_running && has_current)  return MONITOR_STATUS_FAULT;
  return MONITOR_STATUS_STOPPED;
}

/**
 * @brief  更新电流监测数据（TMR15 1Hz中断中调用）
 */
void MotorMonitorUpdate(void)
{
  const volatile uint16_t *buf;
  uint16_t filtered[4];
  uint8_t i;
  /* ADC通道偏移到电机ID的映射 */
  static const uint8_t ch_map[4] = { ADC_IDX_MOTOR1, ADC_IDX_MOTOR2, ADC_IDX_MOTOR3, ADC_IDX_MOTOR4 };
  static const motor_id_t mid_map[4] = { MOTOR_ID_1, MOTOR_ID_2, MOTOR_ID_3, MOTOR_ID_4 };

  if (!g_half_ready)
    return;

  /* 取走缓冲区指针 */
  __disable_irq();
  buf = g_process_buf;
  g_half_ready = 0;
  __enable_irq();

  if (buf == NULL)
    return;

  /* 对4个通道分别滤波 */
  for (i = 0; i < 4; i++)
  {
    filtered[i] = filter_channel(buf, ADC_SAMPLES_PER_HALF, ch_map[i]);
  }

  /* 更新各电机监测数据 */
  for (i = 0; i < 4; i++)
  {
    motor_monitor[i].raw_adc = filtered[i];
    motor_monitor[i].current_ma = adc_to_current_ma(filtered[i]);
    motor_monitor[i].status = determine_status(mid_map[i], motor_monitor[i].current_ma);

    if (motor_monitor[i].status == MONITOR_STATUS_FAULT)
    {
      if (motor_monitor[i].current_ma <= CURRENT_THRESHOLD_MA)
        motor_monitor[i].fault_code = 1;  /* 欠流 */
      else
        motor_monitor[i].fault_code = 2;  /* 过流 */
    }
    else
    {
      motor_monitor[i].fault_code = 0;
    }
  }
}

uint16_t MotorGetCurrentMA(uint8_t motor_id)
{
  if (motor_id >= 4) return 0;
  return motor_monitor[motor_id].current_ma;
}

monitor_status_t MotorGetMonitorStatus(uint8_t motor_id)
{
  if (motor_id >= 4) return MONITOR_STATUS_STOPPED;
  return motor_monitor[motor_id].status;
}

uint8_t MotorGetFaultCode(uint8_t motor_id)
{
  if (motor_id >= 4) return 0;
  return motor_monitor[motor_id].fault_code;
}

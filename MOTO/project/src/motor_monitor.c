/**
  ***************************************************************************
  * @file     motor_monitor.c
  * @brief    motor current monitoring and fault detection implementation
  ***************************************************************************
  */

#include "motor_monitor.h"
#include "at32f421_wk_config.h"
#include "stepper_motor.h"
#include <string.h>

/* Global monitoring data */
motor_monitor_data_t motor1_monitor = {0};
motor_monitor_data_t motor2_monitor = {0};

/* ADC DMA double-buffer (normal DMA mode) */
static uint16_t adc_buf1[ADC_DMA_BUFFER_SIZE];
static uint16_t adc_buf2[ADC_DMA_BUFFER_SIZE];
static volatile uint16_t *g_adc_active_buf = adc_buf1;   /* buffer currently used by DMA */
static volatile uint16_t *g_adc_process_buf = adc_buf2;  /* last completed buffer */
static volatile uint8_t g_adc_block_ready = 0;

/**
 * @brief  initialize motor current monitoring
 * @param  none
 * @retval none
 */
void MotorMonitorInit(void)
{
  /* clear DMA buffers */
  memset(adc_buf1, 0, sizeof(adc_buf1));
  memset(adc_buf2, 0, sizeof(adc_buf2));

  /* initialize monitor data */
  memset(&motor1_monitor, 0, sizeof(motor_monitor_data_t));
  memset(&motor2_monitor, 0, sizeof(motor_monitor_data_t));

  /* reset buffer state */
  g_adc_active_buf = adc_buf1;
  g_adc_process_buf = adc_buf2;
  g_adc_block_ready = 0;

  /* configure DMA for ADC1 (non-circular + double buffering) */
  dma_channel_enable(DMA1_CHANNEL1, FALSE);
  dma_flag_clear(DMA1_GL1_FLAG);
  wk_dma_channel_config(DMA1_CHANNEL1,
                        (uint32_t)&ADC1->odt,
                        (uint32_t)g_adc_active_buf,
                        (uint16_t)ADC_DMA_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* enable ADC DMA requests (ADC trigger source is configured in wk_adc1_init) */
  adc_dma_mode_enable(ADC1, TRUE);

  /* start sampling timer (TMR15 CH1 triggers ADC conversions) */
  tmr_counter_value_set(TMR15, 0);
  tmr_counter_enable(TMR15, TRUE);
}

/**
 * @brief  convert ADC value to current in mA
 * @param  adc_current: ADC value from current sensor
 * @param  adc_vref: ADC value from 2.5V reference
 * @retval current in mA
 */
static float adc_to_current_ma(uint16_t adc_current, uint16_t adc_vref)
{
  float v_out_mv;        /* INA180 output voltage in mV */

  /* protect against division by zero */
  if (adc_vref == 0)
    return 0.0f;

  /*
   * Current calculation using measured Vref for calibration:
   * V_out = I_motor * R_sense * Gain
   * I_motor = V_out / (R_sense * Gain)
   * I_motor = V_out / (0.02 * 50) = V_out / 1 = V_out (in mA when V_out is in mV)
   *
   * V_out calculation (using measured Vref for calibration):
   * V_out = (adc_current / adc_vref) * VREF_NOMINAL
   * V_out = adc_current * VREF_NOMINAL / adc_vref
   */

  /* calculate INA180 output voltage using Vref calibration */
  v_out_mv = (float)adc_current * VREF_NOMINAL / (float)adc_vref;

  /* convert to mA (result in mA equals voltage in mV numerically) */
  return v_out_mv;
}

/**
 * @brief  ADC DMA transfer complete ISR callback (DMA1_Channel1_IRQHandler)
 * @param  none
 * @retval none
 */
void MotorMonitor_OnAdcDmaTransferCompleteISR(void)
{
  volatile uint16_t *temp;

  /* swap buffers: active becomes process, process becomes active */
  temp = g_adc_active_buf;
  g_adc_active_buf = g_adc_process_buf;
  g_adc_process_buf = temp;

  /* restart DMA into the new active buffer */
  dma_channel_enable(DMA1_CHANNEL1, FALSE);
  wk_dma_channel_config(DMA1_CHANNEL1,
                        (uint32_t)&ADC1->odt,
                        (uint32_t)g_adc_active_buf,
                        (uint16_t)ADC_DMA_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* mark completed buffer ready for processing */
  g_adc_block_ready = 1;
}

/**
 * @brief  update motor status based on current and motor state
 * @param  motor_id: motor id (0 or 1)
 * @param  current_ma: current in mA
 * @retval motor status
 */
static motor_status_t determine_motor_status(uint8_t motor_id, float current_ma)
{
  motor_state_t motor_state;
  uint8_t is_running;
  uint8_t has_current;
  motor_id_t mid;

  mid = (motor_id == 0) ? MOTOR_ID_1 : MOTOR_ID_2;

  /* get motor running state */
  motor_state = MotorGetState(mid);
  is_running = (motor_state != MOTOR_STATE_STOPPED);

  /* check if current is above threshold */
  has_current = (current_ma > CURRENT_THRESHOLD_MA);

  /* status determination logic */
  if (is_running && has_current)
  {
    /* motor running with current - NORMAL */
    return MOTOR_STATUS_RUNNING;
  }
  else if (is_running && !has_current)
  {
    /* motor running but no current - FAULT (undercurrent) */
    return MOTOR_STATUS_FAULT;
  }
  else if (!is_running && has_current)
  {
    /* motor stopped but has current - FAULT (overcurrent/short circuit) */
    return MOTOR_STATUS_FAULT;
  }
  else
  {
    /* motor stopped with no current - STOPPED */
    return MOTOR_STATUS_STOPPED;
  }
}

/**
 * @brief  update current monitoring data (call every 1 second in TMR3 interrupt)
 * @param  none
 * @retval none
 */
void MotorMonitorUpdate(void)
{
  const uint16_t *samples;
  uint32_t sum_vref = 0, sum_motor1 = 0, sum_motor2 = 0;
  uint16_t min_vref = 0xFFFF, max_vref = 0;
  uint16_t min_motor1 = 0xFFFF, max_motor1 = 0;
  uint16_t min_motor2 = 0xFFFF, max_motor2 = 0;
  uint16_t count = (uint16_t)ADC_SAMPLE_SET_COUNT;
  uint16_t filtered_motor1, filtered_motor2, filtered_vref;
  uint16_t i;

  if (!g_adc_block_ready)
    return;

  /* take ownership of the last completed buffer */
  __disable_irq();
  samples = (const uint16_t *)g_adc_process_buf;
  g_adc_block_ready = 0;
  __enable_irq();

  /* simple trimmed mean: remove min/max and average remaining samples */
  for (i = 0; i < count; i++)
  {
    uint32_t idx = (uint32_t)i * ADC_SAMPLE_CHANNEL_COUNT;
    uint16_t vref = samples[idx + 0];    /* CH5 (PA5): 2.5V reference */
    uint16_t motor1 = samples[idx + 1];  /* CH9 (PB1): Motor 1 current */
    uint16_t motor2 = samples[idx + 2];  /* CH6 (PA6): Motor 2 current */

    sum_vref += vref;
    if (vref < min_vref) min_vref = vref;
    if (vref > max_vref) max_vref = vref;

    sum_motor1 += motor1;
    if (motor1 < min_motor1) min_motor1 = motor1;
    if (motor1 > max_motor1) max_motor1 = motor1;

    sum_motor2 += motor2;
    if (motor2 < min_motor2) min_motor2 = motor2;
    if (motor2 > max_motor2) max_motor2 = motor2;
  }

  if (count > 2)
  {
    sum_vref = sum_vref - min_vref - max_vref;
    sum_motor1 = sum_motor1 - min_motor1 - max_motor1;
    sum_motor2 = sum_motor2 - min_motor2 - max_motor2;
    count -= 2;
  }

  filtered_vref = (uint16_t)(sum_vref / count);
  filtered_motor1 = (uint16_t)(sum_motor1 / count);
  filtered_motor2 = (uint16_t)(sum_motor2 / count);

  /* calculate current for motor 1 */
  motor1_monitor.raw_adc = filtered_motor1;
  motor1_monitor.vref_adc = filtered_vref;
  motor1_monitor.current_ma = adc_to_current_ma(filtered_motor1, filtered_vref);
  motor1_monitor.status = determine_motor_status(0, motor1_monitor.current_ma);

  /* set fault code */
  if (motor1_monitor.status == MOTOR_STATUS_FAULT)
  {
    if (motor1_monitor.current_ma < CURRENT_THRESHOLD_MA)
      motor1_monitor.fault_code = 1;  /* undercurrent */
    else
      motor1_monitor.fault_code = 2;  /* overcurrent */
  }
  else
  {
    motor1_monitor.fault_code = 0;
  }

  /* calculate current for motor 2 */
  motor2_monitor.raw_adc = filtered_motor2;
  motor2_monitor.vref_adc = filtered_vref;
  motor2_monitor.current_ma = adc_to_current_ma(filtered_motor2, filtered_vref);
  motor2_monitor.status = determine_motor_status(1, motor2_monitor.current_ma);

  /* set fault code */
  if (motor2_monitor.status == MOTOR_STATUS_FAULT)
  {
    if (motor2_monitor.current_ma < CURRENT_THRESHOLD_MA)
      motor2_monitor.fault_code = 1;  /* undercurrent */
    else
      motor2_monitor.fault_code = 2;  /* overcurrent */
  }
  else
  {
    motor2_monitor.fault_code = 0;
  }
}

/**
 * @brief  get motor current in mA
 * @param  motor_id: motor id (0 or 1)
 * @retval current in mA
 */
float MotorGetCurrent(uint8_t motor_id)
{
  if (motor_id >= 2)
    return 0.0f;

  if (motor_id == 0)
    return motor1_monitor.current_ma;
  else
    return motor2_monitor.current_ma;
}

/**
 * @brief  get motor status
 * @param  motor_id: motor id (0 or 1)
 * @retval motor status (STOPPED, RUNNING, FAULT)
 */
motor_status_t MotorGetStatus(uint8_t motor_id)
{
  if (motor_id >= 2)
    return MOTOR_STATUS_STOPPED;

  if (motor_id == 0)
    return motor1_monitor.status;
  else
    return motor2_monitor.status;
}

/**
 * @brief  check if motor is in fault condition
 * @param  motor_id: motor id (0 or 1)
 * @retval 1 if fault, 0 if normal
 */
uint8_t MotorIsFault(uint8_t motor_id)
{
  motor_status_t status;

  if (motor_id >= 2)
    return 0;

  status = MotorGetStatus(motor_id);
  return (status == MOTOR_STATUS_FAULT) ? 1 : 0;
}

/**
 * @brief  get fault code
 * @param  motor_id: motor id (0 or 1)
 * @retval fault code: 0=none, 1=undercurrent, 2=overcurrent
 */
uint8_t MotorGetFaultCode(uint8_t motor_id)
{
  if (motor_id >= 2)
    return 0;

  if (motor_id == 0)
    return motor1_monitor.fault_code;
  else
    return motor2_monitor.fault_code;
}

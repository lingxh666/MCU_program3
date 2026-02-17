/**
  ***************************************************************************
  * @file     motor_monitor.h
  * @brief    motor current monitoring and fault detection header file
  ***************************************************************************
  */

#ifndef __MOTOR_MONITOR_H
#define __MOTOR_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f421.h"
#include <stdint.h>

/* ADC channel definitions for current monitoring */
#define ADC_CHAN_VREF_2V5          ADC_CHANNEL_5    /* PA5 - 2.5V reference voltage */
#define ADC_CHAN_MOTOR1_CURRENT    ADC_CHANNEL_9    /* PB1 - Motor 1 current (INA180) */
#define ADC_CHAN_MOTOR2_CURRENT    ADC_CHANNEL_6    /* PA6 - Motor 2 current (INA180) */

/* ADC sampling strategy:
 * - fixed sampling rate from TMR15 CH1 trigger (configured in wk_adc1_init/wk_tmr15_init)
 * - DMA normal mode (non-circular) + double buffering to reduce RAM
 */
#define ADC_SAMPLE_CHANNEL_COUNT   3U
#define ADC_SAMPLE_SET_COUNT       256U
#define ADC_DMA_BUFFER_SIZE        (ADC_SAMPLE_CHANNEL_COUNT * ADC_SAMPLE_SET_COUNT) /* halfwords */

/* Current calculation parameters */
#define ADC_BITS                   4096   /* 12-bit ADC: 0-4095 */
#define VREF_NOMINAL               2500   /* nominal 2.5V reference in mV */
#define SENSE_RESISTOR             20     /* 0.02 ohm = 20mohm */
#define INA180_GAIN                50     /* INA180A2 gain = 50 V/V */

/* Current threshold for fault detection (in mA) */
#define CURRENT_THRESHOLD_MA       10     /* 10mA threshold to consider current as non-zero */

/* Motor status definition */
typedef enum
{
  MOTOR_STATUS_STOPPED = 0,    /* motor stopped, current = 0 */
  MOTOR_STATUS_RUNNING = 1,    /* motor running, current > 0 (normal) */
  MOTOR_STATUS_FAULT = 2       /* fault condition */
} motor_status_t;

/* Motor monitoring data structure */
typedef struct
{
  float current_ma;             /* current in mA */
  motor_status_t status;        /* motor status: STOPPED, RUNNING, FAULT */
  uint16_t raw_adc;             /* raw ADC value */
  uint16_t vref_adc;            /* reference voltage ADC value */
  uint8_t fault_code;           /* fault code: 0=none, 1=undercurrent, 2=overcurrent */
} motor_monitor_data_t;

/* Global monitoring data for both motors */
extern motor_monitor_data_t motor1_monitor;
extern motor_monitor_data_t motor2_monitor;

/* Function prototypes */

/**
 * @brief  initialize motor current monitoring
 * @param  none
 * @retval none
 * @note   configures DMA buffer and starts ADC continuous conversion
 */
void MotorMonitorInit(void);

/**
 * @brief  update current monitoring data (call every 1 second in TMR3 interrupt)
 * @param  none
 * @retval none
 * @note   processes ADC samples, applies filtering, calculates current
 */
void MotorMonitorUpdate(void);

/**
 * @brief  ADC DMA transfer complete ISR callback
 * @param  none
 * @retval none
 * @note   called from DMA1_Channel1_IRQHandler
 */
void MotorMonitor_OnAdcDmaTransferCompleteISR(void);

/**
 * @brief  get motor current in mA
 * @param  motor_id: motor id (0 or 1)
 * @retval current in mA
 */
float MotorGetCurrent(uint8_t motor_id);

/**
 * @brief  get motor status
 * @param  motor_id: motor id (0 or 1)
 * @retval motor status (STOPPED, RUNNING, FAULT)
 */
motor_status_t MotorGetStatus(uint8_t motor_id);

/**
 * @brief  check if motor is in fault condition
 * @param  motor_id: motor id (0 or 1)
 * @retval 1 if fault, 0 if normal
 */
uint8_t MotorIsFault(uint8_t motor_id);

/**
 * @brief  get fault code
 * @param  motor_id: motor id (0 or 1)
 * @retval fault code: 0=none, 1=undercurrent, 2=overcurrent
 */
uint8_t MotorGetFaultCode(uint8_t motor_id);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_MONITOR_H */

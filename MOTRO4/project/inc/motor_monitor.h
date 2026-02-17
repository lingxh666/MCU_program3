/**
  ***************************************************************************
  * @file     motor_monitor.h
  * @brief    四路电机电流监测头文件
  *           ADC DMA循环模式 + HDT/FDT双缓冲 + 去极值均值滤波
  ***************************************************************************
  */

#ifndef __MOTOR_MONITOR_H
#define __MOTOR_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f422_426.h"
#include <stdint.h>

/* ADC通道顺序（与wk_adc1_init中的序列一致）
 * 序列1: CH3 (PA3) = AD4 加药泵电流
 * 序列2: CH4 (PA4) = AD3 留样转盘电流
 * 序列3: CH5 (PA5) = AD1 采样电流
 * 序列4: CH6 (PA6) = AD2 送留样电流
 */
#define ADC_CHANNEL_COUNT       4U
#define ADC_SAMPLES_PER_HALF    128U
#define ADC_DMA_HALF_SIZE       (ADC_CHANNEL_COUNT * ADC_SAMPLES_PER_HALF)  /* 512 halfwords */
#define ADC_DMA_TOTAL_SIZE      (ADC_DMA_HALF_SIZE * 2)                     /* 1024 halfwords */

/* DMA缓冲区中各电机电流的偏移（每组4个采样值） */
#define ADC_IDX_MOTOR4          0   /* CH3: 加药泵 */
#define ADC_IDX_MOTOR3          1   /* CH4: 留样转盘 */
#define ADC_IDX_MOTOR1          2   /* CH5: 采样 */
#define ADC_IDX_MOTOR2          3   /* CH6: 送留样 */

/* 电流计算参数
 * INA180A2: 增益50, 采样电阻0.02Ω
 * V_out = I × 0.02 × 50 = I × 1.0
 * I(mA) = V_out(mV) = ADC_value × 3300 / 4096
 */
#define ADC_FULL_SCALE          4096
#define VREF_MV                 3300

/* 故障检测电流阈值(mA) */
#define CURRENT_THRESHOLD_MA    10

/* 电机监测状态 */
typedef enum
{
  MONITOR_STATUS_STOPPED = 0,   /* 停止且无电流 */
  MONITOR_STATUS_RUNNING = 1,   /* 运行且有电流（正常） */
  MONITOR_STATUS_FAULT   = 2    /* 故障（电流与运行状态不匹配） */
} monitor_status_t;

/* 单路电机监测数据 */
typedef struct
{
  uint16_t current_ma;          /* 电流值(mA) */
  uint16_t raw_adc;             /* 滤波后ADC原始值 */
  monitor_status_t status;      /* 监测状态 */
  uint8_t fault_code;           /* 故障码: 0=无, 1=欠流, 2=过流 */
} motor_monitor_data_t;

/* 全局监测数据 */
extern motor_monitor_data_t motor_monitor[4];

/* ADC DMA缓冲区（供wk_config.h引用） */
extern uint16_t adc_dma_buf[ADC_DMA_TOTAL_SIZE];

/* 函数声明 */
void MotorMonitorInit(void);
void MotorMonitorUpdate(void);
void MotorMonitor_DmaHalfISR(void);
void MotorMonitor_DmaFullISR(void);
uint16_t MotorGetCurrentMA(uint8_t motor_id);
monitor_status_t MotorGetMonitorStatus(uint8_t motor_id);
uint8_t MotorGetFaultCode(uint8_t motor_id);

#ifdef __cplusplus
}
#endif

#endif /* __MOTOR_MONITOR_H */

/**
  ***************************************************************************
  * @file     stepper_motor.h
  * @brief    四路步进电机控制头文件
  ***************************************************************************
  */

#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f422_426.h"
#include <stdint.h>

/* 电机参数 */
#define MOTOR_STEP_ANGLE     1.8f
#define MOTOR_MICROSTEPS     16
#define MOTOR_STEPS_PER_REV  200
#define MOTOR_PULSES_PER_REV (MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS)  /* 3200 */

/* RPM与频率转换 */
#define RPM_TO_HZ(rpm)       ((uint32_t)((uint32_t)(rpm) * MOTOR_PULSES_PER_REV / 60))
#define HZ_TO_RPM(hz)        ((uint16_t)((uint32_t)(hz) * 60 / MOTOR_PULSES_PER_REV))

/* 加减速参数 */
#define DEFAULT_ACCEL_RPM_PER_SEC   500
#define UPDATE_FREQ_HZ              1000   /* TMR6 1kHz调用 */

/* RPM范围 */
#define MIN_RPM              1
#define MAX_RPM              1000

/* 电机ID */
typedef enum
{
  MOTOR_ID_1 = 0,   /* 采样电机:   TMR17_CH1 (PB9),  CW1 (PB8) */
  MOTOR_ID_2 = 1,   /* 送留样电机: TMR4_CH2  (PB7),  CW2 (PF6) */
  MOTOR_ID_3 = 2,   /* 留样转盘:   TMR3_CH2  (PB5),  CW3 (PF7) */
  MOTOR_ID_4 = 3,   /* 加药泵:     TMR3_CH1  (PB4),  CW4 (PB6) */
  MOTOR_COUNT = 4
} motor_id_t;

/* 电机方向 */
typedef enum
{
  MOTOR_DIR_CW  = 0,
  MOTOR_DIR_CCW = 1
} motor_dir_t;

/* 电机状态 */
typedef enum
{
  MOTOR_STATE_STOPPED = 0,
  MOTOR_STATE_ACCELERATING,
  MOTOR_STATE_RUNNING,
  MOTOR_STATE_DECELERATING
} motor_state_t;

/* 电机配置结构体 */
typedef struct
{
  tmr_type *tmr;
  tmr_channel_select_type ch;
  gpio_type *dir_port;
  uint16_t dir_pin;
  uint16_t current_freq;
  uint16_t target_freq;
  uint16_t current_rpm;
  uint16_t target_rpm;
  int16_t accel_rpm_per_s;
  motor_dir_t direction;
  motor_state_t state;
  uint8_t stop_request;
  uint8_t is_advanced_tmr;     /* TMR17等高级定时器需要output_enable */
  uint8_t shares_tmr;          /* 共享定时器标记(TMR3 CH1/CH2) */
  uint16_t accel_tick;         /* 加速分频计数器 */
} motor_config_t;

/* 函数声明 */
void stepper_init(void);
void MotorRun(motor_id_t motor_id, uint16_t rpm, motor_dir_t dir);
void MotorStop(motor_id_t motor_id);
void MotorImmediateStop(motor_id_t motor_id);
void MotorSetAcceleration(motor_id_t motor_id, int16_t accel_rpm_per_s);
void MotorUpdate(motor_id_t motor_id);
motor_state_t MotorGetState(motor_id_t motor_id);
uint16_t MotorGetSpeedRPM(motor_id_t motor_id);
motor_dir_t MotorGetDirection(motor_id_t motor_id);
void MotorUpdateLEDIndicator(void);

#ifdef __cplusplus
}
#endif

#endif /* __STEPPER_MOTOR_H */

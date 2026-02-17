/**
  **************************************************************************
  * @file     stepper_motor.h
  * @brief    stepper motor control header file
  **************************************************************************
  */

#ifndef __STEPPER_MOTOR_H
#define __STEPPER_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f421.h"
#include <stdint.h>

/* motor parameters */
#define MOTOR_STEP_ANGLE     1.8f    /* step angle in degrees */
#define MOTOR_MICROSTEPS     16      /* microsteps (1/16) */
#define MOTOR_STEPS_PER_REV  200     /* steps per revolution = 360 / 1.8 */
#define MOTOR_PULSES_PER_REV (MOTOR_STEPS_PER_REV * MOTOR_MICROSTEPS)  /* 3200 pulses/rev */

/* RPM to PWM frequency conversion */
#define RPM_TO_HZ(rpm)       ((uint16_t)((rpm) * MOTOR_PULSES_PER_REV / 60))
#define HZ_TO_RPM(hz)        ((uint16_t)((hz) * 60 / MOTOR_PULSES_PER_REV))

/* acceleration/deceleration settings */
#define DEFAULT_ACCEL_RPM_PER_SEC   500    /* default acceleration: 500 RPM/s */
#define UPDATE_FREQ_HZ               1000   /* acceleration update frequency: 1kHz */

/* motor id definition */
typedef enum
{
  MOTOR_ID_1 = 0,
  MOTOR_ID_2 = 1
} motor_id_t;

/* motor direction definition */
typedef enum
{
  MOTOR_DIR_CW = 0,    /* clockwise / forward */
  MOTOR_DIR_CCW = 1    /* counter-clockwise / reverse */
} motor_dir_t;

/* motor state definition */
typedef enum
{
  MOTOR_STATE_STOPPED = 0,
  MOTOR_STATE_ACCELERATING,
  MOTOR_STATE_RUNNING,
  MOTOR_STATE_DECELERATING
} motor_state_t;

/* motor configuration structure */
typedef struct
{
  tmr_type *tmr;              /* timer pointer */
  tmr_channel_select_type ch; /* timer channel */
  gpio_type *dir_port;        /* direction GPIO port */
  uint16_t dir_pin;           /* direction GPIO pin */
  uint16_t current_freq;      /* current frequency in Hz */
  uint16_t target_freq;       /* target frequency in Hz */
  uint16_t current_rpm;       /* current speed in RPM */
  uint16_t target_rpm;        /* target speed in RPM */
  int16_t accel_rpm_per_s;    /* acceleration in RPM/s */
  motor_dir_t direction;      /* current direction */
  motor_state_t state;        /* motor state */
  uint8_t stop_request;       /* stop request flag */
} motor_config_t;

/* function prototypes */

/**
 * @brief  initialize stepper motor control
 * @param  none
 * @retval none
 */
void stepper_init(void);

/**
 * @brief  motor run with soft start
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  rpm: target speed in RPM (1 - 1000)
 * @param  dir: direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 * @retval none
 * @note   this function enables soft start (acceleration)
 */
void MotorRun(motor_id_t motor_id, uint16_t rpm, motor_dir_t dir);

/**
 * @brief  motor soft stop
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 * @note   this function decelerates the motor to stop smoothly
 */
void MotorStop(motor_id_t motor_id);

/**
 * @brief  motor immediate stop (emergency stop)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 * @note   this function stops the motor immediately without deceleration
 */
void MotorImmediateStop(motor_id_t motor_id);

/**
 * @brief  set motor acceleration rate
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  accel_rpm_per_s: acceleration in RPM per second (10 - 2000)
 * @retval none
 */
void MotorSetAcceleration(motor_id_t motor_id, int16_t accel_rpm_per_s);

/**
 * @brief  update motor speed (call periodically in timer interrupt or main loop)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 * @note   should be called at UPDATE_FREQ_HZ (1000Hz) for smooth acceleration
 *         or can be called in main loop with proper timing
 */
void MotorUpdate(motor_id_t motor_id);

/**
 * @brief  get motor current state
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval motor state (MOTOR_STATE_XXX)
 */
motor_state_t MotorGetState(motor_id_t motor_id);

/**
 * @brief  get motor current speed in RPM
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval current speed in RPM
 */
uint16_t MotorGetSpeedRPM(motor_id_t motor_id);

/**
 * @brief  get motor current direction
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval motor direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 */
motor_dir_t MotorGetDirection(motor_id_t motor_id);

/**
 * @brief  update LED indicators (call every 1 second in TMR3 interrupt)
 * @param  none
 * @retval none
 * @note   LED flashes at 1Hz when motor is running, off when stopped
 *         gpio_bits_set() = LED off (high level)
 *         gpio_bits_reset() = LED on (low level)
 */
void MotorUpdateLEDIndicator(void);

/* ========== Legacy functions (for backward compatibility) ========== */

/**
 * @brief  set motor pulse frequency (speed control)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  freq_hz: pulse frequency in Hz (1 - 50000)
 * @retval none
 */
void stepper_set_speed(motor_id_t motor_id, uint16_t freq_hz);

/**
 * @brief  set motor direction
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  dir: direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 * @retval none
 */
void stepper_set_dir(motor_id_t motor_id, motor_dir_t dir);

/**
 * @brief  start motor (enable PWM output)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void stepper_start(motor_id_t motor_id);

/**
 * @brief  stop motor (disable PWM output)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void stepper_stop(motor_id_t motor_id);

#ifdef __cplusplus
}
#endif

#endif /* __STEPPER_MOTOR_H */

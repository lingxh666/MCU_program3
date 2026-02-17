/**
  **************************************************************************
  * @file     stepper_motor.c
  * @brief    stepper motor control implementation with acceleration/deceleration
  **************************************************************************
  */

#include "stepper_motor.h"
#include "at32f421_wk_config.h"

/* motor configuration table - ARM Compiler 5 compatible initialization */
static volatile motor_config_t motor_config[2] = {
  /* MOTOR 1: TMR17_CH1 (PA7), Direction: PA10 */
  {
    TMR17,
    TMR_SELECT_CHANNEL_1,
    GPIOA,
    GPIO_PINS_10,
    0,     /* current_freq */
    0,     /* target_freq */
    0,     /* current_rpm */
    0,     /* target_rpm */
    DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW,
    MOTOR_STATE_STOPPED,
    0      /* stop_request */
  },
  /* MOTOR 2: TMR14_CH1 (PA4), Direction: PA0 */
  {
    TMR14,
    TMR_SELECT_CHANNEL_1,
    GPIOA,
    GPIO_PINS_0,
    0,     /* current_freq */
    0,     /* target_freq */
    0,     /* current_rpm */
    0,     /* target_rpm */
    DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW,
    MOTOR_STATE_STOPPED,
    0      /* stop_request */
  }
};

/* system clock frequency (72MHz for AT32F421) */
#define TIMER_CLOCK_FREQ     72000000UL

/* frequency range limits */
#define MIN_FREQ_HZ          1
#define MAX_FREQ_HZ          60000  /* Support 1000RPM: 1000*3200/60 = 53.33kHz */

/* RPM limits */
#define MIN_RPM              1
#define MAX_RPM              1000

static inline uint32_t motor_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static inline void motor_exit_critical(uint32_t primask)
{
  if (primask == 0U)
    __enable_irq();
}

/**
 * @brief  internal function to set PWM frequency
 */
static void set_pwm_frequency(motor_id_t motor_id, uint16_t freq_hz)
{
  uint32_t psc, arr;
  tmr_type *tmr;

  if (motor_id >= 2)
    return;

  tmr = motor_config[motor_id].tmr;

  /* limit frequency range */
  if (freq_hz < MIN_FREQ_HZ)
    freq_hz = MIN_FREQ_HZ;
  if (freq_hz > MAX_FREQ_HZ)
    freq_hz = MAX_FREQ_HZ;

  /* choose ARR to get good resolution */
  if (freq_hz >= 1000)
  {
    arr = 71;   /* ARR = 72-1 */
  }
  else if (freq_hz >= 100)
  {
    arr = 719;  /* ARR = 720-1 */
  }
  else
  {
    arr = 7199; /* ARR = 7200-1 */
  }

  /* calculate prescaler */
  psc = (TIMER_CLOCK_FREQ / (uint32_t)freq_hz / (arr + 1)) - 1;

  if (psc > 65535)
    psc = 65535;

  /* disable timer before reconfiguration */
  tmr_counter_enable(tmr, FALSE);

  /* set new prescaler and period */
  tmr_period_value_set(tmr, arr);
  tmr_clock_source_div_set(tmr, TMR_CLOCK_DIV1);
  tmr_base_init(tmr, arr, psc);
  tmr_channel_value_set(tmr, motor_config[motor_id].ch, arr / 2);

  /* re-enable timer */
  tmr_counter_enable(tmr, TRUE);

  /* store current frequency */
  motor_config[motor_id].current_freq = freq_hz;
}

/**
 * @brief  initialize stepper motor control
 * @param  none
 * @retval none
 */
void stepper_init(void)
{
  uint32_t primask = motor_enter_critical();

  /* set direction pins initial state */
  gpio_bits_reset(motor_config[MOTOR_ID_1].dir_port, motor_config[MOTOR_ID_1].dir_pin);
  gpio_bits_reset(motor_config[MOTOR_ID_2].dir_port, motor_config[MOTOR_ID_2].dir_pin);

  /* initialize motor states */
  motor_config[MOTOR_ID_1].state = MOTOR_STATE_STOPPED;
  motor_config[MOTOR_ID_1].current_rpm = 0;
  motor_config[MOTOR_ID_1].target_rpm = 0;
  motor_config[MOTOR_ID_1].current_freq = 0;
  motor_config[MOTOR_ID_1].target_freq = 0;
  motor_config[MOTOR_ID_1].stop_request = 0;

  motor_config[MOTOR_ID_2].state = MOTOR_STATE_STOPPED;
  motor_config[MOTOR_ID_2].current_rpm = 0;
  motor_config[MOTOR_ID_2].target_rpm = 0;
  motor_config[MOTOR_ID_2].current_freq = 0;
  motor_config[MOTOR_ID_2].target_freq = 0;
  motor_config[MOTOR_ID_2].stop_request = 0;

  motor_exit_critical(primask);
}

/**
 * @brief  motor run with soft start
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  rpm: target speed in RPM (1 - 1000)
 * @param  dir: direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 * @retval none
 */
void MotorRun(motor_id_t motor_id, uint16_t rpm, motor_dir_t dir)
{
  uint16_t target_freq;
  uint32_t primask;

  if (motor_id >= 2)
    return;

  /* limit RPM range */
  if (rpm < MIN_RPM)
    rpm = MIN_RPM;
  if (rpm > MAX_RPM)
    rpm = MAX_RPM;

  target_freq = RPM_TO_HZ(rpm);
  primask = motor_enter_critical();

  /* set direction first */
  motor_config[motor_id].direction = dir;
  if (dir == MOTOR_DIR_CW)
  {
    gpio_bits_reset(motor_config[motor_id].dir_port, motor_config[motor_id].dir_pin);
  }
  else
  {
    gpio_bits_set(motor_config[motor_id].dir_port, motor_config[motor_id].dir_pin);
  }

  /* set target RPM and frequency */
  motor_config[motor_id].target_rpm = rpm;
  motor_config[motor_id].target_freq = target_freq;
  motor_config[motor_id].stop_request = 0;

  /* enable timer counter if it was disabled */
  tmr_counter_enable(motor_config[motor_id].tmr, TRUE);

  /* enable timer output */
  tmr_output_enable(motor_config[motor_id].tmr, TRUE);

  /* set state to accelerating if target > current */
  if (motor_config[motor_id].current_rpm < rpm)
  {
    motor_config[motor_id].state = MOTOR_STATE_ACCELERATING;
  }
  else
  {
    motor_config[motor_id].state = MOTOR_STATE_RUNNING;
  }

  motor_exit_critical(primask);
}

/**
 * @brief  motor soft stop
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void MotorStop(motor_id_t motor_id)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  /* set stop request flag - will decelerate in MotorUpdate */
  motor_config[motor_id].stop_request = 1;
  motor_config[motor_id].state = MOTOR_STATE_DECELERATING;
  motor_exit_critical(primask);
}

/**
 * @brief  motor immediate stop (emergency stop)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void MotorImmediateStop(motor_id_t motor_id)
{
  tmr_type *tmr;
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  tmr = motor_config[motor_id].tmr;

  /* disable PWM output immediately */
  tmr_output_enable(tmr, FALSE);

  /* set channel compare value to 0 to ensure no pulse output */
  tmr_channel_value_set(tmr, motor_config[motor_id].ch, 0);

  /* disable timer counter to ensure complete stop */
  tmr_counter_enable(tmr, FALSE);

  /* reset state */
  motor_config[motor_id].current_rpm = 0;
  motor_config[motor_id].current_freq = 0;
  motor_config[motor_id].target_rpm = 0;
  motor_config[motor_id].target_freq = 0;
  motor_config[motor_id].state = MOTOR_STATE_STOPPED;
  motor_config[motor_id].stop_request = 0;

  motor_exit_critical(primask);
}

/**
 * @brief  set motor acceleration rate
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  accel_rpm_per_s: acceleration in RPM per second (10 - 2000)
 * @retval none
 */
void MotorSetAcceleration(motor_id_t motor_id, int16_t accel_rpm_per_s)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  if (accel_rpm_per_s < 10)
    accel_rpm_per_s = 10;
  if (accel_rpm_per_s > 2000)
    accel_rpm_per_s = 2000;

  primask = motor_enter_critical();
  motor_config[motor_id].accel_rpm_per_s = accel_rpm_per_s;
  motor_exit_critical(primask);
}

/**
 * @brief  update motor speed (call periodically at 1kHz for smooth acceleration)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void MotorUpdate(motor_id_t motor_id)
{
  uint16_t new_rpm;
  int16_t rpm_step;

  if (motor_id >= 2)
    return;

  /* keep STOPPED state when motor is idle (0 RPM target and current) */
  if ((motor_config[motor_id].stop_request == 0) &&
      (motor_config[motor_id].target_rpm == 0) &&
      (motor_config[motor_id].current_rpm == 0))
  {
    motor_config[motor_id].state = MOTOR_STATE_STOPPED;
    return;
  }

  /* calculate RPM step per update (ensure minimum of 1 RPM) */
  /* formula: rpm_step = accel / update_freq */
  /* example: 500 RPM/s / 1000 Hz = 0.5 RPM per update -> round to 1 RPM */
  rpm_step = (motor_config[motor_id].accel_rpm_per_s + UPDATE_FREQ_HZ - 1) / UPDATE_FREQ_HZ;
  if (rpm_step < 1)
    rpm_step = 1;

  /* handle deceleration (stop request) */
  if (motor_config[motor_id].stop_request)
  {
    if (motor_config[motor_id].current_rpm > (uint16_t)rpm_step)
    {
      /* decelerate */
      new_rpm = motor_config[motor_id].current_rpm - rpm_step;
    }
    else
    {
      /* reached zero speed - stop motor completely */
      new_rpm = 0;
      motor_config[motor_id].current_rpm = 0;
      motor_config[motor_id].current_freq = 0;
      motor_config[motor_id].target_rpm = 0;
      motor_config[motor_id].target_freq = 0;
      motor_config[motor_id].state = MOTOR_STATE_STOPPED;
      motor_config[motor_id].stop_request = 0;

      /* disable PWM output and ensure clean stop */
      tmr_output_enable(motor_config[motor_id].tmr, FALSE);
      tmr_channel_value_set(motor_config[motor_id].tmr, motor_config[motor_id].ch, 0);
      tmr_counter_enable(motor_config[motor_id].tmr, FALSE);
      return;
    }
  }
  /* handle acceleration to target speed */
  else if (motor_config[motor_id].current_rpm < motor_config[motor_id].target_rpm)
  {
    motor_config[motor_id].state = MOTOR_STATE_ACCELERATING;

    if ((motor_config[motor_id].target_rpm - motor_config[motor_id].current_rpm) <= (uint16_t)rpm_step)
    {
      /* reached target speed */
      new_rpm = motor_config[motor_id].target_rpm;
      motor_config[motor_id].state = MOTOR_STATE_RUNNING;
    }
    else
    {
      /* accelerate */
      new_rpm = motor_config[motor_id].current_rpm + rpm_step;
    }
  }
  /* handle deceleration to lower target speed (e.g., Modbus reduces speed) */
  else if (motor_config[motor_id].current_rpm > motor_config[motor_id].target_rpm)
  {
    motor_config[motor_id].state = MOTOR_STATE_DECELERATING;

    if ((motor_config[motor_id].current_rpm - motor_config[motor_id].target_rpm) <= (uint16_t)rpm_step)
    {
      /* reached target speed */
      new_rpm = motor_config[motor_id].target_rpm;
      motor_config[motor_id].state = MOTOR_STATE_RUNNING;
    }
    else
    {
      /* decelerate */
      new_rpm = motor_config[motor_id].current_rpm - rpm_step;
    }
  }
  else
  {
    /* already at target speed */
    motor_config[motor_id].state = (motor_config[motor_id].target_rpm == 0) ? MOTOR_STATE_STOPPED : MOTOR_STATE_RUNNING;
    return;
  }

  /* update motor speed */
  motor_config[motor_id].current_rpm = new_rpm;
  motor_config[motor_id].current_freq = RPM_TO_HZ(new_rpm);

  /* reached 0 RPM via target reduction: force a clean stop */
  if (new_rpm == 0)
  {
    motor_config[motor_id].current_rpm = 0;
    motor_config[motor_id].current_freq = 0;
    motor_config[motor_id].target_rpm = 0;
    motor_config[motor_id].target_freq = 0;
    motor_config[motor_id].state = MOTOR_STATE_STOPPED;
    motor_config[motor_id].stop_request = 0;

    tmr_output_enable(motor_config[motor_id].tmr, FALSE);
    tmr_channel_value_set(motor_config[motor_id].tmr, motor_config[motor_id].ch, 0);
    tmr_counter_enable(motor_config[motor_id].tmr, FALSE);
    return;
  }

  /* apply new PWM frequency */
  set_pwm_frequency(motor_id, motor_config[motor_id].current_freq);
}

/**
 * @brief  get motor current state
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval motor state
 */
motor_state_t MotorGetState(motor_id_t motor_id)
{
  if (motor_id >= 2)
    return MOTOR_STATE_STOPPED;

  return motor_config[motor_id].state;
}

/**
 * @brief  get motor current speed in RPM
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval current speed in RPM
 */
uint16_t MotorGetSpeedRPM(motor_id_t motor_id)
{
  if (motor_id >= 2)
    return 0;

  return motor_config[motor_id].current_rpm;
}

/**
 * @brief  get motor current direction
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval motor direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 */
motor_dir_t MotorGetDirection(motor_id_t motor_id)
{
  if (motor_id >= 2)
    return MOTOR_DIR_CW;

  return motor_config[motor_id].direction;
}

/**
 * @brief  update LED indicators (call every 1 second in TMR3 interrupt)
 * @param  none
 * @retval none
 * @note   LED flashes at 1Hz when motor is running, off when stopped
 */
void MotorUpdateLEDIndicator(void)
{
  /* static variable to track LED state (toggle each second) */
  static uint8_t led_state = 0;
  motor_state_t state1, state2;

  /* get motor states */
  state1 = motor_config[MOTOR_ID_1].state;
  state2 = motor_config[MOTOR_ID_2].state;

  /* toggle LED state each call (every 1 second) */
  led_state = !led_state;

  /* LED1 control */
  if (state1 == MOTOR_STATE_STOPPED)
  {
    /* motor stopped - LED off */
    gpio_bits_set(LED1_GPIO_PORT, LED1_PIN);
  }
  else
  {
    /* motor running - flash LED */
    if (led_state)
      gpio_bits_reset(LED1_GPIO_PORT, LED1_PIN);  /* LED on */
    else
      gpio_bits_set(LED1_GPIO_PORT, LED1_PIN);    /* LED off */
  }

  /* LED2 control */
  if (state2 == MOTOR_STATE_STOPPED)
  {
    /* motor stopped - LED off */
    gpio_bits_set(LED2_GPIO_PORT, LED2_PIN);
  }
  else
  {
    /* motor running - flash LED */
    if (led_state)
      gpio_bits_reset(LED2_GPIO_PORT, LED2_PIN);  /* LED on */
    else
      gpio_bits_set(LED2_GPIO_PORT, LED2_PIN);    /* LED off */
  }
}

/* ========== Legacy functions (for backward compatibility) ========== */

/**
 * @brief  set motor pulse frequency (speed control)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  freq_hz: pulse frequency in Hz (1 - 50000)
 * @retval none
 */
void stepper_set_speed(motor_id_t motor_id, uint16_t freq_hz)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  motor_config[motor_id].current_freq = freq_hz;
  motor_config[motor_id].current_rpm = HZ_TO_RPM(freq_hz);
  motor_exit_critical(primask);
  set_pwm_frequency(motor_id, freq_hz);
}

/**
 * @brief  set motor direction
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @param  dir: direction (MOTOR_DIR_CW or MOTOR_DIR_CCW)
 * @retval none
 */
void stepper_set_dir(motor_id_t motor_id, motor_dir_t dir)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  motor_config[motor_id].direction = dir;
  if (dir == MOTOR_DIR_CW)
  {
    gpio_bits_reset(motor_config[motor_id].dir_port, motor_config[motor_id].dir_pin);
  }
  else
  {
    gpio_bits_set(motor_config[motor_id].dir_port, motor_config[motor_id].dir_pin);
  }
  motor_exit_critical(primask);
}

/**
 * @brief  start motor (enable PWM output)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void stepper_start(motor_id_t motor_id)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  motor_config[motor_id].state = MOTOR_STATE_RUNNING;
  tmr_output_enable(motor_config[motor_id].tmr, TRUE);
  motor_exit_critical(primask);
}

/**
 * @brief  stop motor (disable PWM output)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval none
 */
void stepper_stop(motor_id_t motor_id)
{
  uint32_t primask;

  if (motor_id >= 2)
    return;

  primask = motor_enter_critical();
  motor_config[motor_id].state = MOTOR_STATE_STOPPED;
  tmr_output_enable(motor_config[motor_id].tmr, FALSE);
  motor_exit_critical(primask);
}

/**
 * @brief  get current motor frequency (legacy)
 * @param  motor_id: motor id (MOTOR_ID_1 or MOTOR_ID_2)
 * @retval current frequency in Hz
 */
uint16_t stepper_get_speed(motor_id_t motor_id)
{
  if (motor_id >= 2)
    return 0;

  return motor_config[motor_id].current_freq;
}

/**
  ***************************************************************************
  * @file     stepper_motor.c
  * @brief    四路步进电机控制实现（加减速）
  *           STEP1: TMR17_CH1(PB9)  采样电机     独立
  *           STEP2: TMR4_CH2 (PB7)  送留样电机   独立
  *           STEP3: TMR3_CH2 (PB5)  留样转盘     与STEP4共享TMR3
  *           STEP4: TMR3_CH1 (PB4)  加药泵       与STEP3共享TMR3
  ***************************************************************************
  */

#include "stepper_motor.h"
#include "motor_monitor.h"
#include "at32f422_426_wk_config.h"
#include <stdio.h>

/* 定时器时钟频率 160MHz */
#define TIMER_CLOCK_FREQ     160000000UL

/* 频率范围 */
#define MIN_FREQ_HZ          1
#define MAX_FREQ_HZ          60000

/* 电机配置表 */
static volatile motor_config_t motor_config[MOTOR_COUNT] = {
  /* MOTOR_ID_1: 采样电机 TMR17_CH1 (PB9), CW1 (PB8), 高级定时器 */
  { TMR17, TMR_SELECT_CHANNEL_1, CW1_GPIO_PORT, CW1_PIN,
    0, 0, 0, 0, DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW, MOTOR_STATE_STOPPED, 0, 1, 0 },
  /* MOTOR_ID_2: 送留样电机 TMR4_CH2 (PB7), CW2 (PF6), 独立 */
  { TMR4, TMR_SELECT_CHANNEL_2, CW2_GPIO_PORT, CW2_PIN,
    0, 0, 0, 0, DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW, MOTOR_STATE_STOPPED, 0, 0, 0 },
  /* MOTOR_ID_3: 留样转盘 TMR3_CH2 (PB5), CW3 (PF7), 共享TMR3 */
  { TMR3, TMR_SELECT_CHANNEL_2, CW3_GPIO_PORT, CW3_PIN,
    0, 0, 0, 0, DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW, MOTOR_STATE_STOPPED, 0, 0, 1 },
  /* MOTOR_ID_4: 加药泵 TMR3_CH1 (PB4), CW4 (PB6), 共享TMR3 */
  { TMR3, TMR_SELECT_CHANNEL_1, CW4_GPIO_PORT, CW4_PIN,
    0, 0, 0, 0, DEFAULT_ACCEL_RPM_PER_SEC,
    MOTOR_DIR_CW, MOTOR_STATE_STOPPED, 0, 0, 1 }
};

static uint32_t motor_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void motor_exit_critical(uint32_t primask)
{
  if (primask == 0U)
    __enable_irq();
}

/**
 * @brief  获取共享TMR3的另一个电机ID
 */
static motor_id_t get_tmr3_peer(motor_id_t id)
{
  if (id == MOTOR_ID_3) return MOTOR_ID_4;
  if (id == MOTOR_ID_4) return MOTOR_ID_3;
  return id;
}

/**
 * @brief  将电机复位到完全停止状态（清零速度、频率、停止请求）
 */
static void reset_motor_state(volatile motor_config_t *m)
{
  m->current_rpm  = 0;
  m->current_freq = 0;
  m->target_rpm   = 0;
  m->target_freq  = 0;
  m->state        = MOTOR_STATE_STOPPED;
  m->stop_request = 0;
}

/**
 * @brief  设置PWM频率
 */
static void set_pwm_frequency(motor_id_t motor_id, uint16_t freq_hz)
{
  uint32_t psc, arr;
  volatile motor_config_t *m = &motor_config[motor_id];
  tmr_type *tmr = m->tmr;

  if (freq_hz < MIN_FREQ_HZ) freq_hz = MIN_FREQ_HZ;
  if (freq_hz > MAX_FREQ_HZ) freq_hz = MAX_FREQ_HZ;

  /* 根据频率选择ARR以获得较好分辨率 */
  if (freq_hz >= 1000)
    arr = 159;    /* 160分频 */
  else if (freq_hz >= 100)
    arr = 1599;   /* 1600分频 */
  else
    arr = 15999;  /* 16000分频 */

  psc = (TIMER_CLOCK_FREQ / (uint32_t)freq_hz / (arr + 1)) - 1;
  if (psc > 65535) psc = 65535;

  tmr_counter_enable(tmr, FALSE);
  tmr_base_init(tmr, arr, psc);
  tmr_channel_value_set(tmr, m->ch, (arr + 1) / 2);

  /* 共享TMR3时，确保对方通道的compare值保持正确 */
  if (m->shares_tmr)
  {
    motor_id_t peer = get_tmr3_peer(motor_id);
    uint32_t peer_cmp = (motor_config[peer].state != MOTOR_STATE_STOPPED)
                        ? (arr + 1) / 2 : 0;
    tmr_channel_value_set(tmr, motor_config[peer].ch, peer_cmp);
  }

  tmr_counter_enable(tmr, TRUE);
  m->current_freq = freq_hz;
}

/**
 * @brief  停止PWM输出（不改变定时器基础配置）
 */
static void stop_pwm_output(motor_id_t motor_id)
{
  volatile motor_config_t *m = &motor_config[motor_id];
  tmr_type *tmr = m->tmr;

  /* 将compare值设为0，停止脉冲输出 */
  tmr_channel_value_set(tmr, m->ch, 0);

  if (m->is_advanced_tmr)
  {
    /* 高级定时器额外关闭主输出 */
    tmr_output_enable(tmr, FALSE);
    tmr_counter_enable(tmr, FALSE);
  }
  else if (!m->shares_tmr)
  {
    /* 独立通用定时器，可以关闭计数器 */
    tmr_counter_enable(tmr, FALSE);
  }
  /* 共享TMR3时不关闭计数器，对方可能在用 */
}

void stepper_init(void)
{
  uint8_t i;
  uint32_t primask = motor_enter_critical();

  for (i = 0; i < MOTOR_COUNT; i++)
  {
    gpio_bits_reset(motor_config[i].dir_port, motor_config[i].dir_pin);
    reset_motor_state(&motor_config[i]);
  }

  motor_exit_critical(primask);
}

void MotorRun(motor_id_t motor_id, uint16_t rpm, motor_dir_t dir)
{
  uint32_t primask;
  volatile motor_config_t *m;

  if (motor_id >= MOTOR_COUNT) return;
  if (rpm < MIN_RPM) rpm = MIN_RPM;
  if (rpm > MAX_RPM) rpm = MAX_RPM;

  primask = motor_enter_critical();
  m = &motor_config[motor_id];

  /* 共享TMR3互斥检查：如果对方正在运行，拒绝启动 */
  if (m->shares_tmr)
  {
    motor_id_t peer = get_tmr3_peer(motor_id);
    if (motor_config[peer].state != MOTOR_STATE_STOPPED)
    {
      motor_exit_critical(primask);
      return;
    }
  }

  /* 设置方向 */
  m->direction = dir;
  if (dir == MOTOR_DIR_CW)
    gpio_bits_reset(m->dir_port, m->dir_pin);
  else
    gpio_bits_set(m->dir_port, m->dir_pin);

  m->target_rpm  = rpm;
  m->target_freq = RPM_TO_HZ(rpm);
  m->stop_request = 0;

  /* 从停止状态启动时，从最低速开始加速 */
  if (m->current_rpm == 0)
  {
    m->current_rpm  = MIN_RPM;
    m->current_freq = RPM_TO_HZ(MIN_RPM);
    set_pwm_frequency(motor_id, m->current_freq);
  }

  /* 高级定时器开启主输出 */
  if (m->is_advanced_tmr)
    tmr_output_enable(m->tmr, TRUE);

  tmr_counter_enable(m->tmr, TRUE);

  m->state = (m->current_rpm < rpm) ? MOTOR_STATE_ACCELERATING
                                    : MOTOR_STATE_RUNNING;

  motor_exit_critical(primask);
}

void MotorStop(motor_id_t motor_id)
{
  uint32_t primask;
  if (motor_id >= MOTOR_COUNT) return;

  primask = motor_enter_critical();
  motor_config[motor_id].stop_request = 1;
  motor_config[motor_id].state = MOTOR_STATE_DECELERATING;
  motor_exit_critical(primask);
}

void MotorImmediateStop(motor_id_t motor_id)
{
  uint32_t primask;
  if (motor_id >= MOTOR_COUNT) return;

  primask = motor_enter_critical();
  stop_pwm_output(motor_id);
  reset_motor_state(&motor_config[motor_id]);
  motor_exit_critical(primask);
}

void MotorSetAcceleration(motor_id_t motor_id, int16_t accel_rpm_per_s)
{
  uint32_t primask;
  if (motor_id >= MOTOR_COUNT) return;
  if (accel_rpm_per_s < 10) accel_rpm_per_s = 10;
  if (accel_rpm_per_s > 2000) accel_rpm_per_s = 2000;

  primask = motor_enter_critical();
  motor_config[motor_id].accel_rpm_per_s = accel_rpm_per_s;
  motor_exit_critical(primask);
}

/**
 * @brief  电机速度更新（TMR6 1kHz中断中调用）
 */
void MotorUpdate(motor_id_t motor_id)
{
  volatile motor_config_t *m;
  uint16_t new_rpm;
  int16_t rpm_step;

  if (motor_id >= MOTOR_COUNT) return;
  m = &motor_config[motor_id];

  /* 空闲状态直接返回 */
  if ((m->stop_request == 0) && (m->target_rpm == 0) && (m->current_rpm == 0))
  {
    m->state = MOTOR_STATE_STOPPED;
    return;
  }

  /* 加速分频：accel < 1000时每N个tick步进1RPM */
  if (m->accel_rpm_per_s >= UPDATE_FREQ_HZ)
  {
    rpm_step = m->accel_rpm_per_s / UPDATE_FREQ_HZ;
  }
  else
  {
    uint16_t ticks_per_step = UPDATE_FREQ_HZ / m->accel_rpm_per_s;
    if (++m->accel_tick < ticks_per_step)
      return;
    m->accel_tick = 0;
    rpm_step = 1;
  }

  /* 减速停止 */
  if (m->stop_request)
  {
    if (m->current_rpm > (uint16_t)rpm_step)
    {
      new_rpm = m->current_rpm - rpm_step;
    }
    else
    {
      stop_pwm_output(motor_id);
      reset_motor_state(m);
      return;
    }
  }
  /* 加速到目标 */
  else if (m->current_rpm < m->target_rpm)
  {
    m->state = MOTOR_STATE_ACCELERATING;
    if ((m->target_rpm - m->current_rpm) <= (uint16_t)rpm_step)
    {
      new_rpm = m->target_rpm;
      m->state = MOTOR_STATE_RUNNING;
    }
    else
    {
      new_rpm = m->current_rpm + rpm_step;
    }
  }
  /* 减速到较低目标 */
  else if (m->current_rpm > m->target_rpm)
  {
    m->state = MOTOR_STATE_DECELERATING;
    if ((m->current_rpm - m->target_rpm) <= (uint16_t)rpm_step)
    {
      new_rpm = m->target_rpm;
      m->state = MOTOR_STATE_RUNNING;
    }
    else
    {
      new_rpm = m->current_rpm - rpm_step;
    }
  }
  else
  {
    /* 已达目标速度 */
    m->state = (m->target_rpm == 0) ? MOTOR_STATE_STOPPED : MOTOR_STATE_RUNNING;
    return;
  }

  m->current_rpm  = new_rpm;
  m->current_freq = RPM_TO_HZ(new_rpm);

  if (new_rpm == 0)
  {
    stop_pwm_output(motor_id);
    reset_motor_state(m);
    return;
  }

  set_pwm_frequency(motor_id, m->current_freq);
}

motor_state_t MotorGetState(motor_id_t motor_id)
{
  if (motor_id >= MOTOR_COUNT) return MOTOR_STATE_STOPPED;
  return motor_config[motor_id].state;
}

uint16_t MotorGetSpeedRPM(motor_id_t motor_id)
{
  if (motor_id >= MOTOR_COUNT) return 0;
  return motor_config[motor_id].current_rpm;
}

motor_dir_t MotorGetDirection(motor_id_t motor_id)
{
  if (motor_id >= MOTOR_COUNT) return MOTOR_DIR_CW;
  return motor_config[motor_id].direction;
}

/**
 * @brief  LED指示灯更新（TMR15 1Hz中断中调用）
 *         常亮=故障, 闪烁=运行, 灭=待机
 */
void MotorUpdateLEDIndicator(void)
{
  static uint8_t led_toggle = 0;
  uint8_t i;
  gpio_type *led_port[MOTOR_COUNT] = { LED1_GPIO_PORT, LED2_GPIO_PORT, LED3_GPIO_PORT, LED4_GPIO_PORT };
  uint16_t led_pin[MOTOR_COUNT] = { LED1_PIN, LED2_PIN, LED3_PIN, LED4_PIN };

  led_toggle = !led_toggle;

  for (i = 0; i < MOTOR_COUNT; i++)
  {
    if (MotorGetMonitorStatus(i) == MONITOR_STATUS_FAULT)
    {
      /* 故障：LED常亮（低电平亮） */
      gpio_bits_reset(led_port[i], led_pin[i]);
    }
    else if (motor_config[i].state == MOTOR_STATE_STOPPED)
    {
      /* 待机：LED灭（高电平灭） */
      gpio_bits_set(led_port[i], led_pin[i]);
    }
    else
    {
      /* 运行中：LED闪烁 */
      if (led_toggle)
        gpio_bits_reset(led_port[i], led_pin[i]);
      else
        gpio_bits_set(led_port[i], led_pin[i]);
    }
  }
}

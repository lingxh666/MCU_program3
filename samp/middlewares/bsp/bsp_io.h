#ifndef BSP_IO_H
#define BSP_IO_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== Relay/Valve Output IDs ======================== */
typedef enum {
  RELAY_INLET_VALVE = 0,    /* PA10 进水阀 */
  RELAY_INSTANT_VALVE,      /* PA8  瞬时阀 */
  RELAY_DELIVER_VALVE,      /* PC9  送留样阀 */
  RELAY_DRAIN_A,            /* PC8  A排水 */
  RELAY_DRAIN_B,            /* PD15 B排水 */
  RELAY_STIR_A,             /* PD14 A搅拌 */
  RELAY_STIR_B,             /* PD13 B搅拌 */
  RELAY_OUTLET_VALVE_A,     /* PD12 出水阀A */
  RELAY_OUTLET_VALVE_B,     /* PD11 出水阀B */
  RELAY_LOCK,               /* PD10 锁开关 */
  RELAY_EXT_PUMP,           /* PB10 外接泵 */
  RELAY_SPARE1,             /* PB11 备用1 */
  RELAY_SPARE2,             /* PB12 备用2 */
  RELAY_SPARE3,             /* PB13 备用3 */
  RELAY_COUNT
} relay_id_t;

/* ======================== Input IDs ======================== */
typedef enum {
  INPUT_LEVEL_SAMPLE = 0,   /* PC12 采样液位 */
  INPUT_LEVEL_DELIVERY,     /* PD2  送样液位 */
  INPUT_LEVEL_RETAIN,       /* PD3  留样液位 */
  INPUT_TRIG_SAMPLE,        /* PB7  采样触发 */
  INPUT_TRIG_DELIVERY,      /* PB9  送样触发 */
  INPUT_TRIG_RETAIN,        /* PE2  留样触发 */
  INPUT_LOCK_STATE,         /* PE3  锁状态 */
  INPUT_BOTTLE_ORIGIN,      /* PE5  瓶原点 */
  INPUT_BOTTLE_POS,         /* PE6  瓶到位 */
  INPUT_SPARE,              /* PE4  备用输入 */
  INPUT_COUNT
} input_id_t;

/* ======================== H-Bridge Motor Direction ======================== */
typedef enum {
  MOTOR_STOP = 0,
  MOTOR_EMPTY,              /* PE14=H, PE15=L 排空方向 */
  MOTOR_RESTORE             /* PE14=L, PE15=H 复位方向 */
} motor_dir_t;

/* ======================== Relay Macros ======================== */
#define INLET_VALVE_ON()      gpio_bits_set(GPIOA, GPIO_PINS_10)
#define INLET_VALVE_OFF()     gpio_bits_reset(GPIOA, GPIO_PINS_10)
#define INSTANT_VALVE_ON()    gpio_bits_set(GPIOA, GPIO_PINS_8)
#define INSTANT_VALVE_OFF()   gpio_bits_reset(GPIOA, GPIO_PINS_8)
#define DELIVER_VALVE_ON()    gpio_bits_set(GPIOC, GPIO_PINS_9)
#define DELIVER_VALVE_OFF()   gpio_bits_reset(GPIOC, GPIO_PINS_9)
#define DRAIN_A_ON()          gpio_bits_set(GPIOC, GPIO_PINS_8)
#define DRAIN_A_OFF()         gpio_bits_reset(GPIOC, GPIO_PINS_8)
#define DRAIN_B_ON()          gpio_bits_set(GPIOD, GPIO_PINS_15)
#define DRAIN_B_OFF()         gpio_bits_reset(GPIOD, GPIO_PINS_15)
#define STIR_A_ON()           gpio_bits_set(GPIOD, GPIO_PINS_14)
#define STIR_A_OFF()          gpio_bits_reset(GPIOD, GPIO_PINS_14)
#define STIR_B_ON()           gpio_bits_set(GPIOD, GPIO_PINS_13)
#define STIR_B_OFF()          gpio_bits_reset(GPIOD, GPIO_PINS_13)
#define OUTLET_VALVE_A_ON()   gpio_bits_set(GPIOD, GPIO_PINS_12)
#define OUTLET_VALVE_A_OFF()  gpio_bits_reset(GPIOD, GPIO_PINS_12)
#define OUTLET_VALVE_B_ON()   gpio_bits_set(GPIOD, GPIO_PINS_11)
#define OUTLET_VALVE_B_OFF()  gpio_bits_reset(GPIOD, GPIO_PINS_11)
#define LOCK_ON()             gpio_bits_set(GPIOD, GPIO_PINS_10)
#define LOCK_OFF()            gpio_bits_reset(GPIOD, GPIO_PINS_10)
#define EXT_PUMP_ON()         gpio_bits_set(GPIOB, GPIO_PINS_10)
#define EXT_PUMP_OFF()        gpio_bits_reset(GPIOB, GPIO_PINS_10)

/* 485/232 mode select (PA15) */
#define RS485_MODE_SELECT()   gpio_bits_set(GPIOA, GPIO_PINS_15)
#define RS232_MODE_SELECT()   gpio_bits_reset(GPIOA, GPIO_PINS_15)

/* ======================== API ======================== */
void relay_set(relay_id_t id, uint8_t state);
uint8_t relay_get_state(relay_id_t id);
void relay_all_off(void);

uint8_t input_read(input_id_t id);
uint8_t input_get_dip_switch(void);
uint8_t read_trigger_sampling_signal(void);

void bottle_motor_set(motor_dir_t dir);
motor_dir_t bottle_motor_get_dir(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_IO_H */

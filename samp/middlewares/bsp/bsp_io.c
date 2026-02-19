#include "bsp_io.h"

/* ======================== Relay Pin Table ======================== */
typedef struct {
  gpio_type *port;
  uint32_t pin;
} gpio_pin_t;

static const gpio_pin_t relay_table[RELAY_COUNT] = {
  { GPIOA, GPIO_PINS_10 },  /* RELAY_INLET_VALVE */
  { GPIOA, GPIO_PINS_8  },  /* RELAY_INSTANT_VALVE */
  { GPIOC, GPIO_PINS_9  },  /* RELAY_DELIVER_VALVE */
  { GPIOC, GPIO_PINS_8  },  /* RELAY_DRAIN_A */
  { GPIOD, GPIO_PINS_15 },  /* RELAY_DRAIN_B */
  { GPIOD, GPIO_PINS_14 },  /* RELAY_STIR_A */
  { GPIOD, GPIO_PINS_13 },  /* RELAY_STIR_B */
  { GPIOD, GPIO_PINS_12 },  /* RELAY_OUTLET_VALVE_A */
  { GPIOD, GPIO_PINS_11 },  /* RELAY_OUTLET_VALVE_B */
  { GPIOD, GPIO_PINS_10 },  /* RELAY_LOCK */
  { GPIOB, GPIO_PINS_10 },  /* RELAY_EXT_PUMP */
  { GPIOB, GPIO_PINS_11 },  /* RELAY_SPARE1 */
  { GPIOB, GPIO_PINS_12 },  /* RELAY_SPARE2 */
  { GPIOB, GPIO_PINS_13 },  /* RELAY_SPARE3 */
};

/* ======================== Input Pin Table ======================== */
static const gpio_pin_t input_table[INPUT_COUNT] = {
  { GPIOC, GPIO_PINS_12 },  /* INPUT_LEVEL_SAMPLE */
  { GPIOD, GPIO_PINS_2  },  /* INPUT_LEVEL_DELIVERY */
  { GPIOD, GPIO_PINS_3  },  /* INPUT_LEVEL_RETAIN */
  { GPIOB, GPIO_PINS_7  },  /* INPUT_TRIG_SAMPLE */
  { GPIOB, GPIO_PINS_9  },  /* INPUT_TRIG_DELIVERY */
  { GPIOE, GPIO_PINS_2  },  /* INPUT_TRIG_RETAIN */
  { GPIOE, GPIO_PINS_3  },  /* INPUT_LOCK_STATE */
  { GPIOE, GPIO_PINS_5  },  /* INPUT_BOTTLE_ORIGIN */
  { GPIOE, GPIO_PINS_6  },  /* INPUT_BOTTLE_POS */
  { GPIOE, GPIO_PINS_4  },  /* INPUT_SPARE */
};

/* ======================== Relay Functions ======================== */
void relay_set(relay_id_t id, uint8_t state)
{
  if(id >= RELAY_COUNT) return;
  if(state)
    gpio_bits_reset(relay_table[id].port, relay_table[id].pin);
  else
    gpio_bits_set(relay_table[id].port, relay_table[id].pin);
}

uint8_t relay_get_state(relay_id_t id)
{
  if(id >= RELAY_COUNT) return 0;
  return (gpio_output_data_read(relay_table[id].port) & relay_table[id].pin) ? 0 : 1;
}

void relay_all_off(void)
{
  uint8_t i;
  for(i = 0; i < RELAY_COUNT; i++)
    gpio_bits_set(relay_table[i].port, relay_table[i].pin);
  /* H-bridge motor stop */
  bottle_motor_set(MOTOR_STOP);
}

/* ======================== Input Functions ======================== */
uint8_t input_read(input_id_t id)
{
  if(id >= INPUT_COUNT) return 0;
  return (gpio_input_data_bit_read(input_table[id].port, input_table[id].pin) == SET) ? 1 : 0;
}

uint8_t input_get_dip_switch(void)
{
  uint8_t val = 0;
  if(gpio_input_data_bit_read(GPIOE, GPIO_PINS_11) == SET) val |= 0x01;
  if(gpio_input_data_bit_read(GPIOE, GPIO_PINS_12) == SET) val |= 0x02;
  if(gpio_input_data_bit_read(GPIOE, GPIO_PINS_13) == SET) val |= 0x04;
  return val;
}

/* ======================== H-Bridge Motor Functions ======================== */
void bottle_motor_set(motor_dir_t dir)
{
  switch(dir)
  {
    case MOTOR_EMPTY:
      gpio_bits_set(GPIOE, GPIO_PINS_15);    /* 先断开反向 */
      gpio_bits_reset(GPIOE, GPIO_PINS_14);  /* 再导通正向 */
      break;
    case MOTOR_RESTORE:
      gpio_bits_set(GPIOE, GPIO_PINS_14);    /* 先断开正向 */
      gpio_bits_reset(GPIOE, GPIO_PINS_15);  /* 再导通反向 */
      break;
    default: /* MOTOR_STOP */
      gpio_bits_set(GPIOE, GPIO_PINS_14);    /* 断开 */
      gpio_bits_set(GPIOE, GPIO_PINS_15);    /* 断开 */
      break;
  }
}

motor_dir_t bottle_motor_get_dir(void)
{
  uint8_t pe14 = (gpio_output_data_read(GPIOE) & GPIO_PINS_14) ? 1 : 0;
  uint8_t pe15 = (gpio_output_data_read(GPIOE) & GPIO_PINS_15) ? 1 : 0;
  if(!pe14 && pe15) return MOTOR_EMPTY;
  if(pe14 && !pe15) return MOTOR_RESTORE;
  return MOTOR_STOP;
}

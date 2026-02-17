#ifndef BSP_UART_COLLECTOR_H
#define BSP_UART_COLLECTOR_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 通信模式 */
#define COLLECTOR_MODE_485    0
#define COLLECTOR_MODE_232    1

/* PA15控制485/232模式切换 */
#define COLLECTOR_SELECT_485()  gpio_bits_set(GPIOA, GPIO_PINS_15)
#define COLLECTOR_SELECT_232()  gpio_bits_reset(GPIOA, GPIO_PINS_15)

void collector_init(void);
void collector_set_mode(uint8_t mode);
uint8_t collector_get_mode(void);
void collector_send(const uint8_t *data, uint16_t len);
uint16_t collector_recv(uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_COLLECTOR_H */

#ifndef BSP_UART_ADMODULE_H
#define BSP_UART_ADMODULE_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADMODULE_MAX_CH   8

void admodule_init(void);
float admodule_get_value(uint8_t channel);
uint8_t admodule_is_updated(void);
void admodule_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_ADMODULE_H */

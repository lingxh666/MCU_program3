#ifndef BSP_UART_BLUETOOTH_H
#define BSP_UART_BLUETOOTH_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void bluetooth_init(void);
void bluetooth_send(const uint8_t *data, uint16_t len);
uint16_t bluetooth_recv(uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_BLUETOOTH_H */

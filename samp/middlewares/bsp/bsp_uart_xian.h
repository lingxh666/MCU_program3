#ifndef BSP_UART_XIAN_H
#define BSP_UART_XIAN_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void xian485_init(void);
void xian485_send(const uint8_t *data, uint16_t len);
uint16_t xian485_recv(uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_XIAN_H */

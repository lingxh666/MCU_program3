#ifndef BSP_UART_SPARE_H
#define BSP_UART_SPARE_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

void spare485_init(void);
void spare485_send(const uint8_t *data, uint16_t len);
uint16_t spare485_recv(uint8_t *buf, uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_SPARE_H */

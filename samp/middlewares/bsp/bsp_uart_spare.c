#include "bsp_uart_spare.h"

void spare485_init(void)
{
  /* 预留协议层初始化 */
}

void spare485_send(const uint8_t *data, uint16_t len)
{
  bsp_uart_send(UART_PORT_SPARE485, data, len);
}

uint16_t spare485_recv(uint8_t *buf, uint16_t max_len)
{
  return bsp_uart_get_rxdata(UART_PORT_SPARE485, buf, max_len);
}

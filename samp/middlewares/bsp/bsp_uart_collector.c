#include "bsp_uart_collector.h"

static uint8_t current_mode = COLLECTOR_MODE_485;

void collector_init(void)
{
  COLLECTOR_SELECT_485();
  current_mode = COLLECTOR_MODE_485;
}

void collector_set_mode(uint8_t mode)
{
  if (mode == COLLECTOR_MODE_232)
    COLLECTOR_SELECT_232();
  else
    COLLECTOR_SELECT_485();

  current_mode = mode;
}

uint8_t collector_get_mode(void)
{
  return current_mode;
}

void collector_send(const uint8_t *data, uint16_t len)
{
  bsp_uart_send(UART_PORT_COLLECTOR, data, len);
}

uint16_t collector_recv(uint8_t *buf, uint16_t max_len)
{
  return bsp_uart_get_rxdata(UART_PORT_COLLECTOR, buf, max_len);
}

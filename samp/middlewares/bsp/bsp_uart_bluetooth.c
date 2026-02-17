#include "bsp_uart_bluetooth.h"

void bluetooth_init(void)
{
  /* 占位：后续根据蓝牙模块型号补充AT配置 */
}

void bluetooth_send(const uint8_t *data, uint16_t len)
{
  bsp_uart_send(UART_PORT_BLUETOOTH, data, len);
}

uint16_t bluetooth_recv(uint8_t *buf, uint16_t max_len)
{
  return bsp_uart_get_rxdata(UART_PORT_BLUETOOTH, buf, max_len);
}

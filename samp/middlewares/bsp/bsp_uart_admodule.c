#include "bsp_uart_admodule.h"
#include <string.h>

static float ad_values[ADMODULE_MAX_CH];
static volatile uint8_t ad_updated = 0;

static void admodule_rx_cb(uart_port_t port, uint8_t *data, uint16_t len)
{
  (void)port;
  (void)data;
  (void)len;

  /* TODO: 根据AD模块协议解析数据帧，存入ad_values[] */
  ad_updated = 1;
}

void admodule_init(void)
{
  memset(ad_values, 0, sizeof(ad_values));
  ad_updated = 0;
  bsp_uart_set_rx_callback(UART_PORT_ADMODULE, admodule_rx_cb);
}

float admodule_get_value(uint8_t channel)
{
  if (channel >= ADMODULE_MAX_CH)
    return 0.0f;
  return ad_values[channel];
}

uint8_t admodule_is_updated(void)
{
  if (ad_updated)
  {
    ad_updated = 0;
    return 1;
  }
  return 0;
}

void admodule_poll(void)
{
  /* 预留：发送查询命令给AD模块 */
}

#include "bsp_uart_4g.h"
#include <string.h>

static at_urc_callback_t urc_cb = NULL;

/* AT响应缓冲 */
static char at_resp_buf[256];
static volatile uint16_t at_resp_len = 0;
static volatile uint8_t  at_resp_ready = 0;

static void module_4g_rx_cb(uart_port_t port, uint8_t *data, uint16_t len)
{
  (void)port;

  if (len >= sizeof(at_resp_buf))
    return;

  memcpy(at_resp_buf, data, len);
  at_resp_buf[len] = '\0';
  at_resp_len = len;
  at_resp_ready = 1;

  /* URC: 非AT命令响应的主动上报，以'+'开头 */
  if (urc_cb && data[0] == '+')
    urc_cb((const char *)data, len);
}

void module_4g_init(void)
{
  at_resp_len = 0;
  at_resp_ready = 0;
  bsp_uart_set_rx_callback(UART_PORT_4G, module_4g_rx_cb);
}

uint8_t module_4g_send_at(const char *cmd, char *resp, uint16_t resp_size,
                          uint16_t timeout_ms)
{
  uint16_t wait = 0;

  at_resp_ready = 0;
  at_resp_len = 0;

  bsp_uart_send(UART_PORT_4G, (const uint8_t *)cmd, (uint16_t)strlen(cmd));
  bsp_uart_send(UART_PORT_4G, (const uint8_t *)"\r\n", 2);

  while (wait < timeout_ms)
  {
    if (at_resp_ready)
    {
      if (resp && resp_size > 0)
      {
        uint16_t copy_len = at_resp_len;
        if (copy_len >= resp_size)
          copy_len = resp_size - 1;
        memcpy(resp, at_resp_buf, copy_len);
        resp[copy_len] = '\0';
      }

      if (strstr(at_resp_buf, "OK"))
        return AT_OK;
      if (strstr(at_resp_buf, "ERROR"))
        return AT_ERROR;

      /* 中间响应，继续等待 */
      at_resp_ready = 0;
    }
    wait++;
    /* 需配合RTOS的1ms延时使用 */
  }

  return AT_TIMEOUT;
}

void module_4g_send_data(const uint8_t *data, uint16_t len)
{
  bsp_uart_send(UART_PORT_4G, data, len);
}

uint16_t module_4g_recv_data(uint8_t *buf, uint16_t max_len)
{
  return bsp_uart_get_rxdata(UART_PORT_4G, buf, max_len);
}

void module_4g_set_urc_callback(at_urc_callback_t cb)
{
  urc_cb = cb;
}

void module_4g_poll(void)
{
  /* 预留：模块状态检查、URC处理、心跳 */
}

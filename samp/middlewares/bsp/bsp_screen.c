#include "bsp_screen.h"
#include <string.h>

static screen_cmd_callback_t screen_cmd_cb = NULL;

static void screen_uart_rx_cb(uart_port_t port, uint8_t *data, uint16_t len)
{
  (void)port;
  screen_rx_process(data, len);
}

void screen_init(void)
{
  bsp_uart_set_rx_callback(UART_PORT_SCREEN, screen_uart_rx_cb);
}

void screen_write_var(uint16_t addr, const uint8_t *data, uint16_t len)
{
  uint8_t buf[64];

  if (len + 6 > sizeof(buf))
    return;

  buf[0] = SCREEN_FRAME_HEAD1;
  buf[1] = SCREEN_FRAME_HEAD2;
  buf[2] = (uint8_t)(len + 3);       /* 数据长度 = cmd(1) + addr(2) + data */
  buf[3] = SCREEN_CMD_WRITE_VAR;
  buf[4] = (uint8_t)(addr >> 8);
  buf[5] = (uint8_t)(addr & 0xFF);
  memcpy(&buf[6], data, len);

  bsp_uart_send(UART_PORT_SCREEN, buf, 6 + len);
}

void screen_write_u16(uint16_t addr, uint16_t value)
{
  uint8_t data[2];
  data[0] = (uint8_t)(value >> 8);
  data[1] = (uint8_t)(value & 0xFF);
  screen_write_var(addr, data, 2);
}

void screen_read_var(uint16_t addr, uint8_t word_count)
{
  uint8_t buf[7];
  buf[0] = SCREEN_FRAME_HEAD1;
  buf[1] = SCREEN_FRAME_HEAD2;
  buf[2] = 0x04;                      /* 数据长度 = cmd(1) + addr(2) + count(1) */
  buf[3] = SCREEN_CMD_READ_VAR;
  buf[4] = (uint8_t)(addr >> 8);
  buf[5] = (uint8_t)(addr & 0xFF);
  buf[6] = word_count;
  bsp_uart_send(UART_PORT_SCREEN, buf, 7);
}

void screen_switch_page(uint8_t page_id)
{
  uint8_t data[4] = { 0x5A, 0x01, 0x00, page_id };
  screen_write_var(SCREEN_PAGE_REG_ADDR, data, 4);
}

void screen_set_cmd_callback(screen_cmd_callback_t cb)
{
  screen_cmd_cb = cb;
}

void screen_rx_process(uint8_t *data, uint16_t len)
{
  uint8_t cmd;
  uint16_t addr, frame_len;

  /* 最小帧: 帧头(2) + 长度(1) + 命令(1) + 地址(2) = 6 */
  if (len < 6)
    return;
  if (data[0] != SCREEN_FRAME_HEAD1 || data[1] != SCREEN_FRAME_HEAD2)
    return;

  frame_len = data[2];
  if (frame_len + 3 > len)
    return;

  cmd  = data[3];
  addr = ((uint16_t)data[4] << 8) | data[5];

  if (screen_cmd_cb)
    screen_cmd_cb(cmd, addr, &data[6], frame_len - 3);
}

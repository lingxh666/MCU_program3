#include "bsp_uart.h"
#include <string.h>

/* UART外设与DMA通道映射表 */
static usart_type * const uart_periph[UART_PORT_COUNT] = {
  USART2, USART3, UART4, UART5, USART6, UART7, UART8
};

static dma_channel_type * const uart_dma_ch[UART_PORT_COUNT] = {
  DMA1_CHANNEL1, DMA1_CHANNEL2, DMA1_CHANNEL3, DMA1_CHANNEL4,
  DMA1_CHANNEL5, DMA1_CHANNEL6, DMA1_CHANNEL7
};

/* DMA接收缓冲区（硬件写入） */
static uint8_t uart_dma_buf[UART_PORT_COUNT][UART_DMA_BUF_SIZE];

/* 应用接收缓冲区（IDLE中断拷贝） */
static uint8_t uart_rx_buf[UART_PORT_COUNT][UART_DMA_BUF_SIZE];
static volatile uint16_t uart_rx_len[UART_PORT_COUNT];
static volatile uint8_t  uart_rx_ready[UART_PORT_COUNT];

/* 接收回调 */
static uart_rx_callback_t uart_rx_cb[UART_PORT_COUNT];

void bsp_uart_init(void)
{
  uint8_t i;

  /* 清零所有接收状态 */
  memset((void *)uart_rx_len, 0, sizeof(uart_rx_len));
  memset((void *)uart_rx_ready, 0, sizeof(uart_rx_ready));
  memset(uart_rx_cb, 0, sizeof(uart_rx_cb));

  for(i = 0; i < UART_PORT_COUNT; i++)
  {
    /* 重新配置DMA缓冲区 */
    dma_channel_enable(uart_dma_ch[i], FALSE);
    uart_dma_ch[i]->paddr = (uint32_t)&uart_periph[i]->dt;
    uart_dma_ch[i]->maddr = (uint32_t)uart_dma_buf[i];
    uart_dma_ch[i]->dtcnt = UART_DMA_BUF_SIZE;
    dma_channel_enable(uart_dma_ch[i], TRUE);
  }
}

void bsp_uart_send(uart_port_t port, const uint8_t *data, uint16_t len)
{
  usart_type *usart;
  uint16_t i;

  if(port >= UART_PORT_COUNT) return;
  usart = uart_periph[port];

  for(i = 0; i < len; i++)
  {
    while(usart_flag_get(usart, USART_TDBE_FLAG) == RESET);
    usart_data_transmit(usart, data[i]);
  }
  /* 等待最后一个字节发送完成 */
  while(usart_flag_get(usart, USART_TDC_FLAG) == RESET);
}

uint16_t bsp_uart_get_rxdata(uart_port_t port, uint8_t *buf, uint16_t max_len)
{
  uint16_t len;

  if(port >= UART_PORT_COUNT || !uart_rx_ready[port])
    return 0;

  len = uart_rx_len[port];
  if(len > max_len) len = max_len;
  memcpy(buf, uart_rx_buf[port], len);

  uart_rx_ready[port] = 0;
  uart_rx_len[port] = 0;
  return len;
}

uint8_t bsp_uart_rx_available(uart_port_t port)
{
  if(port >= UART_PORT_COUNT) return 0;
  return uart_rx_ready[port];
}

void bsp_uart_set_rx_callback(uart_port_t port, uart_rx_callback_t cb)
{
  if(port >= UART_PORT_COUNT) return;
  uart_rx_cb[port] = cb;
}

void bsp_uart_idle_irq(uart_port_t port)
{
  dma_channel_type *dma_ch;
  uint16_t recv_len;

  if(port >= UART_PORT_COUNT) return;
  dma_ch = uart_dma_ch[port];

  /* 读取数据寄存器以彻底清除IDLE标志 */
  usart_data_receive(uart_periph[port]);

  /* 禁用DMA，读取剩余计数 */
  dma_channel_enable(dma_ch, FALSE);
  recv_len = UART_DMA_BUF_SIZE - dma_data_number_get(dma_ch);

  if(recv_len > 0)
  {
    /* 拷贝到应用缓冲区 */
    memcpy(uart_rx_buf[port], uart_dma_buf[port], recv_len);
    uart_rx_len[port] = recv_len;
    uart_rx_ready[port] = 1;

    /* 调用回调（ISR上下文） */
    if(uart_rx_cb[port])
      uart_rx_cb[port](port, uart_rx_buf[port], recv_len);
  }

  /* 重置DMA计数并重新启用 */
  dma_data_number_set(dma_ch, UART_DMA_BUF_SIZE);
  dma_channel_enable(dma_ch, TRUE);
}

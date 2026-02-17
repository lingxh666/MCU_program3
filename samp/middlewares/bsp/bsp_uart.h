#ifndef BSP_UART_H
#define BSP_UART_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DMA接收缓冲区大小 */
#define UART_DMA_BUF_SIZE     256

/* UART端口枚举（按功能命名） */
typedef enum {
  UART_PORT_COLLECTOR = 0,  /* USART2 - 数采仪(485/232) */
  UART_PORT_BLUETOOTH,      /* USART3 - 蓝牙 */
  UART_PORT_SCREEN,         /* UART4  - 串口屏 */
  UART_PORT_XIAN485,        /* UART5  - 西安485 */
  UART_PORT_4G,             /* USART6 - 4G模块 */
  UART_PORT_SPARE485,       /* UART7  - 485备用 */
  UART_PORT_ADMODULE,       /* UART8  - AD模块 */
  UART_PORT_COUNT           /* 端口总数 = 7 */
} uart_port_t;

/* UART接收回调函数类型 */
typedef void (*uart_rx_callback_t)(uart_port_t port, uint8_t *data, uint16_t len);

/* 初始化UART DMA框架（配置DMA缓冲区） */
void bsp_uart_init(void);

/* 发送数据（阻塞方式） */
void bsp_uart_send(uart_port_t port, const uint8_t *data, uint16_t len);

/* 获取接收数据，返回实际长度，无数据返回0 */
uint16_t bsp_uart_get_rxdata(uart_port_t port, uint8_t *buf, uint16_t max_len);

/* 检查是否有接收数据 */
uint8_t bsp_uart_rx_available(uart_port_t port);

/* 设置接收回调（在IDLE中断中调用，注意ISR上下文） */
void bsp_uart_set_rx_callback(uart_port_t port, uart_rx_callback_t cb);

/* IDLE中断处理（由中断服务函数调用） */
void bsp_uart_idle_irq(uart_port_t port);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */

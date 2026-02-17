#ifndef BSP_UART_4G_H
#define BSP_UART_4G_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AT命令结果 */
#define AT_OK         0
#define AT_ERROR      1
#define AT_TIMEOUT    2

/* URC主动上报回调 */
typedef void (*at_urc_callback_t)(const char *urc, uint16_t len);

void module_4g_init(void);
uint8_t module_4g_send_at(const char *cmd, char *resp, uint16_t resp_size,
                          uint16_t timeout_ms);
void module_4g_send_data(const uint8_t *data, uint16_t len);
uint16_t module_4g_recv_data(uint8_t *buf, uint16_t max_len);
void module_4g_set_urc_callback(at_urc_callback_t cb);
void module_4g_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_4G_H */

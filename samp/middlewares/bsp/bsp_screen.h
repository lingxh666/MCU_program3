#ifndef BSP_SCREEN_H
#define BSP_SCREEN_H

#include "bsp_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 迪文屏协议常量 */
#define SCREEN_FRAME_HEAD1    0x5A
#define SCREEN_FRAME_HEAD2    0xA5
#define SCREEN_CMD_WRITE_VAR  0x82
#define SCREEN_CMD_READ_VAR   0x83
#define SCREEN_PAGE_REG_ADDR  0x0084

/* 接收帧解析回调 */
typedef void (*screen_cmd_callback_t)(uint8_t cmd, uint16_t addr,
                                      const uint8_t *data, uint16_t data_len);

void screen_init(void);
void screen_write_var(uint16_t addr, const uint8_t *data, uint16_t len);
void screen_write_u16(uint16_t addr, uint16_t value);
void screen_read_var(uint16_t addr, uint8_t word_count);
void screen_switch_page(uint8_t page_id);
void screen_set_cmd_callback(screen_cmd_callback_t cb);
void screen_rx_process(uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_SCREEN_H */

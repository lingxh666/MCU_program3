#ifndef BSP_WDT_H
#define BSP_WDT_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 启动看门狗（调用后必须定期喂狗，否则系统复位） */
void bsp_wdt_enable(void);

/* 喂狗 */
void bsp_wdt_feed(void);

/* 检查上次复位是否由看门狗触发 */
uint8_t bsp_wdt_is_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_WDT_H */

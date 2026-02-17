#ifndef BSP_WIEGAND_H
#define BSP_WIEGAND_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 无脉冲超过此时间视为一帧结束 */
#define WIEGAND_TIMEOUT_MS    250
#define WIEGAND_MAX_BITS      34

void wiegand_init(void);
uint8_t wiegand_get_card_id(uint32_t *card_id);
void wiegand_d0_irq(void);
void wiegand_d1_irq(void);
void wiegand_timeout_check(void);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file    bsp_timer.h
 * @brief   硬件定时器全局计数器声明
 *
 * TMR2: 1s 周期 → g_tmr2_seconds   (秒级定时)
 * TMR4: 1ms周期 → g_tmr4_milliseconds (毫秒级定时)
 */
#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_tmr2_seconds;       /* TMR2 ISR 每秒递增 */
extern volatile uint32_t g_tmr4_milliseconds;  /* TMR4 ISR 每毫秒递增 */

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIMER_H */

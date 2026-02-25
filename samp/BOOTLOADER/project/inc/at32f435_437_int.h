/**
 * @file    at32f435_437_int.h
 * @brief   Bootloader中断处理头文件 (AT32F435)
 */
#ifndef __AT32F435_437_INT_H
#define __AT32F435_437_INT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f435_437.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __AT32F435_437_INT_H */

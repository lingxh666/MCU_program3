#ifndef BSP_PVM_H
#define BSP_PVM_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化PVM监测（开机调用） */
void bsp_pvm_init(void);

/* 获取断电次数 */
uint32_t bsp_pvm_get_count(void);

/* 获取最后断电日期(YYYYMMDD) */
uint32_t bsp_pvm_get_date(void);

/* 获取最后断电时间(HHmmss) */
uint32_t bsp_pvm_get_time(void);

/* PVM中断处理（在PVM_IRQHandler中调用） */
void bsp_pvm_irq(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_PVM_H */

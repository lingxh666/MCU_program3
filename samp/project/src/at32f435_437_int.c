/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f435_437_int.c
  * @brief    main interrupt service routines.
  **************************************************************************
  * Copyright (c) 2025, Artery Technology, All rights reserved.
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* includes ------------------------------------------------------------------*/
#include "at32f435_437_int.h"
#include "usb_app.h"
#include "wk_system.h"
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_uart.h"
#include "bsp_can_motor.h"
#include "bsp_wiegand.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */
#include "bsp_pvm.h"
/* add user code end 0 */

/* external variables ---------------------------------------------------------*/
/* add user code begin external variables */

/* add user code end external variables */

/**
  * @brief  this function handles nmi exception.
  * @param  none
  * @retval none
  */
void NMI_Handler(void)
{
  /* add user code begin NonMaskableInt_IRQ 0 */

  /* add user code end NonMaskableInt_IRQ 0 */

  /* add user code begin NonMaskableInt_IRQ 1 */

  /* add user code end NonMaskableInt_IRQ 1 */
}

/**
  * @brief  this function handles hard fault exception.
  * @param  none
  * @retval none
  */
void HardFault_Handler(void)
{
  /* add user code begin HardFault_IRQ 0 */

  /* add user code end HardFault_IRQ 0 */
  /* go to infinite loop when hard fault exception occurs */
  while (1)
  {
    /* add user code begin W1_HardFault_IRQ 0 */

    /* add user code end W1_HardFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles memory manage exception.
  * @param  none
  * @retval none
  */
void MemManage_Handler(void)
{
  /* add user code begin MemoryManagement_IRQ 0 */

  /* add user code end MemoryManagement_IRQ 0 */
  /* go to infinite loop when memory manage exception occurs */
  while (1)
  {
    /* add user code begin W1_MemoryManagement_IRQ 0 */

    /* add user code end W1_MemoryManagement_IRQ 0 */
  }
}

/**
  * @brief  this function handles bus fault exception.
  * @param  none
  * @retval none
  */
void BusFault_Handler(void)
{
  /* add user code begin BusFault_IRQ 0 */

  /* add user code end BusFault_IRQ 0 */
  /* go to infinite loop when bus fault exception occurs */
  while (1)
  {
    /* add user code begin W1_BusFault_IRQ 0 */

    /* add user code end W1_BusFault_IRQ 0 */
  }
}

/**
  * @brief  this function handles usage fault exception.
  * @param  none
  * @retval none
  */
void UsageFault_Handler(void)
{
  /* add user code begin UsageFault_IRQ 0 */

  /* add user code end UsageFault_IRQ 0 */
  /* go to infinite loop when usage fault exception occurs */
  while (1)
  {
    /* add user code begin W1_UsageFault_IRQ 0 */

    /* add user code end W1_UsageFault_IRQ 0 */
  }
}


/**
  * @brief  this function handles debug monitor exception.
  * @param  none
  * @retval none
  */
void DebugMon_Handler(void)
{
  /* add user code begin DebugMonitor_IRQ 0 */

  /* add user code end DebugMonitor_IRQ 0 */
  /* add user code begin DebugMonitor_IRQ 1 */

  /* add user code end DebugMonitor_IRQ 1 */
}

extern void xPortSysTickHandler(void);

/**
  * @brief  this function handles systick handler.
  * @param  none
  * @retval none
  */
void SysTick_Handler(void)
{
  /* add user code begin SysTick_IRQ 0 */

  /* add user code end SysTick_IRQ 0 */

#if (INCLUDE_xTaskGetSchedulerState == 1 )
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
  {
#endif /* INCLUDE_xTaskGetSchedulerState */
  xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
  }
#endif /* INCLUDE_xTaskGetSchedulerState */

  /* add user code begin SysTick_IRQ 1 */

  /* add user code end SysTick_IRQ 1 */
}

/**
  * @brief  this function handles TMR2 handler.
  * @param  none
  * @retval none
  */
void TMR2_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR2_GLOBAL_IRQ 0 */

  /* add user code end TMR2_GLOBAL_IRQ 0 */

  /* overflow interrupt management */
  if(tmr_interrupt_flag_get(TMR2, TMR_OVF_FLAG) != RESET)
  {
    /* add user code begin TMR2_TMR_OVF_FLAG */
    /* clear flag */
    tmr_flag_clear(TMR2, TMR_OVF_FLAG);
    /* add user code end TMR2_TMR_OVF_FLAG */
  }

  /* add user code begin TMR2_GLOBAL_IRQ 1 */

  /* add user code end TMR2_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles TMR3 handler.
  * @param  none
  * @retval none
  */
void TMR3_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR3_GLOBAL_IRQ 0 */

  /* add user code end TMR3_GLOBAL_IRQ 0 */

  /* overflow interrupt management */
  if(tmr_interrupt_flag_get(TMR3, TMR_OVF_FLAG) != RESET)
  {
    /* add user code begin TMR3_TMR_OVF_FLAG */
    /* clear flag */
    tmr_flag_clear(TMR3, TMR_OVF_FLAG);
    /* add user code end TMR3_TMR_OVF_FLAG */
  }

  /* add user code begin TMR3_GLOBAL_IRQ 1 */

  /* add user code end TMR3_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles TMR4 handler.
  * @param  none
  * @retval none
  */
void TMR4_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR4_GLOBAL_IRQ 0 */

  /* add user code end TMR4_GLOBAL_IRQ 0 */

  /* overflow interrupt management */
  if(tmr_interrupt_flag_get(TMR4, TMR_OVF_FLAG) != RESET)
  {
    /* add user code begin TMR4_TMR_OVF_FLAG */
    /* clear flag */
    tmr_flag_clear(TMR4, TMR_OVF_FLAG);
    /* add user code end TMR4_TMR_OVF_FLAG */
  }

  /* add user code begin TMR4_GLOBAL_IRQ 1 */

  /* add user code end TMR4_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles USART2 handler.
  * @param  none
  * @retval none
  */
void USART2_IRQHandler(void)
{
  /* add user code begin USART2_IRQ 0 */

  /* add user code end USART2_IRQ 0 */

  if(usart_interrupt_flag_get(USART2, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin USART2_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(USART2, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_COLLECTOR);
    /* add user code end USART2_USART_IDLEF_FLAG */ 
  }

  /* add user code begin USART2_IRQ 1 */

  /* add user code end USART2_IRQ 1 */
}

/**
  * @brief  this function handles USART3 handler.
  * @param  none
  * @retval none
  */
void USART3_IRQHandler(void)
{
  /* add user code begin USART3_IRQ 0 */

  /* add user code end USART3_IRQ 0 */

  if(usart_interrupt_flag_get(USART3, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin USART3_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(USART3, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_BLUETOOTH);
    /* add user code end USART3_USART_IDLEF_FLAG */ 
  }

  /* add user code begin USART3_IRQ 1 */

  /* add user code end USART3_IRQ 1 */
}

/**
  * @brief  this function handles TMR8 overflow and TMR13 handler.
  * @param  none
  * @retval none
  */
void TMR8_OVF_TMR13_IRQHandler(void)
{
  /* add user code begin TMR8_OVF_TMR13_IRQ 0 */

  /* add user code end TMR8_OVF_TMR13_IRQ 0 */

  /* overflow interrupt management */
  if(tmr_interrupt_flag_get(TMR8, TMR_OVF_FLAG) != RESET)
  {
    /* add user code begin TMR8_TMR_OVF_FLAG */
    /* clear flag */
    tmr_flag_clear(TMR8, TMR_OVF_FLAG);
    /* add user code end TMR8_TMR_OVF_FLAG */
  }

  /* add user code begin TMR8_OVF_TMR13_IRQ 1 */

  /* add user code end TMR8_OVF_TMR13_IRQ 1 */
}

/**
  * @brief  this function handles TMR5 handler.
  * @param  none
  * @retval none
  */
void TMR5_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR5_GLOBAL_IRQ 0 */

  /* add user code end TMR5_GLOBAL_IRQ 0 */

  wk_timebase_handler();

  /* add user code begin TMR5_GLOBAL_IRQ 1 */

  /* add user code end TMR5_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles UART4 handler.
  * @param  none
  * @retval none
  */
void UART4_IRQHandler(void)
{
  /* add user code begin UART4_IRQ 0 */

  /* add user code end UART4_IRQ 0 */

  if(usart_interrupt_flag_get(UART4, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin UART4_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(UART4, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_SCREEN);
    /* add user code end UART4_USART_IDLEF_FLAG */ 
  }

  /* add user code begin UART4_IRQ 1 */

  /* add user code end UART4_IRQ 1 */
}

/**
  * @brief  this function handles UART5 handler.
  * @param  none
  * @retval none
  */
void UART5_IRQHandler(void)
{
  /* add user code begin UART5_IRQ 0 */

  /* add user code end UART5_IRQ 0 */

  if(usart_interrupt_flag_get(UART5, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin UART5_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(UART5, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_XIAN485);
    /* add user code end UART5_USART_IDLEF_FLAG */ 
  }

  /* add user code begin UART5_IRQ 1 */

  /* add user code end UART5_IRQ 1 */
}

/**
  * @brief  this function handles TMR6 & DAC handler.
  * @param  none
  * @retval none
  */
void TMR6_DAC_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR6_DAC_GLOBAL_IRQ 0 */

  /* add user code end TMR6_DAC_GLOBAL_IRQ 0 */

  /* add user code begin TMR6_DAC_GLOBAL_IRQ 1 */

  /* add user code end TMR6_DAC_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles TMR7 handler.
  * @param  none
  * @retval none
  */
void TMR7_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR7_GLOBAL_IRQ 0 */

  /* add user code end TMR7_GLOBAL_IRQ 0 */

  /* overflow interrupt management */
  if(tmr_interrupt_flag_get(TMR7, TMR_OVF_FLAG) != RESET)
  {
    /* add user code begin TMR7_TMR_OVF_FLAG */
    /* clear flag */
    tmr_flag_clear(TMR7, TMR_OVF_FLAG);
    wiegand_timeout_check();
    /* add user code end TMR7_TMR_OVF_FLAG */
  }

  /* add user code begin TMR7_GLOBAL_IRQ 1 */

  /* add user code end TMR7_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles OTGFS1 handler.
  * @param  none
  * @retval none
  */
void OTGFS1_IRQHandler(void)
{
  /* add user code begin OTGFS1_IRQ 0 */

  /* add user code end OTGFS1_IRQ 0 */

  wk_otgfs1_irq_handler();

  /* add user code begin OTGFS1_IRQ 1 */

  /* add user code end OTGFS1_IRQ 1 */
}

/**
  * @brief  this function handles USART6 handler.
  * @param  none
  * @retval none
  */
void USART6_IRQHandler(void)
{
  /* add user code begin USART6_IRQ 0 */

  /* add user code end USART6_IRQ 0 */

  if(usart_interrupt_flag_get(USART6, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin USART6_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(USART6, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_4G);
    /* add user code end USART6_USART_IDLEF_FLAG */ 
  }

  /* add user code begin USART6_IRQ 1 */

  /* add user code end USART6_IRQ 1 */
}

/**
  * @brief  this function handles OTGFS2 handler.
  * @param  none
  * @retval none
  */
void OTGFS2_IRQHandler(void)
{
  /* add user code begin OTGFS2_IRQ 0 */

  /* add user code end OTGFS2_IRQ 0 */

  wk_otgfs2_irq_handler();

  /* add user code begin OTGFS2_IRQ 1 */

  /* add user code end OTGFS2_IRQ 1 */
}

/**
  * @brief  this function handles UART7 handler.
  * @param  none
  * @retval none
  */
void UART7_IRQHandler(void)
{
  /* add user code begin UART7_IRQ 0 */

  /* add user code end UART7_IRQ 0 */

  if(usart_interrupt_flag_get(UART7, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin UART7_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(UART7, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_SPARE485);
    /* add user code end UART7_USART_IDLEF_FLAG */ 
  }

  /* add user code begin UART7_IRQ 1 */

  /* add user code end UART7_IRQ 1 */
}

/**
  * @brief  this function handles UART8 handler.
  * @param  none
  * @retval none
  */
void UART8_IRQHandler(void)
{
  /* add user code begin UART8_IRQ 0 */

  /* add user code end UART8_IRQ 0 */

  if(usart_interrupt_flag_get(UART8, USART_IDLEF_FLAG) != RESET)
  {
    /* add user code begin UART8_USART_IDLEF_FLAG */
    /* clear flag */
    usart_flag_clear(UART8, USART_IDLEF_FLAG);
    bsp_uart_idle_irq(UART_PORT_ADMODULE);
    /* add user code end UART8_USART_IDLEF_FLAG */ 
  }

  /* add user code begin UART8_IRQ 1 */

  /* add user code end UART8_IRQ 1 */
}

/* add user code begin 1 */

/* CAN1 FIFO0接收中断 */
void CAN1_RX0_IRQHandler(void)
{
  can_motor_rx_irq();
}

/* EXINT4中断 - Wiegand D0 (PD4) */
void EXINT4_IRQHandler(void)
{
  if(exint_flag_get(EXINT_LINE_4) != RESET)
  {
    exint_flag_clear(EXINT_LINE_4);
    wiegand_d0_irq();
  }
}

/* EXINT9_5中断 - Wiegand D1 (PD7) */
void EXINT9_5_IRQHandler(void)
{
  if(exint_flag_get(EXINT_LINE_7) != RESET)
  {
    exint_flag_clear(EXINT_LINE_7);
    wiegand_d1_irq();
  }
}

/* PVM中断 - 电压监测 */
void PVM_IRQHandler(void)
{
  if(exint_interrupt_flag_get(EXINT_LINE_16) != RESET)
  {
    exint_flag_clear(EXINT_LINE_16);
    bsp_pvm_irq();
  }
}

/* add user code end 1 */

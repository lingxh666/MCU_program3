/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f421_int.c
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
#include "at32f421_int.h"
#include "wk_system.h"
#include "at32f421_wk_config.h"
#include "stepper_motor.h"
#include "motor_monitor.h"
/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
extern uint8_t Buf[100];
volatile uint16_t len;  /* volatile for interrupt-main loop synchronization */

/* double buffering for Modbus reception to prevent DMA overwrite */
static uint8_t rx_buf1[100];
static uint8_t rx_buf2[100];
static volatile uint8_t *active_buf = rx_buf1;  /* buffer currently used by DMA - MUST match initial DMA config */
static volatile uint8_t *process_buf = rx_buf2; /* buffer being processed by main loop */
__IO uint16_t adc1_ordinary_valuetab[200][3] = {0};
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */
uint32_t g_tmr3_seconds = 0;
volatile uint8_t g_tmr3_update_flag = 0;  /* 1 second update flag for main loop */
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
  * @brief  this function handles svcall exception.
  * @param  none
  * @retval none
  */
void SVC_Handler(void)
{
  /* add user code begin SVCall_IRQ 0 */

  /* add user code end SVCall_IRQ 0 */
  /* add user code begin SVCall_IRQ 1 */

  /* add user code end SVCall_IRQ 1 */
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

/**
  * @brief  this function handles pendsv_handler exception.
  * @param  none
  * @retval none
  */
void PendSV_Handler(void)
{
  /* add user code begin PendSV_IRQ 0 */

  /* add user code end PendSV_IRQ 0 */
  /* add user code begin PendSV_IRQ 1 */

  /* add user code end PendSV_IRQ 1 */
}

/**
  * @brief  this function handles systick handler.
  * @param  none
  * @retval none
  */
void SysTick_Handler(void)
{
  /* add user code begin SysTick_IRQ 0 */

  /* add user code end SysTick_IRQ 0 */

  /* add user code begin SysTick_IRQ 1 */

  /* add user code end SysTick_IRQ 1 */
}

/**
  * @brief  this function handles TMR3 handler.
  * @param  none
  * @retval none
  */
void TMR3_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR3_GLOBAL_IRQ 0 */
    if( tmr_flag_get( TMR3, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR3, TMR_OVF_FLAG );
        g_tmr3_seconds++;        /* 1 second increment */
        g_tmr3_update_flag = 1;  /* set flag for main loop processing */
    }
  /* add user code end TMR3_GLOBAL_IRQ 0 */

  /* add user code begin TMR3_GLOBAL_IRQ 1 */

  /* add user code end TMR3_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles TMR6 handler.
  * @param  none
  * @retval none
  */
void TMR6_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR6_GLOBAL_IRQ 0 */

  /* add user code end TMR6_GLOBAL_IRQ 0 */

  wk_timebase_handler();

  /* 1kHz motor speed update in interrupt for stable acceleration timing */
  MotorUpdate(MOTOR_ID_1);
  MotorUpdate(MOTOR_ID_2);
  /* add user code begin TMR6_GLOBAL_IRQ 1 */

  /* add user code end TMR6_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles DMA1 Channel1 interrupt (ADC1 DMA transfer complete).
  * @param  none
  * @retval none
  */
void DMA1_Channel1_IRQHandler(void)
{
  /* add user code begin DMA1_Channel1_IRQ 0 */
  if (dma_interrupt_flag_get(DMA1_FDT1_FLAG) == SET)
  {
    dma_flag_clear(DMA1_GL1_FLAG);
    MotorMonitor_OnAdcDmaTransferCompleteISR();
  }
  else if (dma_interrupt_flag_get(DMA1_DTERR1_FLAG) == SET)
  {
    dma_flag_clear(DMA1_GL1_FLAG);
  }
  /* add user code end DMA1_Channel1_IRQ 0 */

  /* add user code begin DMA1_Channel1_IRQ 1 */

  /* add user code end DMA1_Channel1_IRQ 1 */
}

/**
  * @brief  this function handles TMR14 handler.
  * @param  none
  * @retval none
  */
void TMR14_GLOBAL_IRQHandler(void)
{
  /* add user code begin TMR14_GLOBAL_IRQ 0 */

  /* add user code end TMR14_GLOBAL_IRQ 0 */

  /* add user code begin TMR14_GLOBAL_IRQ 1 */

  /* add user code end TMR14_GLOBAL_IRQ 1 */
}

/**
  * @brief  this function handles USART2 handler.
  * @param  none
  * @retval none
  */
void USART2_IRQHandler(void)
{

  /* add user code begin USART2_IRQ 0 */
    if(usart_flag_get(USART2, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(USART2, USART_IDLEF_FLAG);
        usart_data_receive(USART2);

        dma_channel_enable(DMA1_CHANNEL5, FALSE);
        len = sizeof(Buf) - dma_data_number_get(DMA1_CHANNEL5);

        /* swap buffers: active becomes process, process becomes active */
        volatile uint8_t *temp = active_buf;
        active_buf = process_buf;
        process_buf = temp;

        /* reconfigure DMA to new active buffer */
        dma_channel_enable(DMA1_CHANNEL5, FALSE);
        dma_data_number_set(DMA1_CHANNEL5, sizeof(Buf));
        wk_dma_channel_config(DMA1_CHANNEL5,
                              (uint32_t)&USART2->dt,
                              (uint32_t)active_buf,
                              sizeof(Buf));
        dma_channel_enable(DMA1_CHANNEL5, TRUE);
    }

  /* add user code end USART2_IRQ 0 */

  /* add user code begin USART2_IRQ 1 */

  /* add user code end USART2_IRQ 1 */
}

/* add user code begin 1 */

/**
  * @brief  initialize Modbus double buffering (call after DMA initialization)
  * @param  none
  * @retval none
  * @note   MUST be called after wk_dma1_channel5_init() to reconfigure DMA buffer
  */
void Modbus_InitDoubleBuffer(void)
{
  /* reconfigure DMA to use active_buf instead of Buf */
  dma_channel_enable(DMA1_CHANNEL5, FALSE);
  dma_data_number_set(DMA1_CHANNEL5, 100);
  wk_dma_channel_config(DMA1_CHANNEL5,
                        (uint32_t)&USART2->dt,
                        (uint32_t)active_buf,
                        100);
  dma_channel_enable(DMA1_CHANNEL5, TRUE);
}

/**
  * @brief  get Modbus received data safely (thread-safe with double buffering)
  * @param  buf: output buffer pointer (will be set to point to received data)
  * @retval received data length (0 if no data available)
  * @note   this function is called from main loop to safely access interrupt data
  */
uint16_t Modbus_GetRxData(const uint8_t **buf)
{
  uint16_t rx_len;

  /* check if data available */
  if (len == 0)
  {
    *buf = NULL;
    return 0;
  }

  /* disable interrupts briefly to copy data safely */
  __disable_irq();
  rx_len = len;
  len = 0;  /* clear flag */
  *buf = (const uint8_t *)process_buf;
  __enable_irq();

  return rx_len;
}

/* add user code end 1 */

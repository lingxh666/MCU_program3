/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
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

/* Includes ------------------------------------------------------------------*/
#include "at32f435_437_wk_config.h"
#include "wk_system.h"
#include "freertos_app.h"
#include "arm_math.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_rtc.h"
#include "bsp_uart.h"
#include "bsp_can_motor.h"
#include "bsp_wiegand.h"
#include "bsp_adc.h"
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

/* add user code end 0 */

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  /* add user code begin 1 */
//	nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x2000);
  /* add user code end 1 */

  /* system clock config. */
  wk_system_clock_config();

  /* config periph clock. */
  wk_periph_clock_config();

  /* nvic config. */
  wk_nvic_config();

  /* timebase config. */
  wk_timebase_init();

  /* init gpio function. */
  wk_gpio_config();

  /* init adc-common function. */
  wk_adc_common_init();

  /* init adc1 function. */
  wk_adc1_init();

  /* init adc2 function. */
  wk_adc2_init();

  /* init dma1 channel1 */
  wk_dma1_channel1_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL1, 
                        (uint32_t)&USART2->dt, 
                        DMA1_CHANNEL1_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL1_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* init dma1 channel2 */
  wk_dma1_channel2_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL2, 
                        (uint32_t)&USART3->dt, 
                        DMA1_CHANNEL2_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL2_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL2, TRUE);

  /* init dma1 channel3 */
  wk_dma1_channel3_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL3, 
                        (uint32_t)&UART4->dt, 
                        DMA1_CHANNEL3_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL3_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL3, TRUE);

  /* init dma1 channel4 */
  wk_dma1_channel4_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL4, 
                        (uint32_t)&UART5->dt, 
                        DMA1_CHANNEL4_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL4_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL4, TRUE);

  /* init dma1 channel5 */
  wk_dma1_channel5_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL5, 
                        (uint32_t)&USART6->dt, 
                        DMA1_CHANNEL5_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL5_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL5, TRUE);

  /* init dma1 channel6 */
  wk_dma1_channel6_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL6, 
                        (uint32_t)&UART7->dt, 
                        DMA1_CHANNEL6_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL6_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL6, TRUE);

  /* init dma1 channel7 */
  wk_dma1_channel7_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL7, 
                        (uint32_t)&UART8->dt, 
                        DMA1_CHANNEL7_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL7_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL7, TRUE);

  /* init dma2 channel1 */
  wk_dma2_channel1_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA2_CHANNEL1, 
                        (uint32_t)&ADC1->odt, 
                        DMA2_CHANNEL1_MEMORY_BASE_ADDR, 
                        DMA2_CHANNEL1_BUFFER_SIZE);
  dma_channel_enable(DMA2_CHANNEL1, TRUE);

  /* init dma2 channel2 */
  wk_dma2_channel2_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA2_CHANNEL2, 
                        (uint32_t)&ADC2->odt, 
                        DMA2_CHANNEL2_MEMORY_BASE_ADDR, 
                        DMA2_CHANNEL2_BUFFER_SIZE);
  dma_channel_enable(DMA2_CHANNEL2, TRUE);

  /* init usart1 function. */
  wk_usart1_init();

  /* init usart2 function. */
  wk_usart2_init();

  /* init usart3 function. */
  wk_usart3_init();

  /* init uart4 function. */
  wk_uart4_init();

  /* init uart5 function. */
  wk_uart5_init();

  /* init usart6 function. */
  wk_usart6_init();

  /* init uart7 function. */
  wk_uart7_init();

  /* init uart8 function. */
  wk_uart8_init();

  /* init acc function. */
  wk_acc_init();

  /* init usb_otgfs1 function. */
  wk_usb_otgfs1_init();

  /* init usb_otgfs2 function. */
  wk_usb_otgfs2_init();

  /* init can1 function. */
  wk_can1_init();

  /* init qspi2 function. */
  wk_qspi2_init();

  /* init crc function. */
  wk_crc_init();

  /* init wdt function. */
  wk_wdt_init();

  /* init ertc function. */
  /* wk_ertc_init() 每次上电会重置时间，改用bsp_rtc_init()通过BPR判断是否首次 */
  bsp_rtc_init();

  /* init exint function. */
  wk_exint_config();

  /* init tmr2 function. */
  wk_tmr2_init();

  /* init tmr3 function. */
  wk_tmr3_init();

  /* init tmr4 function. */
  wk_tmr4_init();

  /* init tmr6 function. */
  wk_tmr6_init();

  /* init tmr7 function. */
  wk_tmr7_init();

  /* init tmr8 function. */
  wk_tmr8_init();

  /* add user code begin 2 */
  bsp_uart_init();
  can_motor_init();
  wiegand_init();
  bsp_adc1_dma_start();   /* 启动ADC1 DMA连续采样 */
  /* add user code end 2 */

  /* init freertos function. */
  wk_freertos_init();

  while(1)
  {
    /* add user code begin 3 */

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */

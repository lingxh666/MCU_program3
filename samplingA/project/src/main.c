/* add user code begin Header */
/**
  **************************************************************************
  * @file     main.c
  * @brief    main program
  **************************************************************************
  *                       Copyright notice & Disclaimer
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
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_button.h"
#include "multi_button.h"
#include "work.h"
#include "wiegand.h"
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
    nvic_vector_table_set(NVIC_VECTTAB_FLASH, 0x2000);
    /* add user code end 1 */

    /* system clock config. */
    wk_system_clock_config();

    /* config periph clock. */
    wk_periph_clock_config();

    /* init debug function. */
    wk_debug_config();

    /* nvic config. */
    wk_nvic_config();

    /* timebase config. */
    wk_timebase_init();

    /* init gpio function. */
    wk_gpio_config();

    /* init adc1 function. */
    wk_adc1_init();

    /* init dma1 channel1 */
    wk_dma1_channel1_init();
    wk_dma_channel_config(DMA1_CHANNEL1, (uint32_t)&ADC1->odt, (uint32_t)adc1_ordinary_valuetab, 200 * 9);  // 200���9ͨ��=1800
    dma_channel_enable(DMA1_CHANNEL1, TRUE);
    wk_dma1_channel2_init();
    wk_dma_channel_config(DMA1_CHANNEL2, (uint32_t)&USART2->dt, (uint32_t)UART2_Buf, 160);
    dma_channel_enable(DMA1_CHANNEL2, TRUE);
    wk_dma1_channel3_init();
    wk_dma_channel_config(DMA1_CHANNEL3, (uint32_t)&USART3->dt, (uint32_t)UART3_Buf, 100);
    dma_channel_enable(DMA1_CHANNEL3, TRUE);
    wk_dma1_channel4_init();
    wk_dma_channel_config(DMA1_CHANNEL4, (uint32_t)&UART4->dt, (uint32_t)UART4_Buf, 100);
    dma_channel_enable(DMA1_CHANNEL4, TRUE);
    wk_dma1_channel5_init();
    wk_dma_channel_config(DMA1_CHANNEL5, (uint32_t)&UART5->dt, (uint32_t)UART5_Buf, 100);
    dma_channel_enable(DMA1_CHANNEL5, TRUE);
    wk_dma1_channel6_init();
    wk_dma_channel_config(DMA1_CHANNEL6, (uint32_t)&USART6->dt, (uint32_t)UART6_Buf, 512);
    dma_channel_enable(DMA1_CHANNEL6, TRUE);
    wk_dma1_channel7_init();
    wk_dma_channel_config(DMA1_CHANNEL7, (uint32_t)&UART7->dt, (uint32_t)UART7_Buf, 100);
    dma_channel_enable(DMA1_CHANNEL7, TRUE);
    wk_dma2_channel1_init();
    wk_dma_channel_config(DMA2_CHANNEL1, (uint32_t)&UART8->dt, (uint32_t)UART8_Buf, 100);
    dma_channel_enable(DMA2_CHANNEL1, TRUE);

    /* init usart1 function. */
    wk_usart1_init();

    /* init usart2 function. */
    wk_usart2_init();

    /* init usart3 function. */
    wk_usart3_init();

    /* init uart4 function. */
    // wk_uart4_init();  // ★ 延迟初始化到排水前，避免屏幕开机发送数据导致MCU启动卡死

    /* init uart5 function. */
    wk_uart5_init();

    /* init usart6 function. */
    wk_usart6_init();

    /* init uart7 function. */
    wk_uart7_init();

    /* init uart8 function. */
    wk_uart8_init();

    /* init spi2 function. */
    wk_spi2_init();

    /* init rtc function. */
    wk_rtc_init();

    /* init exint function. */
    wk_exint_config();

    /* init tmr1 function. */
    wk_tmr1_init();

    /* init tmr2 function. */
    wk_tmr2_init();

    /* init tmr3 function. */
    wk_tmr3_init();

    /* init tmr4 function. */
    wk_tmr4_init();

    /* init tmr5 function. */
    wk_tmr5_init();

    /* init tmr6 function. */
    wk_tmr6_init();

    /* init crc function. */
    wk_crc_init();

    /* init wdt function. */
    wk_wdt_init();
    BTNinit();
    wiegand_init();
		delay_init();
    /* init freertos function. */
    wk_freertos_init();


}

/* add user code begin 4 */

/* add user code end 4 */

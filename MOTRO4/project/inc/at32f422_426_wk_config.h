/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f422_426_wk_config.h
  * @brief    header file of work bench config
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

/* define to prevent recursive inclusion -----------------------------------*/
#ifndef __AT32F422_426_WK_CONFIG_H
#define __AT32F422_426_WK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes -----------------------------------------------------------------------*/
#include "stdio.h"
#include "at32f422_426.h"

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

/* add user code begin dma define */
/* user can only modify the dma define value */
#include "motor_monitor.h"
#define DMA1_CHANNEL1_BUFFER_SIZE   ADC_DMA_TOTAL_SIZE
#define DMA1_CHANNEL1_MEMORY_BASE_ADDR   ((uint32_t)adc_dma_buf)
//#define DMA1_CHANNEL1_PERIPHERAL_BASE_ADDR  0

//#define DMA1_CHANNEL2_BUFFER_SIZE   0
//#define DMA1_CHANNEL2_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL2_PERIPHERAL_BASE_ADDR   0

//#define DMA1_CHANNEL3_BUFFER_SIZE   0
//#define DMA1_CHANNEL3_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL3_PERIPHERAL_BASE_ADDR   0

//#define DMA1_CHANNEL4_BUFFER_SIZE   0
//#define DMA1_CHANNEL4_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL4_PERIPHERAL_BASE_ADDR   0

//#define DMA1_CHANNEL5_BUFFER_SIZE   0
//#define DMA1_CHANNEL5_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL5_PERIPHERAL_BASE_ADDR   0

//#define DMA1_CHANNEL6_BUFFER_SIZE   0
//#define DMA1_CHANNEL6_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL6_PERIPHERAL_BASE_ADDR   0

//#define DMA1_CHANNEL7_BUFFER_SIZE   0
//#define DMA1_CHANNEL7_MEMORY_BASE_ADDR   0
//#define DMA1_CHANNEL7_PERIPHERAL_BASE_ADDR   0
/* add user code end dma define */

/* Private defines -------------------------------------------------------------*/
#define AD4_PIN    GPIO_PINS_3
#define AD4_GPIO_PORT    GPIOA
#define AD3_PIN    GPIO_PINS_4
#define AD3_GPIO_PORT    GPIOA
#define AD1_PIN    GPIO_PINS_5
#define AD1_GPIO_PORT    GPIOA
#define AD2_PIN    GPIO_PINS_6
#define AD2_GPIO_PORT    GPIOA
#define LED4_PIN    GPIO_PINS_7
#define LED4_GPIO_PORT    GPIOA
#define LED1_PIN    GPIO_PINS_0
#define LED1_GPIO_PORT    GPIOB
#define LED2_PIN    GPIO_PINS_1
#define LED2_GPIO_PORT    GPIOB
#define LED3_PIN    GPIO_PINS_2
#define LED3_GPIO_PORT    GPIOB
#define CW2_PIN    GPIO_PINS_6
#define CW2_GPIO_PORT    GPIOF
#define CW3_PIN    GPIO_PINS_7
#define CW3_GPIO_PORT    GPIOF
#define STEP4_PIN    GPIO_PINS_4
#define STEP4_GPIO_PORT    GPIOB
#define STEP3_PIN    GPIO_PINS_5
#define STEP3_GPIO_PORT    GPIOB
#define CW4_PIN    GPIO_PINS_6
#define CW4_GPIO_PORT    GPIOB
#define STEP2_PIN    GPIO_PINS_7
#define STEP2_GPIO_PORT    GPIOB
#define CW1_PIN    GPIO_PINS_8
#define CW1_GPIO_PORT    GPIOB
#define STEP1_PIN    GPIO_PINS_9
#define STEP1_GPIO_PORT    GPIOB

/* exported functions ------------------------------------------------------- */
  /* system clock config. */
  void wk_system_clock_config(void);

  /* config periph clock. */
  void wk_periph_clock_config(void);

  /* nvic config. */
  void wk_nvic_config(void);

  /* init gpio function. */
  void wk_gpio_config(void);

  /* init adc1 function. */
  void wk_adc1_init(void);

  /* init can1 function. */
  void wk_can1_init(void);

  /* init tmr3 function. */
  void wk_tmr3_init(void);

  /* init tmr14 function. */
  void wk_tmr4_init(void);

  /* init tmr6 function. */
  void wk_tmr6_init(void);

  /* init tmr15 function. */
  void wk_tmr15_init(void);

  /* init tmr17 function. */
  void wk_tmr17_init(void);

  /* init usart1 function. */
  void wk_usart1_init(void);

  /* init wdt function. */
  void wk_wdt_init(void);

  /* init dma1 channel1 */
  void wk_dma1_channel1_init(void);

  /* config dma channel transfer parameter */
  /* user need to modify parameters memory_base_addr and buffer_size */
  void wk_dma_channel_config(dma_channel_type* dmax_channely, uint32_t peripheral_base_addr, uint32_t memory_base_addr, uint16_t buffer_size);

/* add user code begin exported functions */

/* add user code end exported functions */

#ifdef __cplusplus
}
#endif

#endif

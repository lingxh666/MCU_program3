/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f421_wk_config.h
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
#ifndef __AT32F421_WK_CONFIG_H
#define __AT32F421_WK_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* includes -----------------------------------------------------------------------*/
#include "stdio.h"
#include "at32f421.h"

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
/* DMA1_CHANNEL1: ADC1 - 3 channels (motor1 current, vref 2.5V, motor2 current) */
#define DMA1_CHANNEL1_BUFFER_SIZE   3000   /* 3 channels x 1000 samples */
#define DMA1_CHANNEL1_MEMORY_BASE_ADDR   0  /* will be set in motor_monitor.c */
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

#define DMA1_CHANNEL5_BUFFER_SIZE   100
#define DMA1_CHANNEL5_MEMORY_BASE_ADDR   ((uint32_t)Buf)
//#define DMA1_CHANNEL5_PERIPHERAL_BASE_ADDR   0
/* add user code end dma define */

/* Private defines -------------------------------------------------------------*/
#define LED1_PIN    GPIO_PINS_0
#define LED1_GPIO_PORT    GPIOF
#define LED2_PIN    GPIO_PINS_1
#define LED2_GPIO_PORT    GPIOF
#define CW2_PIN    GPIO_PINS_0
#define CW2_GPIO_PORT    GPIOA
#define ADC_PIN    GPIO_PINS_5
#define ADC_GPIO_PORT    GPIOA
#define ADC2_PIN    GPIO_PINS_6
#define ADC2_GPIO_PORT    GPIOA
#define ADC1_PIN    GPIO_PINS_1
#define ADC1_GPIO_PORT    GPIOB
#define CW1_PIN    GPIO_PINS_10
#define CW1_GPIO_PORT    GPIOA

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

  /* init crc function. */
  void wk_crc_init(void);

  /* init tmr3 function. */
  void wk_tmr3_init(void);

  /* init tmr14 function. */
  void wk_tmr14_init(void);

  /* init tmr15 function. */
  void wk_tmr15_init(void);

  /* init tmr17 function. */
  void wk_tmr17_init(void);

  /* init usart1 function. */
  void wk_usart1_init(void);

  /* init usart2 function. */
  void wk_usart2_init(void);

  /* init wdt function. */
  void wk_wdt_init(void);

  /* init dma1 channel1 */
  void wk_dma1_channel1_init(void);

  /* init dma1 channel5 */
  void wk_dma1_channel5_init(void);

  /* config dma channel transfer parameter */
  /* user need to modify parameters memory_base_addr and buffer_size */
  void wk_dma_channel_config(dma_channel_type* dmax_channely, uint32_t peripheral_base_addr, uint32_t memory_base_addr, uint16_t buffer_size);

/* add user code begin exported functions */
uint8_t CRCCheck(uint8_t *data, uint16_t length);
uint16_t CRC16_MODBUS(uint8_t *data, uint16_t length);
void SendData( uint8_t *buf, uint8_t len);
void Modbus_InitDoubleBuffer(void);
uint16_t Modbus_GetRxData(const uint8_t **buf);

/* add user code end exported functions */

#ifdef __cplusplus
}
#endif

#endif

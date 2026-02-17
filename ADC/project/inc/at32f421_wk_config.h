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
/* ADC通道与采样参数 */
#define ADC_CH_COUNT        7     /* 总通道数(含校准通道CH9) */
#define ADC_DATA_CH_COUNT   6     /* 数据通道数 */
#define ADC_SAMPLE_COUNT    256   /* 每通道采样点数 */
#define ADC_CAL_CH_INDEX    6     /* 校准通道在序列中的索引 */

/* 串口帧定义: 帧头(6BB6) + 6x2字节数据 + 帧尾(8CC8) = 16字节 */
#define FRAME_HEAD_H        0x6B
#define FRAME_HEAD_L        0xB6
#define FRAME_TAIL_H        0x8C
#define FRAME_TAIL_L        0xC8
#define FRAME_LEN           16
/* add user code end exported macro */

/* add user code begin dma define */
/* user can only modify the dma define value */
/* 双缓冲: 7通道 x 256采样 x 2(半传输+全传输) = 3584 */
#define ADC_DMA_BUF_SIZE                (ADC_CH_COUNT * ADC_SAMPLE_COUNT * 2)
#define DMA1_CHANNEL1_BUFFER_SIZE       ADC_DMA_BUF_SIZE
#define DMA1_CHANNEL1_MEMORY_BASE_ADDR  0  /* 在main.c中动态设置 */
/* add user code end dma define */

/* exported functions ------------------------------------------------------- */
  /* system clock config. */
  void wk_system_clock_config(void);

  /* config periph clock. */
  void wk_periph_clock_config(void);

  /* nvic config. */
  void wk_nvic_config(void);

  /* init adc1 function. */
  void wk_adc1_init(void);

  /* init tmr6 function. */
  void wk_tmr6_init(void);

  /* init tmr16 function. */
  void wk_tmr16_init(void);

  /* init usart1 function. */
  void wk_usart1_init(void);

  /* init usart2 function. */
  void wk_usart2_init(void);

  /* init wdt function. */
  void wk_wdt_init(void);

  /* init dma1 channel1 */
  void wk_dma1_channel1_init(void);

  /* config dma channel transfer parameter */
  /* user need to modify parameters memory_base_addr and buffer_size */
  void wk_dma_channel_config(dma_channel_type* dmax_channely, uint32_t peripheral_base_addr, uint32_t memory_base_addr, uint16_t buffer_size);

/* add user code begin exported functions */
/* 全局变量(定义在main.c) */
extern uint16_t adc_dma_buf[ADC_DMA_BUF_SIZE];
extern volatile uint16_t filtered_adc[ADC_CH_COUNT];
extern volatile uint8_t  buf_ready_flag;   /* 0=无 1=前半区 2=后半区 */
extern volatile uint8_t  send_flag;        /* 1=定时发送触发 */
extern volatile uint32_t dma_isr_count;    /* DMA中断计数(调试) */

/* 用户函数 */
void usart1_send_byte(uint8_t data);
void adc_filter_process(uint16_t *buf_start);
void usart1_send_frame(void);
/* add user code end exported functions */

#ifdef __cplusplus
}
#endif

#endif

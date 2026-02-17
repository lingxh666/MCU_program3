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
#include "at32f421_wk_config.h"
#include "wk_system.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#pragma diag_suppress 870  /* 抑制ARMCC v5中文字符警告 */
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
uint16_t adc_dma_buf[ADC_DMA_BUF_SIZE];          /* DMA双缓冲区 */
volatile uint16_t filtered_adc[ADC_CH_COUNT];     /* 滤波后ADC值 */
volatile uint8_t  buf_ready_flag = 0;             /* 0=无 1=前半区 2=后半区 */
volatile uint8_t  send_flag = 0;                  /* 1=定时发送触发 */
volatile uint32_t dma_isr_count = 0;              /* DMA中断计数(调试) */
/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/**
  * @brief  USART1发送单字节(轮询)
  */
void usart1_send_byte(uint8_t data)
{
  while(usart_flag_get(USART1, USART_TDBE_FLAG) == RESET);
  usart_data_transmit(USART1, data);
}

/**
  * @brief  极值剔除滤波，处理一个半区(7通道 x 256采样)
  * @param  buf_start 半区起始地址
  */
void adc_filter_process(uint16_t *buf_start)
{
  uint8_t ch;
  uint16_t i;

  for(ch = 0; ch < ADC_CH_COUNT; ch++)
  {
    uint32_t sum = 0;
    uint16_t max_val = 0;
    uint16_t min_val = 4095;

    for(i = 0; i < ADC_SAMPLE_COUNT; i++)
    {
      uint16_t v = buf_start[i * ADC_CH_COUNT + ch];
      sum += v;
      if(v > max_val) max_val = v;
      if(v < min_val) min_val = v;
    }
    /* 去掉一个最大值和一个最小值后取平均 */
    filtered_adc[ch] = (uint16_t)((sum - max_val - min_val) / (ADC_SAMPLE_COUNT - 2));
  }
}

/**
  * @brief  计算电流发送值(mA x 1000)
  * @param  adc_val  通道ADC滤波值
  * @param  ref_val  校准通道ADC值(已保证非零)
  * @retval 电流值(mA x 1000)，上限0xFFFF
  */
static uint32_t calc_current(uint16_t adc_val, uint16_t ref_val)
{
  /* adc × 2500000 / (ref × 150) = adc × 50000 / (ref × 3) */
  uint32_t val = (uint32_t)adc_val * 50000UL / ((uint32_t)ref_val * 3UL);
  if(val > 0xFFFF) val = 0xFFFF;
  return val;
}

/**
  * @brief  组帧并通过USART1发送6通道电流数据
  *         帧格式: 6BB6 + 6x2字节(电流mA x 1000) + 8CC8 = 16字节
  */
void usart1_send_frame(void)
{
  uint8_t frame[FRAME_LEN];
  uint16_t ch9_adc = filtered_adc[ADC_CAL_CH_INDEX];
  uint8_t i;

  if(ch9_adc == 0) ch9_adc = 1;  /* 防除零 */

  frame[0] = FRAME_HEAD_H;
  frame[1] = FRAME_HEAD_L;

  for(i = 0; i < ADC_DATA_CH_COUNT; i++)
  {
    uint32_t send_val = calc_current(filtered_adc[i], ch9_adc);
    frame[2 + i * 2]     = (uint8_t)(send_val >> 8);
    frame[2 + i * 2 + 1] = (uint8_t)(send_val & 0xFF);
  }

  frame[14] = FRAME_TAIL_H;
  frame[15] = FRAME_TAIL_L;

  for(i = 0; i < FRAME_LEN; i++)
  {
    usart1_send_byte(frame[i]);
  }
}

/* add user code end 0 */

/**
  * @brief main function.
  * @param  none
  * @retval none
  */
int main(void)
{
  /* add user code begin 1 */

  /* add user code end 1 */

  /* system clock config. */
  wk_system_clock_config();

  /* config periph clock. */
  wk_periph_clock_config();

  /* nvic config. */
  wk_nvic_config();

  /* timebase config. */
  wk_timebase_init();

  /* init adc1 function. */
  wk_adc1_init();

  /* init dma1 channel1 */
  wk_dma1_channel1_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL1,
                        (uint32_t)&ADC1->odt,
                        (uint32_t)adc_dma_buf,
                        DMA1_CHANNEL1_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* init usart1 function. */
  wk_usart1_init();

  /* init usart2 function. */
  wk_usart2_init();

  /* init wdt function. */
  wk_wdt_init();

  /* init tmr6 function. */
  wk_tmr6_init();

  /* init tmr16 function. */
  wk_tmr16_init();

  /* add user code begin 2 */
  adc_ordinary_software_trigger_enable(ADC1, TRUE);
  printf("ADC 6通道4-20mA采集启动\r\n");
  printf("DMA缓冲区: %d字, 地址: 0x%08X\r\n", ADC_DMA_BUF_SIZE, (uint32_t)adc_dma_buf);
  /* add user code end 2 */

  while(1)
  {
    /* add user code begin 3 */
    /* DMA半区就绪 → 滤波 */
    if(buf_ready_flag != 0)
    {
      uint16_t offset = (buf_ready_flag == 1) ? 0 : (ADC_CH_COUNT * ADC_SAMPLE_COUNT);
      buf_ready_flag = 0;
      adc_filter_process(&adc_dma_buf[offset]);
    }

    /* 1s定时到 → 组帧发送 + 调试输出 */
    if(send_flag)
    {
      uint8_t k;
      send_flag = 0;
      usart1_send_frame();
      /* 发送成功后喂狗，约6.5s超时，连续5次未发送则复位 */
      wdt_counter_reload();

      printf("DMA中断:%u\r\n", dma_isr_count);
      printf("ADC值: CH0=%u CH1=%u CH3=%u CH4=%u CH5=%u CH6=%u CH9=%u\r\n",
             filtered_adc[0], filtered_adc[1], filtered_adc[2],
             filtered_adc[3], filtered_adc[4], filtered_adc[5], filtered_adc[6]);

      if(filtered_adc[ADC_CAL_CH_INDEX] > 0)
      {
        printf("电流(mA): ");
        for(k = 0; k < ADC_DATA_CH_COUNT; k++)
        {
          uint32_t val = calc_current(filtered_adc[k], filtered_adc[ADC_CAL_CH_INDEX]);
          printf("CH%d=%u.%03u ", k, (uint16_t)(val / 1000), (uint16_t)(val % 1000));
        }
        printf("\r\n");
      }
    }
    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */

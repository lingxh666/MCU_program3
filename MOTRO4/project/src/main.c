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
#include "at32f422_426_wk_config.h"
#include "wk_system.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "stepper_motor.h"
#include "motor_monitor.h"
#include "can_protocol.h"
#include <stdio.h>
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
extern volatile uint8_t g_1s_update_flag;
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

  /* init adc1 function. */
  wk_adc1_init();

  /* init dma1 channel1 */
  wk_dma1_channel1_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL1, 
                        (uint32_t)&ADC1->odt, 
                        DMA1_CHANNEL1_MEMORY_BASE_ADDR, 
                        DMA1_CHANNEL1_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL1, TRUE);

  /* init usart1 function. */
  wk_usart1_init();

  /* init can1 function. */
  wk_can1_init();

  /* init wdt function. */
  wk_wdt_init();

  /* init tmr3 function. */
  wk_tmr3_init();

  /* init tmr4 function. */
  wk_tmr4_init();

  /* init tmr6 function. */
  wk_tmr6_init();

  /* init tmr15 function. */
  wk_tmr15_init();

  /* init tmr17 function. */
  wk_tmr17_init();

  /* add user code begin 2 */
  stepper_init();
  MotorMonitorInit();
  CanProtocolInit();
  printf("MOTRO4 启动 OK\r\n");
  wdt_enable();
  /* add user code end 2 */

  while(1)
  {
    /* add user code begin 3 */
    /* 处理CAN接收命令 */
    CanProtocolProcess();

    /* 1秒周期任务 */
    if (g_1s_update_flag)
    {
      g_1s_update_flag = 0;
      MotorUpdateLEDIndicator();
      MotorMonitorUpdate();
      /* 打印4路电机状态 */
      {
        uint8_t i;
        for(i = 0; i < MOTOR_COUNT; i++)
        {
          uint16_t rpm = MotorGetSpeedRPM((motor_id_t)i);
          if(rpm > 0)
            printf("M%d: %uRPM %uHz dir=%d\r\n", i, rpm, RPM_TO_HZ(rpm), MotorGetDirection((motor_id_t)i));
        }
        /* 打印4路ADC电流 */
        printf("ADC: %u,%u,%u,%u mA  raw: %u,%u,%u,%u\r\n",
          motor_monitor[0].current_ma, motor_monitor[1].current_ma,
          motor_monitor[2].current_ma, motor_monitor[3].current_ma,
          motor_monitor[0].raw_adc, motor_monitor[1].raw_adc,
          motor_monitor[2].raw_adc, motor_monitor[3].raw_adc);
      }
    }

    /* 喂狗 */
    wdt_counter_reload();
    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */

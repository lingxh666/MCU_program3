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
#include "at32f421_int.h"
#include "stepper_motor.h"
#include "motor_monitor.h"
#include "modbus_rtu.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
uint8_t Buf[100];

/* test variables */
uint8_t test_state = 0;
uint16_t test_rpm = 0;
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
  /* ADC current monitoring DMA is configured and started in MotorMonitorInit() */

  /* init dma1 channel5 */
  wk_dma1_channel5_init();
  /* config dma channel transfer parameter */
  /* user need to modify define values DMAx_CHANNELy_XXX_BASE_ADDR and DMAx_CHANNELy_BUFFER_SIZE in at32xxx_wk_config.h */
  wk_dma_channel_config(DMA1_CHANNEL5,
                        (uint32_t)&USART2->dt,
                        DMA1_CHANNEL5_MEMORY_BASE_ADDR,
                        DMA1_CHANNEL5_BUFFER_SIZE);
  dma_channel_enable(DMA1_CHANNEL5, TRUE);

  /* reconfigure DMA to use double buffering (MUST be after dma_channel_enable) */
  Modbus_InitDoubleBuffer();

  /* init usart1 function. */
  wk_usart1_init();

  /* init usart2 function. */
  wk_usart2_init();

  /* init crc function. */
  wk_crc_init();

  /* init wdt function. */
  wk_wdt_init();

  /* init tmr3 function. */
  wk_tmr3_init();

  /* init tmr14 function. */
  wk_tmr14_init();

  /* init tmr15 function. */
  wk_tmr15_init();

  /* init tmr17 function. */
  wk_tmr17_init();

  /* add user code begin 2 */

  /* initialize stepper motor control */
  stepper_init();

  /* initialize motor current monitoring */
  MotorMonitorInit();

  /* initialize Modbus RTU slave (detects slave address from PA1) */
  Modbus_Init();

  /* test motors can be started via Modbus commands */
  /* MotorSetAcceleration(MOTOR_ID_1, 500); */
  /* MotorSetAcceleration(MOTOR_ID_2, 500); */
  /* MotorRun(MOTOR_ID_1, 150, MOTOR_DIR_CW); */
  /* MotorRun(MOTOR_ID_2, 800, MOTOR_DIR_CW); */

  /* add user code end 2 */

  while(1)
  {
    /* add user code begin 3 */

    /* feed watchdog */
    wdt_counter_reload();

    /* Modbus RTU processing (non-blocking) */
    Modbus_Process();

    /* 1 second update task (state machine triggered by TMR3 interrupt flag) */
    if (g_tmr3_update_flag)
    {
      g_tmr3_update_flag = 0;  /* clear flag */

      /* update LED indicators */
      MotorUpdateLEDIndicator();

      /* update motor current monitoring */
      MotorMonitorUpdate();

      /* update Modbus holding registers from motor status */
      Modbus_UpdateHoldingRegs();
    }

    /* print status every 500ms (non-blocking using timebase ticks) */
    static uint32_t print_tick = 0;
    if (wk_timebase_get() - print_tick >= 500)
    {
      print_tick = wk_timebase_get();
      printf("M1: %d RPM, M2: %d RPM\r\n",
             MotorGetSpeedRPM(MOTOR_ID_1),
             MotorGetSpeedRPM(MOTOR_ID_2));
    }

    /* sleep until next interrupt (e.g., TMR6 1kHz) */
    __WFI();

    /* add user code end 3 */
  }
}

  /* add user code begin 4 */

  /* add user code end 4 */

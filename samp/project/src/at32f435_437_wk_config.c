/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f435_437_wk_config.c
  * @brief    work bench config program
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

#include "at32f435_437_wk_config.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */

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
  * @brief  system clock config program
  * @note   the system clock is configured as follow:
  *         system clock (sclk)   = (hext * pll_ns)/(pll_ms * pll_fr)
  *         system clock source   = HEXT_VALUE
  *         - lick                = on
  *         - lext                = on
  *         - hick                = on
  *         - hext                = on
  *         - hext                = HEXT_VALUE
  *         - sclk                = 288000000
  *         - ahbdiv              = 1
  *         - ahbclk              = 288000000
  *         - apb1div             = 2
  *         - apb1clk             = 144000000
  *         - apb2div             = 2
  *         - apb2clk             = 144000000
  *         - pll_ns              = 144
  *         - pll_ms              = 1
  *         - pll_fr              = 4
  * @param  none
  * @retval none
  */
void wk_system_clock_config(void)
{
  /* reset crm */
  crm_reset();

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);

  /* config ldo voltage */
  pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
 
  /* set the flash clock divider */
  flash_clock_divider_set(FLASH_CLOCK_DIV_3);

  /* enable battery powered domain access */
  pwc_battery_powered_domain_access(TRUE);

  /* check lext enabled or not */
  if(crm_flag_get(CRM_LEXT_STABLE_FLAG) == RESET)
  {
    crm_clock_source_enable(CRM_CLOCK_SOURCE_LEXT, TRUE);
    while(crm_flag_get(CRM_LEXT_STABLE_FLAG) == RESET)
    {
    }
  }
  /* disable battery powered domain access */
  pwc_battery_powered_domain_access(FALSE);
  /* disable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, FALSE);

  /* enable lick */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_LICK, TRUE);

  /* wait till lick is ready */
  while(crm_flag_get(CRM_LICK_STABLE_FLAG) != SET)
  {
  }

  /* enable hext */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);

  /* wait till hext is ready */
  while(crm_hext_stable_wait() == ERROR)
  {
  }

  /* enable hick */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);

  /* wait till hick is ready */
  while(crm_flag_get(CRM_HICK_STABLE_FLAG) != SET)
  {
  }

  /* config pll clock resource */
  crm_pll_config(CRM_PLL_SOURCE_HEXT, 144, 1, CRM_PLL_FR_4);

  /* enable pll */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);

  /* wait till pll is ready */
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  /* config ahbclk */
  crm_ahb_div_set(CRM_AHB_DIV_1);

  /* config apb2clk, the maximum frequency of APB2 clock is 144 MHz  */
  crm_apb2_div_set(CRM_APB2_DIV_2);

  /* config apb1clk, the maximum frequency of APB1 clock is 144 MHz  */
  crm_apb1_div_set(CRM_APB1_DIV_2);

  /* enable auto step mode */
  crm_auto_step_mode_enable(TRUE);

  /* select pll as system clock source */
  crm_sysclk_switch(CRM_SCLK_PLL);

  /* wait till pll is used as system clock source */
  while(crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
  {
  }

  /* disable auto step mode */
  crm_auto_step_mode_enable(FALSE);

  /* update system_core_clock global variable */
  system_core_clock_update();
}

/**
  * @brief  config periph clock
  * @param  none
  * @retval none
  */
void wk_periph_clock_config(void)
{
  /* enable gpioa periph clock */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  /* enable gpiob periph clock */
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* enable gpioc periph clock */
  crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);

  /* enable gpiod periph clock */
  crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);

  /* enable gpioe periph clock */
  crm_periph_clock_enable(CRM_GPIOE_PERIPH_CLOCK, TRUE);

  /* enable gpioh periph clock */
  crm_periph_clock_enable(CRM_GPIOH_PERIPH_CLOCK, TRUE);

  /* enable crc periph clock */
  crm_periph_clock_enable(CRM_CRC_PERIPH_CLOCK, TRUE);

  /* enable dma1 periph clock */
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  /* enable dma2 periph clock */
  crm_periph_clock_enable(CRM_DMA2_PERIPH_CLOCK, TRUE);

  /* enable usb_otgfs2 periph clock */
  crm_periph_clock_enable(CRM_OTGFS2_PERIPH_CLOCK, TRUE);

  /* enable usb_otgfs1 periph clock */
  crm_periph_clock_enable(CRM_OTGFS1_PERIPH_CLOCK, TRUE);

  /* enable qspi2 periph clock */
  crm_periph_clock_enable(CRM_QSPI2_PERIPH_CLOCK, TRUE);

  /* enable tmr2 periph clock */
  crm_periph_clock_enable(CRM_TMR2_PERIPH_CLOCK, TRUE);

  /* enable tmr3 periph clock */
  crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);

  /* enable tmr4 periph clock */
  crm_periph_clock_enable(CRM_TMR4_PERIPH_CLOCK, TRUE);

  /* enable tmr5 periph clock */
  crm_periph_clock_enable(CRM_TMR5_PERIPH_CLOCK, TRUE);

  /* enable tmr6 periph clock */
  crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);

  /* enable tmr7 periph clock */
  crm_periph_clock_enable(CRM_TMR7_PERIPH_CLOCK, TRUE);

  /* enable usart2 periph clock */
  crm_periph_clock_enable(CRM_USART2_PERIPH_CLOCK, TRUE);

  /* enable usart3 periph clock */
  crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);

  /* enable uart4 periph clock */
  crm_periph_clock_enable(CRM_UART4_PERIPH_CLOCK, TRUE);

  /* enable uart5 periph clock */
  crm_periph_clock_enable(CRM_UART5_PERIPH_CLOCK, TRUE);

  /* enable can1 periph clock */
  crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, TRUE);

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);

  /* enable uart7 periph clock */
  crm_periph_clock_enable(CRM_UART7_PERIPH_CLOCK, TRUE);

  /* enable uart8 periph clock */
  crm_periph_clock_enable(CRM_UART8_PERIPH_CLOCK, TRUE);

  /* enable tmr8 periph clock */
  crm_periph_clock_enable(CRM_TMR8_PERIPH_CLOCK, TRUE);

  /* enable usart1 periph clock */
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* enable usart6 periph clock */
  crm_periph_clock_enable(CRM_USART6_PERIPH_CLOCK, TRUE);

  /* enable adc1 periph clock */
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);

  /* enable adc2 periph clock */
  crm_periph_clock_enable(CRM_ADC2_PERIPH_CLOCK, TRUE);

  /* enable scfg periph clock */
  crm_periph_clock_enable(CRM_SCFG_PERIPH_CLOCK, TRUE);

  /* enable acc periph clock */
  crm_periph_clock_enable(CRM_ACC_PERIPH_CLOCK, TRUE);
}

/**
  * @brief  nvic config
  * @param  none
  * @retval none
  */
void wk_nvic_config(void)
{
  nvic_priority_group_config(NVIC_PRIORITY_GROUP_4);

  NVIC_SetPriority(MemoryManagement_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(BusFault_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(UsageFault_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(SVCall_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(DebugMonitor_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
  nvic_irq_enable(TMR2_GLOBAL_IRQn, 5, 0);
  nvic_irq_enable(TMR3_GLOBAL_IRQn, 5, 0);
  nvic_irq_enable(TMR4_GLOBAL_IRQn, 5, 0);
  nvic_irq_enable(USART2_IRQn, 5, 0);
  nvic_irq_enable(USART3_IRQn, 5, 0);
  nvic_irq_enable(TMR8_OVF_TMR13_IRQn, 5, 0);
  nvic_irq_enable(TMR5_GLOBAL_IRQn, 0, 0);
  nvic_irq_enable(UART4_IRQn, 5, 0);
  nvic_irq_enable(UART5_IRQn, 5, 0);
  nvic_irq_enable(TMR6_DAC_GLOBAL_IRQn, 5, 0);
  nvic_irq_enable(TMR7_GLOBAL_IRQn, 5, 0);
  nvic_irq_enable(OTGFS1_IRQn, 5, 0);
  nvic_irq_enable(USART6_IRQn, 5, 0);
  nvic_irq_enable(OTGFS2_IRQn, 5, 0);
  nvic_irq_enable(UART7_IRQn, 5, 0);
  nvic_irq_enable(UART8_IRQn, 5, 0);
}

/**
  * @brief  init gpio_input/gpio_output/gpio_analog/eventout function.
  * @param  none
  * @retval none
  */
void wk_gpio_config(void)
{
  /* add user code begin gpio_config 0 */

  /* add user code end gpio_config 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin gpio_config 1 */

  /* add user code end gpio_config 1 */

  /* gpio input config */
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_6 | 
                               IN_SW1_PIN | IN_SW2_PIN | IN_SW3_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_12;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_7 | GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* gpio output config */
  gpio_bits_reset(GPIOE, GPIO_PINS_14 | GPIO_PINS_15);
  gpio_bits_reset(GPIOB, GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13);
  gpio_bits_reset(GPIOD, GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13 | GPIO_PINS_14 | 
                  GPIO_PINS_15);
  gpio_bits_reset(GPIOC, GPIO_PINS_8 | GPIO_PINS_9);
  gpio_bits_reset(GPIOA, GPIO_PINS_8 | GPIO_PINS_10 | CH_PIN);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_14 | GPIO_PINS_15;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13 | GPIO_PINS_14 | 
                               GPIO_PINS_15;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_8 | GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_8 | GPIO_PINS_10 | CH_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* add user code begin gpio_config 2 */

  /* add user code end gpio_config 2 */
}

/**
  * @brief  init exint function.
  * @param  none
  * @retval none
  */
void wk_exint_config(void)
{
  /* add user code begin exint_config 0 */

  /* add user code end exint_config 0 */

  gpio_init_type gpio_init_struct;
  exint_init_type exint_init_struct;

  /* add user code begin exint_config 1 */

  /* add user code end exint_config 1 */

  /* configure the EXINT4 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOD, SCFG_PINS_SOURCE4);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_4;
  exint_init_struct.line_polarity = EXINT_TRIGGER_RISING_EDGE;
  exint_init(&exint_init_struct);

  /* configure the EXINT7 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  scfg_exint_line_config(SCFG_PORT_SOURCE_GPIOD, SCFG_PINS_SOURCE7);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_7;
  exint_init_struct.line_polarity = EXINT_TRIGGER_RISING_EDGE;
  exint_init(&exint_init_struct);

  /* add user code begin exint_config 2 */

  /* add user code end exint_config 2 */
}

/**
  * @brief  init acc function
  * @param  none
  * @retval none
  */
void wk_acc_init(void)
{
  /* add user code begin acc_init 0 */

  /* add user code end acc_init 0 */

  /* update the c1\c2\c3 value */
  acc_write_c1(7980);
  acc_write_c2(8000);
  acc_write_c3(8020);

  /* SOF select */
  acc_sof_select(ACC_SOF_OTG2); 

  /* add user code begin acc_init 1 */

  /* add user code end acc_init 1 */

  /* open acc calibration */
  acc_calibration_mode_enable(ACC_CAL_HICKTRIM, TRUE);

  /* add user code begin acc_init 2 */

  /* add user code end acc_init 2 */
}

/**
  * @brief  init usart1 function
  * @param  none
  * @retval none
  */
void wk_usart1_init(void)
{
  /* add user code begin usart1_init 0 */

  /* add user code end usart1_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart1_init 1 */

  /* add user code end usart1_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure param */
  usart_init(USART1, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART1, TRUE);
  usart_parity_selection_config(USART1, USART_PARITY_NONE);

  usart_hardware_flow_control_set(USART1, USART_HARDWARE_FLOW_NONE);

  /* add user code begin usart1_init 2 */

  /* add user code end usart1_init 2 */

  usart_enable(USART1, TRUE);

  /* add user code begin usart1_init 3 */

  /* add user code end usart1_init 3 */
}

/**
  * @brief  init usart2 function
  * @param  none
  * @retval none
  */
void wk_usart2_init(void)
{
  /* add user code begin usart2_init 0 */

  /* add user code end usart2_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart2_init 1 */

  /* add user code end usart2_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE5, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE6, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure param */
  usart_init(USART2, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART2, TRUE);
  usart_receiver_enable(USART2, TRUE);
  usart_parity_selection_config(USART2, USART_PARITY_NONE);

  usart_dma_receiver_enable(USART2, TRUE);

  usart_hardware_flow_control_set(USART2, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(USART2, USART_IDLE_INT, TRUE);

  /* add user code begin usart2_init 2 */

  /* add user code end usart2_init 2 */

  usart_enable(USART2, TRUE);

  /* add user code begin usart2_init 3 */

  /* add user code end usart2_init 3 */
}

/**
  * @brief  init usart3 function
  * @param  none
  * @retval none
  */
void wk_usart3_init(void)
{
  /* add user code begin usart3_init 0 */

  /* add user code end usart3_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart3_init 1 */

  /* add user code end usart3_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE8, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_8;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE9, GPIO_MUX_7);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure param */
  usart_init(USART3, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART3, TRUE);
  usart_receiver_enable(USART3, TRUE);
  usart_parity_selection_config(USART3, USART_PARITY_NONE);

  usart_dma_receiver_enable(USART3, TRUE);

  usart_hardware_flow_control_set(USART3, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(USART3, USART_IDLE_INT, TRUE);

  /* add user code begin usart3_init 2 */

  /* add user code end usart3_init 2 */

  usart_enable(USART3, TRUE);

  /* add user code begin usart3_init 3 */

  /* add user code end usart3_init 3 */
}

/**
  * @brief  init uart4 function
  * @param  none
  * @retval none
  */
void wk_uart4_init(void)
{
  /* add user code begin uart4_init 0 */

  /* add user code end uart4_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin uart4_init 1 */

  /* add user code end uart4_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOC, GPIO_PINS_SOURCE10, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_10;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOC, GPIO_PINS_SOURCE11, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_11;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure param */
  usart_init(UART4, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART4, TRUE);
  usart_receiver_enable(UART4, TRUE);
  usart_parity_selection_config(UART4, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART4, TRUE);

  usart_hardware_flow_control_set(UART4, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(UART4, USART_IDLE_INT, TRUE);

  /* add user code begin uart4_init 2 */

  /* add user code end uart4_init 2 */

  usart_enable(UART4, TRUE);

  /* add user code begin uart4_init 3 */

  /* add user code end uart4_init 3 */
}

/**
  * @brief  init uart5 function
  * @param  none
  * @retval none
  */
void wk_uart5_init(void)
{
  /* add user code begin uart5_init 0 */

  /* add user code end uart5_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin uart5_init 1 */

  /* add user code end uart5_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE6, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure param */
  usart_init(UART5, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART5, TRUE);
  usart_receiver_enable(UART5, TRUE);
  usart_parity_selection_config(UART5, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART5, TRUE);

  usart_hardware_flow_control_set(UART5, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(UART5, USART_IDLE_INT, TRUE);

  /* add user code begin uart5_init 2 */

  /* add user code end uart5_init 2 */

  usart_enable(UART5, TRUE);

  /* add user code begin uart5_init 3 */

  /* add user code end uart5_init 3 */
}

/**
  * @brief  init usart6 function
  * @param  none
  * @retval none
  */
void wk_usart6_init(void)
{
  /* add user code begin usart6_init 0 */

  /* add user code end usart6_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin usart6_init 1 */

  /* add user code end usart6_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOC, GPIO_PINS_SOURCE6, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOC, GPIO_PINS_SOURCE7, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure param */
  usart_init(USART6, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART6, TRUE);
  usart_receiver_enable(USART6, TRUE);
  usart_parity_selection_config(USART6, USART_PARITY_NONE);

  usart_dma_receiver_enable(USART6, TRUE);

  usart_hardware_flow_control_set(USART6, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(USART6, USART_IDLE_INT, TRUE);

  /* add user code begin usart6_init 2 */

  /* add user code end usart6_init 2 */

  usart_enable(USART6, TRUE);

  /* add user code begin usart6_init 3 */

  /* add user code end usart6_init 3 */
}

/**
  * @brief  init uart7 function
  * @param  none
  * @retval none
  */
void wk_uart7_init(void)
{
  /* add user code begin uart7_init 0 */

  /* add user code end uart7_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin uart7_init 1 */

  /* add user code end uart7_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE4, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure param */
  usart_init(UART7, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART7, TRUE);
  usart_receiver_enable(UART7, TRUE);
  usart_parity_selection_config(UART7, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART7, TRUE);

  usart_hardware_flow_control_set(UART7, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(UART7, USART_IDLE_INT, TRUE);

  /* add user code begin uart7_init 2 */

  /* add user code end uart7_init 2 */

  usart_enable(UART7, TRUE);

  /* add user code begin uart7_init 3 */

  /* add user code end uart7_init 3 */
}

/**
  * @brief  init uart8 function
  * @param  none
  * @retval none
  */
void wk_uart8_init(void)
{
  /* add user code begin uart8_init 0 */

  /* add user code end uart8_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin uart8_init 1 */

  /* add user code end uart8_init 1 */

  /* configure the TX pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE1, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure the RX pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE0, GPIO_MUX_8);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure param */
  usart_init(UART8, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART8, TRUE);
  usart_receiver_enable(UART8, TRUE);
  usart_parity_selection_config(UART8, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART8, TRUE);

  usart_hardware_flow_control_set(UART8, USART_HARDWARE_FLOW_NONE);

  /* enable idle interrupt */
  usart_interrupt_enable(UART8, USART_IDLE_INT, TRUE);

  /* add user code begin uart8_init 2 */

  /* add user code end uart8_init 2 */

  usart_enable(UART8, TRUE);

  /* add user code begin uart8_init 3 */

  /* add user code end uart8_init 3 */
}

/**
  * @brief  init usb_otgfs1 function
  * @param  none
  * @retval none
  */
void wk_usb_otgfs1_init(void)
{
  /* add user code begin usb_otgfs1_init 0 */

  /* add user code end usb_otgfs1_init 0 */
  /* add user code begin usb_otgfs1_init 1 */

  /* add user code end usb_otgfs1_init 1 */

  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);

  /* add user code begin usb_otgfs1_init 2 */

  /* add user code end usb_otgfs1_init 2 */
}

/**
  * @brief  init usb_otgfs2 function
  * @param  none
  * @retval none
  */
void wk_usb_otgfs2_init(void)
{
  /* add user code begin usb_otgfs2_init 0 */

  /* add user code end usb_otgfs2_init 0 */
  /* add user code begin usb_otgfs2_init 1 */

  /* add user code end usb_otgfs2_init 1 */

  crm_usb_clock_source_select(CRM_USB_CLOCK_SOURCE_HICK);

  /* add user code begin usb_otgfs2_init 2 */

  /* add user code end usb_otgfs2_init 2 */
}

/**
  * @brief  init can1 function.
  * @param  none
  * @retval none
  */
void wk_can1_init(void)
{
  /* add user code begin can1_init 0 */

  /* add user code end can1_init 0 */
  
  gpio_init_type gpio_init_struct;
  can_base_type can_base_struct;
  can_baudrate_type can_baudrate_struct;
  can_filter_init_type can_filter_init_struct;

  /* add user code begin can1_init 1 */

  /* add user code end can1_init 1 */
  
  /*gpio-----------------------------------------------------------------------------*/ 
  gpio_default_para_init(&gpio_init_struct);

  /* configure the CAN1 TX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE1, GPIO_MUX_9);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure the CAN1 RX pin */
  gpio_pin_mux_config(GPIOD, GPIO_PINS_SOURCE0, GPIO_MUX_9);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /*can_base_init--------------------------------------------------------------------*/ 
  can_default_para_init(&can_base_struct);
  can_base_struct.mode_selection = CAN_MODE_COMMUNICATE;
  can_base_struct.ttc_enable = FALSE;
  can_base_struct.aebo_enable = TRUE;
  can_base_struct.aed_enable = TRUE;
  can_base_struct.prsf_enable = FALSE;
  can_base_struct.mdrsel_selection = CAN_DISCARDING_FIRST_RECEIVED;
  can_base_struct.mmssr_selection = CAN_SENDING_BY_ID;

  can_base_init(CAN1, &can_base_struct);

  /*can_baudrate_setting-------------------------------------------------------------*/ 
  /*set baudrate = pclk/(baudrate_div *(1 + bts1_size + bts2_size))------------------*/ 
  can_baudrate_struct.baudrate_div = 36;                       /*value: 1~0xFFF*/
  can_baudrate_struct.rsaw_size = CAN_RSAW_1TQ;                /*value: 1~4*/
  can_baudrate_struct.bts1_size = CAN_BTS1_6TQ;                /*value: 1~16*/
  can_baudrate_struct.bts2_size = CAN_BTS2_1TQ;                /*value: 1~8*/
  can_baudrate_set(CAN1, &can_baudrate_struct);

  /*can_filter_0_config--------------------------------------------------------------*/
  can_filter_init_struct.filter_activate_enable = TRUE;
  can_filter_init_struct.filter_number = 0;
  can_filter_init_struct.filter_fifo = CAN_FILTER_FIFO0;
  can_filter_init_struct.filter_bit = CAN_FILTER_16BIT;  
  can_filter_init_struct.filter_mode = CAN_FILTER_MODE_ID_MASK;  
  /*Standard identifier + Mask Mode + Data/Remote frame: id/mask 11bit --------------*/
  can_filter_init_struct.filter_id_high = 0x0 << 5;
  can_filter_init_struct.filter_id_low = 0x0 << 5;
  can_filter_init_struct.filter_mask_high = 0x0 << 5;
  can_filter_init_struct.filter_mask_low = 0x0 << 5;

  can_filter_init(CAN1, &can_filter_init_struct);

  /* add user code begin can1_init 2 */

  /* add user code end can1_init 2 */
}

/**
  * @brief  init adc-common function.
  * @param  none
  * @retval none
  */
void wk_adc_common_init(void)
{
  /* add user code begin adc_common_init 0 */

  /* add user code end adc_common_init 0 */

  adc_common_config_type adc_common_struct;

  /* add user code begin adc_common_init 1 */

  /* add user code end adc_common_init 1 */

  adc_reset();

  /* adc_common_settings------------------------------------------------------------ */
  adc_common_default_para_init(&adc_common_struct);
  adc_common_struct.combine_mode = ADC_INDEPENDENT_MODE;
  adc_common_struct.div = ADC_HCLK_DIV_4;
  adc_common_struct.common_dma_mode = ADC_COMMON_DMAMODE_DISABLE;
  adc_common_struct.common_dma_request_repeat_state = FALSE;
  adc_common_struct.sampling_interval = ADC_SAMPLING_INTERVAL_5CYCLES;
  adc_common_struct.tempervintrv_state = TRUE;
  adc_common_struct.vbat_state = TRUE;
  adc_common_config(&adc_common_struct);
  
  /* add user code begin adc_common_init 2 */

  /* add user code end adc_common_init 2 */
}

/**
  * @brief  init adc1 function.
  * @param  none
  * @retval none
  */
void wk_adc1_init(void)
{
  /* add user code begin adc1_init 0 */

  /* add user code end adc1_init 0 */

  gpio_init_type gpio_init_struct;
  adc_base_config_type adc_base_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin adc1_init 1 */

  /* add user code end adc1_init 1 */

  /*gpio--------------------------------------------------------------------*/ 
  /* configure the IN0 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN2 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN3 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN4 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN5 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN6 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN7 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN8 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the IN10 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN11 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN14 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN15 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init(GPIOC, &gpio_init_struct);

  /* adc_settings------------------------------------------------------------------- */
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = TRUE;
  adc_base_struct.repeat_mode = TRUE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 12;
  adc_base_config(ADC1, &adc_base_struct);

  adc_resolution_set(ADC1, ADC_RESOLUTION_12B);

  /* adc_ordinary_conversionmode---------------------------------------------------- */
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_0, 1, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_2, 2, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_3, 3, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_4, 4, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_5, 5, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_6, 6, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_7, 7, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_10, 8, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_11, 9, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_14, 10, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_15, 11, ADC_SAMPLETIME_247_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_8, 12, ADC_SAMPLETIME_247_5);

  /* When "ADC_ORDINARY_TRIG_EDGE_NONE" is selected, the external trigger source is invalid, and user can only use software trigger. \
  The software trigger function is adc_ordinary_software_trigger_enable(ADCx, TRUE); */
  adc_ordinary_conversion_trigger_set(ADC1,  ADC_ORDINARY_TRIG_TMR1TRGOUT, ADC_ORDINARY_TRIG_EDGE_NONE);

  /* add user code begin adc1_init 2 */

  /* add user code end adc1_init 2 */

  adc_enable(ADC1, TRUE);
  while(adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET);

  /* adc calibration---------------------------------------------------------------- */
  adc_calibration_init(ADC1);
  while(adc_calibration_init_status_get(ADC1));
  adc_calibration_start(ADC1);
  while(adc_calibration_status_get(ADC1));

  /* add user code begin adc1_init 3 */

  /* add user code end adc1_init 3 */
}

/**
  * @brief  init adc2 function.
  * @param  none
  * @retval none
  */
void wk_adc2_init(void)
{
  /* add user code begin adc2_init 0 */

  /* add user code end adc2_init 0 */

  gpio_init_type gpio_init_struct;
  adc_base_config_type adc_base_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin adc2_init 1 */

  /* add user code end adc2_init 1 */

  /*gpio--------------------------------------------------------------------*/ 
  /* configure the IN1 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN12 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init(GPIOC, &gpio_init_struct);

  /* adc_settings------------------------------------------------------------------- */
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = FALSE;
  adc_base_struct.repeat_mode = FALSE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 1;
  adc_base_config(ADC2, &adc_base_struct);

  adc_resolution_set(ADC2, ADC_RESOLUTION_12B);

  /* add user code begin adc2_init 2 */

  /* add user code end adc2_init 2 */

  adc_enable(ADC2, TRUE);
  while(adc_flag_get(ADC2, ADC_RDY_FLAG) == RESET);

  /* adc calibration---------------------------------------------------------------- */
  adc_calibration_init(ADC2);
  while(adc_calibration_init_status_get(ADC2));
  adc_calibration_start(ADC2);
  while(adc_calibration_status_get(ADC2));

  /* add user code begin adc2_init 3 */

  /* add user code end adc2_init 3 */
}

/**
  * @brief  init tmr2 function.
  * @param  none
  * @retval none
  */
void wk_tmr2_init(void)
{
  /* add user code begin tmr2_init 0 */

  /* add user code end tmr2_init 0 */


  /* add user code begin tmr2_init 1 */

  /* add user code end tmr2_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR2, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR2, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR2, FALSE);
  tmr_base_init(TMR2, 9999, 28799);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR2, FALSE);
  tmr_primary_mode_select(TMR2, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR2, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR2, TMR_OVF_INT, TRUE);

  /* add user code begin tmr2_init 2 */

  /* add user code end tmr2_init 2 */
}

/**
  * @brief  init tmr3 function.
  * @param  none
  * @retval none
  */
void wk_tmr3_init(void)
{
  /* add user code begin tmr3_init 0 */

  /* add user code end tmr3_init 0 */


  /* add user code begin tmr3_init 1 */

  /* add user code end tmr3_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR3, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR3, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR3, FALSE);
  tmr_base_init(TMR3, 9999, 28799);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR3, FALSE);
  tmr_primary_mode_select(TMR3, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR3, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR3, TMR_OVF_INT, TRUE);

  /* add user code begin tmr3_init 2 */

  /* add user code end tmr3_init 2 */
}

/**
  * @brief  init tmr4 function.
  * @param  none
  * @retval none
  */
void wk_tmr4_init(void)
{
  /* add user code begin tmr4_init 0 */

  /* add user code end tmr4_init 0 */


  /* add user code begin tmr4_init 1 */

  /* add user code end tmr4_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR4, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR4, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR4, FALSE);
  tmr_base_init(TMR4, 999, 287);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR4, FALSE);
  tmr_primary_mode_select(TMR4, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR4, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR4, TMR_OVF_INT, TRUE);

  /* add user code begin tmr4_init 2 */

  /* add user code end tmr4_init 2 */
}

/**
  * @brief  init tmr6 function.
  * @param  none
  * @retval none
  */
void wk_tmr6_init(void)
{
  /* add user code begin tmr6_init 0 */

  /* add user code end tmr6_init 0 */

  /* add user code begin tmr6_init 1 */

  /* add user code end tmr6_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR6, TMR_COUNT_UP);
  tmr_period_buffer_enable(TMR6, FALSE);
  tmr_base_init(TMR6, 999, 287);

  /* configure primary mode settings */
  tmr_primary_mode_select(TMR6, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR6, TRUE);

  /* add user code begin tmr6_init 2 */

  /* add user code end tmr6_init 2 */
}

/**
  * @brief  init tmr7 function.
  * @param  none
  * @retval none
  */
void wk_tmr7_init(void)
{
  /* add user code begin tmr7_init 0 */

  /* add user code end tmr7_init 0 */

  /* add user code begin tmr7_init 1 */

  /* add user code end tmr7_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR7, TMR_COUNT_UP);
  tmr_period_buffer_enable(TMR7, FALSE);
  tmr_base_init(TMR7, 999, 287);

  /* configure primary mode settings */
  tmr_primary_mode_select(TMR7, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR7, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR7, TMR_OVF_INT, TRUE);

  /* add user code begin tmr7_init 2 */

  /* add user code end tmr7_init 2 */
}

/**
  * @brief  init tmr8 function.
  * @param  none
  * @retval none
  */
void wk_tmr8_init(void)
{
  /* add user code begin tmr8_init 0 */

  /* add user code end tmr8_init 0 */


  /* add user code begin tmr8_init 1 */

  /* add user code end tmr8_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR8, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR8, TMR_CLOCK_DIV1);
  tmr_repetition_counter_set(TMR8, 0);
  tmr_period_buffer_enable(TMR8, FALSE);
  tmr_base_init(TMR8, 49999, 28799);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR8, FALSE);
  tmr_primary_mode_select(TMR8, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR8, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR8, TMR_OVF_INT, TRUE);

  /* add user code begin tmr8_init 2 */

  /* add user code end tmr8_init 2 */
}

/**
  * @brief  init ertc function.
  * @param  none
  * @retval none
  */
void wk_ertc_init(void)
{
  /* add user code begin ertc_init 0 */

  /* add user code end ertc_init 0 */

  /* add user code begin ertc_init 1 */

  /* add user code end ertc_init 1 */

  pwc_battery_powered_domain_access(TRUE);

  crm_ertc_clock_select(CRM_ERTC_CLOCK_LEXT);
  crm_ertc_clock_enable(TRUE);
  ertc_reset();
  ertc_wait_update();
  ertc_divider_set(127, 255);
  ertc_hour_mode_set(ERTC_HOUR_MODE_24);

  ertc_time_set(0, 0, 0, ERTC_24H);

  ertc_date_set(0, 1, 1, 1);

  /* add user code begin ertc_init 2 */

  /* add user code end ertc_init 2 */
}

/**
  * @brief  init qspi2 function
  * @param  none
  * @retval none
  */
void wk_qspi2_init(void)
{
  /* add user code begin qspi2_init 0 */

  /* add user code end qspi2_init 0 */

  gpio_init_type gpio_init_struct;
  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin qspi2_init 1 */

  /* add user code end qspi2_init 1 */

  /* configure the SCK pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE1, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the CS pin */
  gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE8, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_8;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the IO0 pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE7, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_7;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure the IO1 pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE8, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_8;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure the IO2 pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE9, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure the IO3 pin */
  gpio_pin_mux_config(GPIOE, GPIO_PINS_SOURCE10, GPIO_MUX_10);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_10;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

 /* configure param */
  qspi_xip_enable(QSPI2, FALSE);

  qspi_clk_division_set(QSPI2, QSPI_CLK_DIV_8);
  
  qspi_sck_mode_set(QSPI2, QSPI_SCK_MODE_0);

  qspi_busy_config(QSPI2, QSPI_BUSY_OFFSET_0);

  qspi_auto_ispc_enable(QSPI2);

  qspi_dma_rx_threshold_set(QSPI2, QSPI_DMA_FIFO_THOD_WORD08);

  qspi_dma_tx_threshold_set(QSPI2, QSPI_DMA_FIFO_THOD_WORD08);

  qspi_xip_enable(QSPI2, TRUE);

  /* add user code begin qspi2_init 2 */

  /* add user code end qspi2_init 2 */
}

/**
  * @brief  init crc function.
  * @param  none
  * @retval none
  */
void wk_crc_init(void)
{
  /* add user code begin crc_init 0 */

  /* add user code end crc_init 0 */

  crc_init_data_set(0x0000FFFF);
  crc_poly_size_set(CRC_POLY_SIZE_16B);
  crc_poly_value_set(0x8005);
  crc_reverse_input_data_set(CRC_REVERSE_INPUT_BY_BYTE);
  crc_reverse_output_data_set(CRC_REVERSE_OUTPUT_DATA);
  crc_data_reset();

  /* add user code begin crc_init 1 */

  /* add user code end crc_init 1 */
}

/**
  * @brief  init wdt function.
  * @param  none
  * @retval none
  */
void wk_wdt_init(void)
{
  /* add user code begin wdt_init 0 */

  /* add user code end wdt_init 0 */

  wdt_register_write_enable(TRUE);
  wdt_divider_set(WDT_CLK_DIV_64);
  wdt_reload_value_set(0xFFF);
  wdt_counter_reload();

  /* if enabled, please feed the dog through wdt_counter_reload() function */
  //wdt_enable();

  /* add user code begin wdt_init 1 */

  /* add user code end wdt_init 1 */
}

/**
  * @brief  init dma1 channel1 for "usart2_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel1_init(void)
{
  /* add user code begin dma1_channel1 0 */

  /* add user code end dma1_channel1 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL1);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL1, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL1, DMAMUX_DMAREQ_ID_USART2_RX);

  /* add user code begin dma1_channel1 1 */

  /* add user code end dma1_channel1 1 */
}

/**
  * @brief  init dma1 channel2 for "usart3_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel2_init(void)
{
  /* add user code begin dma1_channel2 0 */

  /* add user code end dma1_channel2 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL2);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL2, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL2, DMAMUX_DMAREQ_ID_USART3_RX);

  /* add user code begin dma1_channel2 1 */

  /* add user code end dma1_channel2 1 */
}

/**
  * @brief  init dma1 channel3 for "uart4_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel3_init(void)
{
  /* add user code begin dma1_channel3 0 */

  /* add user code end dma1_channel3 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL3);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL3, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL3, DMAMUX_DMAREQ_ID_UART4_RX);

  /* add user code begin dma1_channel3 1 */

  /* add user code end dma1_channel3 1 */
}

/**
  * @brief  init dma1 channel4 for "uart5_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel4_init(void)
{
  /* add user code begin dma1_channel4 0 */

  /* add user code end dma1_channel4 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL4);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL4, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL4, DMAMUX_DMAREQ_ID_UART5_RX);

  /* add user code begin dma1_channel4 1 */

  /* add user code end dma1_channel4 1 */
}

/**
  * @brief  init dma1 channel5 for "usart6_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel5_init(void)
{
  /* add user code begin dma1_channel5 0 */

  /* add user code end dma1_channel5 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL5);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL5, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL5, DMAMUX_DMAREQ_ID_USART6_RX);

  /* add user code begin dma1_channel5 1 */

  /* add user code end dma1_channel5 1 */
}

/**
  * @brief  init dma1 channel6 for "uart7_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel6_init(void)
{
  /* add user code begin dma1_channel6 0 */

  /* add user code end dma1_channel6 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL6);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL6, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL6, DMAMUX_DMAREQ_ID_UART7_RX);

  /* add user code begin dma1_channel6 1 */

  /* add user code end dma1_channel6 1 */
}

/**
  * @brief  init dma1 channel7 for "uart8_rx"
  * @param  none
  * @retval none
  */
void wk_dma1_channel7_init(void)
{
  /* add user code begin dma1_channel7 0 */

  /* add user code end dma1_channel7 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA1_CHANNEL7);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA1_CHANNEL7, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA1, TRUE);
  dmamux_init(DMA1MUX_CHANNEL7, DMAMUX_DMAREQ_ID_UART8_RX);

  /* add user code begin dma1_channel7 1 */

  /* add user code end dma1_channel7 1 */
}

/**
  * @brief  init dma2 channel1 for "adc1"
  * @param  none
  * @retval none
  */
void wk_dma2_channel1_init(void)
{
  /* add user code begin dma2_channel1 0 */

  /* add user code end dma2_channel1 0 */

  dma_init_type dma_init_struct;

  dma_reset(DMA2_CHANNEL1);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = TRUE;
  dma_init(DMA2_CHANNEL1, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA2, TRUE);
  dmamux_init(DMA2MUX_CHANNEL1, DMAMUX_DMAREQ_ID_ADC1);

  /* add user code begin dma2_channel1 1 */

  /* add user code end dma2_channel1 1 */
}

/**
  * @brief  init dma2 channel2 for "adc2"
  * @param  none
  * @retval none
  */
void wk_dma2_channel2_init(void)
{
  /* add user code begin dma2_channel2 0 */

  /* add user code end dma2_channel2 0 */
  
  dma_init_type dma_init_struct;

  dma_reset(DMA2_CHANNEL2);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = TRUE;
  dma_init(DMA2_CHANNEL2, &dma_init_struct);

  /* dmamux function enable */
  dmamux_enable(DMA2, TRUE);
  dmamux_init(DMA2MUX_CHANNEL2, DMAMUX_DMAREQ_ID_ADC2);

  /* add user code begin dma2_channel2 1 */

  /* add user code end dma2_channel2 1 */
}

/**
  * @brief  config dma channel transfer parameter
  * @param  dmax_channely: DMAx_CHANNELy
  * @param  peripheral_base_addr: peripheral address.
  * @param  memory_base_addr: memory address.
  * @param  buffer_size: buffer size.
  * @retval none
  */
void wk_dma_channel_config(dma_channel_type* dmax_channely, uint32_t peripheral_base_addr, uint32_t memory_base_addr, uint16_t buffer_size)
{
  /* add user code begin dma_channel_config 0 */

  /* add user code end dma_channel_config 0 */

  dmax_channely->dtcnt = buffer_size;
  dmax_channely->paddr = peripheral_base_addr;
  dmax_channely->maddr = memory_base_addr;

  /* add user code begin dma_channel_config 1 */

  /* add user code end dma_channel_config 1 */
}

/* add user code begin 1 */

/* add user code end 1 */

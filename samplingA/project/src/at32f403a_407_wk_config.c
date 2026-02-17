/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f403a_407_wk_config.c
  * @brief    work bench config program
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

#include "at32f403a_407_wk_config.h"

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
  *         system clock (sclk)   = hext * pll_mult
  *         system clock source   = HEXT_VALUE
  *         - hext                = HEXT_VALUE
  *         - sclk                = 240000000
  *         - ahbdiv              = 1
  *         - ahbclk              = 240000000
  *         - apb1div             = 2
  *         - apb1clk             = 120000000
  *         - apb2div             = 2
  *         - apb2clk             = 120000000
  *         - pll_mult            = 30
  *         - pll_range           = GT72MHZ (greater than 72 mhz)
  * @param  none
  * @retval none
  */
void wk_system_clock_config(void)
{
  /* reset crm */
  crm_reset();

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
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
  crm_pll_config(CRM_PLL_SOURCE_HEXT, CRM_PLL_MULT_30, CRM_PLL_OUTPUT_RANGE_GT72MHZ);

  /* enable pll */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);

  /* wait till pll is ready */
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  /* config ahbclk */
  crm_ahb_div_set(CRM_AHB_DIV_1);

  /* config apb2clk, the maximum frequency of APB2 clock is 120 MHz  */
  crm_apb2_div_set(CRM_APB2_DIV_2);

  /* config apb1clk, the maximum frequency of APB1 clock is 120 MHz  */
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
  /* enable dma1 periph clock */
  crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK, TRUE);

  /* enable dma2 periph clock */
  crm_periph_clock_enable(CRM_DMA2_PERIPH_CLOCK, TRUE);

  /* enable crc periph clock */
  crm_periph_clock_enable(CRM_CRC_PERIPH_CLOCK, TRUE);

  /* enable iomux periph clock */
  crm_periph_clock_enable(CRM_IOMUX_PERIPH_CLOCK, TRUE);

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

  /* enable adc1 periph clock */
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);

  /* enable tmr1 periph clock */
  crm_periph_clock_enable(CRM_TMR1_PERIPH_CLOCK, TRUE);

  /* enable usart1 periph clock */
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* enable usart6 periph clock */
  crm_periph_clock_enable(CRM_USART6_PERIPH_CLOCK, TRUE);

  /* enable uart7 periph clock */
  crm_periph_clock_enable(CRM_UART7_PERIPH_CLOCK, TRUE);

  /* enable uart8 periph clock */
  crm_periph_clock_enable(CRM_UART8_PERIPH_CLOCK, TRUE);

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

  /* enable tmr14 periph clock */
  crm_periph_clock_enable(CRM_TMR14_PERIPH_CLOCK, TRUE);

  /* enable spi2 periph clock */
  crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);

  /* enable usart2 periph clock */
  crm_periph_clock_enable(CRM_USART2_PERIPH_CLOCK, TRUE);

  /* enable usart3 periph clock */
  crm_periph_clock_enable(CRM_USART3_PERIPH_CLOCK, TRUE);

  /* enable uart4 periph clock */
  crm_periph_clock_enable(CRM_UART4_PERIPH_CLOCK, TRUE);

  /* enable uart5 periph clock */
  crm_periph_clock_enable(CRM_UART5_PERIPH_CLOCK, TRUE);

  /* enable bpr periph clock */
  crm_periph_clock_enable(CRM_BPR_PERIPH_CLOCK, TRUE);

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
}

/**
  * @brief  init debug function.
  * @param  none
  * @retval none
  */
void wk_debug_config(void)
{
  /* jtag-dp disabled and sw-dp enabled */
  gpio_pin_remap_config(SWJTAG_GMUX_010, TRUE);
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
  nvic_irq_enable(EXINT2_IRQn, 3, 0);
  nvic_irq_enable(EXINT3_IRQn, 3, 0);
  nvic_irq_enable(TMR2_GLOBAL_IRQn, 3, 0);
  nvic_irq_enable(TMR3_GLOBAL_IRQn, 3, 0);
  nvic_irq_enable(TMR4_GLOBAL_IRQn, 3, 0);
  nvic_irq_enable(USART2_IRQn, 3, 0);
  nvic_irq_enable(USART3_IRQn, 3, 0);
  nvic_irq_enable(TMR8_TRG_HALL_TMR14_IRQn, 3, 0);
  nvic_irq_enable(TMR5_GLOBAL_IRQn, 3, 0);
  nvic_irq_enable(UART4_IRQn, 3, 0);
  nvic_irq_enable(UART5_IRQn, 3, 0);
  nvic_irq_enable(TMR6_GLOBAL_IRQn, 3, 0);
  nvic_irq_enable(DMA2_Channel2_IRQn, 3, 0);
  nvic_irq_enable(USART6_IRQn, 3, 0);
  nvic_irq_enable(UART7_IRQn, 3, 0);
  nvic_irq_enable(UART8_IRQn, 3, 0);
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
  gpio_init_struct.gpio_pins = GPIO_PINS_2 | GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5 | GPIO_PINS_6 | GPIO_PINS_12;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2| GPIO_PINS_3| GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_5 | GPIO_PINS_6 | GPIO_PINS_7;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* gpio output config */
  gpio_bits_set(GPIOC, GPIO_PINS_5 | CH_PIN);
  gpio_bits_set(GPIOB, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 | GPIO_PINS_10 | GPIO_PINS_11);
  gpio_bits_set(GPIOE, GPIO_PINS_7 | GPIO_PINS_10 | GPIO_PINS_11  | GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15);
	gpio_bits_reset(GPIOE, DIR_PIN);
  gpio_bits_set(SPI2_CS_GPIO_PORT, SPI2_CS_PIN);
  gpio_bits_set(GPIOD, GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13 | GPIO_PINS_14);
	gpio_bits_reset(GPIOD,GPIO_PINS_13 );

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_5 | CH_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 | GPIO_PINS_10 | GPIO_PINS_11 | SPI2_CS_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_7 | DIR_PIN | GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12 | GPIO_PINS_13 | GPIO_PINS_14;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

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

  /* configure the EXINT2 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_exint_line_config(GPIO_PORT_SOURCE_GPIOD, GPIO_PINS_SOURCE2);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_2;
  exint_init_struct.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  exint_init(&exint_init_struct);

  /**
   * Users need to configure EXINT2 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding EXINT2 interrupt.
   *     --exint_interrupt_enable(EXINT_LINE_2, TRUE);
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void EXINT2_IRQHandler(void)
   */

  /* configure the EXINT3 */
  gpio_default_para_init(&gpio_init_struct);
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_exint_line_config(GPIO_PORT_SOURCE_GPIOD, GPIO_PINS_SOURCE3);

  exint_default_para_init(&exint_init_struct);
  exint_init_struct.line_enable = TRUE;
  exint_init_struct.line_mode = EXINT_LINE_INTERRUPUT;
  exint_init_struct.line_select = EXINT_LINE_3;
  exint_init_struct.line_polarity = EXINT_TRIGGER_FALLING_EDGE;
  exint_init(&exint_init_struct);

  /**
   * Users need to configure EXINT3 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding EXINT3 interrupt.
   *     --exint_interrupt_enable(EXINT_LINE_3, TRUE);
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void EXINT3_IRQHandler(void)
   */

  /* add user code begin exint_config 2 */

  /* add user code end exint_config 2 */

  /* add user code begin exint_config 3 */

  /* add user code end exint_config 3 */
}

/**
  * @brief  init rtc function.
  * @param  none
  * @retval none
  */
void wk_rtc_init(void)
{
  /* add user code begin rtc_init 0 */
  
  /* add user code end rtc_init 0 */

  calendar_type time_struct;

  /* add user code begin rtc_init 1 */

  /* add user code end rtc_init 1 */

  pwc_battery_powered_domain_access(TRUE);
  
  /* 检查RTC是否已经初始化（通过备份寄存器BPR_DATA1检查魔数） */
  if(bpr_data_read(BPR_DATA1) != 0x1234)
  {
    /* RTC未初始化，第一次上电或RTC电池失效 */
    crm_rtc_clock_select(CRM_RTC_CLOCK_LEXT);
    crm_rtc_clock_enable(TRUE);
    rtc_wait_update_finish();
    rtc_wait_config_finish();
    rtc_divider_set(32767);
    rtc_wait_config_finish();

    /* 只在第一次初始化时设置默认时间 */
    time_struct.year  = 2025;
    time_struct.month = 1;
    time_struct.date  = 1;
    time_struct.hour  = 0;
    time_struct.min   = 0;
    time_struct.sec   = 0;
    rtc_time_set(&time_struct);

    /* 写入魔数标记RTC已初始化 */
    bpr_data_write(BPR_DATA1, 0x1234);
  }
  else
  {
    /* RTC已初始化，电池有效，保持当前时间 */
    rtc_wait_update_finish();
    rtc_wait_config_finish();
  }

  bpr_rtc_output_select(BPR_RTC_OUTPUT_NONE);

  /* add user code begin rtc_init 2 */

  /* add user code end rtc_init 2 */
}

/**
  * @brief  init spi2 function
  * @param  none
  * @retval none
  */
void wk_spi2_init(void)
{
  /* add user code begin spi2_init 0 */

  /* add user code end spi2_init 0 */

  gpio_init_type gpio_init_struct;
  spi_init_type spi_init_struct;

  gpio_default_para_init(&gpio_init_struct);
  spi_default_para_init(&spi_init_struct);

  /* add user code begin spi2_init 1 */

  /* add user code end spi2_init 1 */

  /* configure the SCK pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_13;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the MISO pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_14;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the MOSI pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_15;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure param */
  spi_init_struct.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
  spi_init_struct.master_slave_mode = SPI_MODE_MASTER;
  spi_init_struct.frame_bit_num = SPI_FRAME_8BIT;
  spi_init_struct.first_bit_transmission = SPI_FIRST_BIT_MSB;
  spi_init_struct.mclk_freq_division = SPI_MCLK_DIV_32;
  spi_init_struct.clock_polarity = SPI_CLOCK_POLARITY_HIGH;
  spi_init_struct.clock_phase = SPI_CLOCK_PHASE_2EDGE;
  spi_init_struct.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
  spi_init(SPI2, &spi_init_struct);

  /* add user code begin spi2_init 2 */

  /* add user code end spi2_init 2 */

  spi_enable(SPI2, TRUE);

  /* add user code begin spi2_init 3 */

  /* add user code end spi2_init 3 */
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_5;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_pin_remap_config(USART2_GMUX_0001, TRUE);

  /* configure param */
  usart_init(USART2, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART2, TRUE);
  usart_receiver_enable(USART2, TRUE);
  usart_parity_selection_config(USART2, USART_PARITY_NONE);

  usart_dma_receiver_enable(USART2, TRUE);

  usart_hardware_flow_control_set(USART2, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure USART2 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding USART2 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void USART2_IRQHandler(void)
   */

  /* add user code begin usart2_init 2 */
	usart_interrupt_enable(USART2, USART_IDLE_INT, TRUE);
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_8;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOD, &gpio_init_struct);

  gpio_pin_remap_config(USART3_GMUX_0011, TRUE);

  /* configure param */
  usart_init(USART3, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(USART3, TRUE);
  usart_receiver_enable(USART3, TRUE);
  usart_parity_selection_config(USART3, USART_PARITY_NONE);

  usart_dma_receiver_enable(USART3, TRUE);

  usart_hardware_flow_control_set(USART3, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure USART3 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding USART3 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void USART3_IRQHandler(void)
   */

  /* add user code begin usart3_init 2 */
	usart_interrupt_enable(USART3, USART_IDLE_INT, TRUE);
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_10;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_11;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure param */
  usart_init(UART4, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART4, TRUE);
  usart_receiver_enable(UART4, TRUE);
  usart_parity_selection_config(UART4, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART4, TRUE);

  usart_hardware_flow_control_set(UART4, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure UART4 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding UART4 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void UART4_IRQHandler(void)
   */

  /* add user code begin uart4_init 2 */
	usart_interrupt_enable(UART4, USART_IDLE_INT, TRUE);
  /* add user code end uart4_init 2 */

  usart_enable(UART4, TRUE);
//	usart_enable(UART4, FALSE);

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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_8;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  gpio_pin_remap_config(UART5_GMUX_0001, TRUE);

  /* configure param */
  usart_init(UART5, 19200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART5, TRUE);
  usart_receiver_enable(UART5, TRUE);
  usart_parity_selection_config(UART5, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART5, TRUE);

  usart_hardware_flow_control_set(UART5, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure UART5 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding UART5 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void UART5_IRQHandler(void)
   */

  /* add user code begin uart5_init 2 */
	usart_interrupt_enable(UART5, USART_IDLE_INT, TRUE);
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_6;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
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

  /**
   * Users need to configure USART6 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding USART6 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void USART6_IRQHandler(void)
   */

  /* add user code begin usart6_init 2 */
	usart_interrupt_enable(USART6, USART_IDLE_INT, TRUE);
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_4;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  gpio_pin_remap_config(UART7_GMUX, TRUE);

  /* configure param */
  usart_init(UART7, 9600, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART7, TRUE);
  usart_receiver_enable(UART7, TRUE);
  usart_parity_selection_config(UART7, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART7, TRUE);

  usart_hardware_flow_control_set(UART7, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure UART7 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding UART7 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void UART7_IRQHandler(void)
   */

  /* add user code begin uart7_init 2 */
	usart_interrupt_enable(UART7, USART_IDLE_INT, TRUE);
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
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure the RX pin */
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type  = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_INPUT;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* configure param */
  usart_init(UART8, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
  usart_transmitter_enable(UART8, TRUE);
  usart_receiver_enable(UART8, TRUE);
  usart_parity_selection_config(UART8, USART_PARITY_NONE);

  usart_dma_receiver_enable(UART8, TRUE);

  usart_hardware_flow_control_set(UART8, USART_HARDWARE_FLOW_NONE);

  /**
   * Users need to configure UART8 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding UART8 interrupt.
   *     --usart_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void UART8_IRQHandler(void)
   */

  /* add user code begin uart8_init 2 */
	usart_interrupt_enable(UART8, USART_IDLE_INT, TRUE);
  /* add user code end uart8_init 2 */

  usart_enable(UART8, TRUE);

  /* add user code begin uart8_init 3 */

  /* add user code end uart8_init 3 */
}

/**
  * @brief  init tmr1 function.
  * @param  none
  * @retval none
  */
void wk_tmr1_init(void)
{
  /* add user code begin tmr1_init 0 */

  /* add user code end tmr1_init 0 */

  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;
  tmr_brkdt_config_type tmr_brkdt_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin tmr1_init 1 */

  /* add user code end tmr1_init 1 */

  /* configure the CH1 pin */
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(GPIOE, &gpio_init_struct);

  /* GPIO PIN remap */
  gpio_pin_remap_config(TMR1_GMUX_0011, TRUE); 

  /* configure counter settings */
  tmr_cnt_dir_set(TMR1, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR1, TMR_CLOCK_DIV1);
  tmr_repetition_counter_set(TMR1, 50);
  tmr_period_buffer_enable(TMR1, TRUE);
  tmr_base_init(TMR1, 99, 239);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR1, FALSE);
  tmr_primary_mode_select(TMR1, TMR_PRIMARY_SEL_RESET);

  /* configure channel 1 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR1, TMR_SELECT_CHANNEL_1, &tmr_output_struct);
  tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, 0);
  tmr_output_channel_buffer_enable(TMR1, TMR_SELECT_CHANNEL_1, TRUE);

  tmr_output_channel_immediately_set(TMR1, TMR_SELECT_CHANNEL_1, FALSE);

  /* configure break and dead-time settings */
  tmr_brkdt_struct.brk_enable = FALSE;
  tmr_brkdt_struct.auto_output_enable = FALSE;
  tmr_brkdt_struct.brk_polarity = TMR_BRK_INPUT_ACTIVE_LOW;
  tmr_brkdt_struct.fcsoen_state = FALSE;
  tmr_brkdt_struct.fcsodis_state = FALSE;
  tmr_brkdt_struct.wp_level = TMR_WP_OFF;
  tmr_brkdt_struct.deadtime = 0;
  tmr_brkdt_config(TMR1, &tmr_brkdt_struct);


  tmr_output_enable(TMR1, TRUE);

  tmr_counter_enable(TMR1, TRUE);

  /* add user code begin tmr1_init 2 */

  /* add user code end tmr1_init 2 */
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
  tmr_base_init(TMR2, 9999, 23999);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR2, FALSE);
  tmr_primary_mode_select(TMR2, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR2, TRUE);

  /**
   * Users need to configure TMR2 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding TMR2 interrupt.
   *     --tmr_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void TMR2_GLOBAL_IRQHandler(void)
   */

  /* add user code begin tmr2_init 2 */
	tmr_interrupt_enable(TMR2, TMR_OVF_INT, TRUE);
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
  tmr_base_init(TMR3, 999, 239);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR3, FALSE);
  tmr_primary_mode_select(TMR3, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR3, TRUE);

  /**
   * Users need to configure TMR3 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding TMR3 interrupt.
   *     --tmr_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void TMR3_GLOBAL_IRQHandler(void)
   */

  /* add user code begin tmr3_init 2 */
	tmr_interrupt_enable(TMR3, TMR_OVF_INT, TRUE);
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
  tmr_base_init(TMR4, 49999, 23999);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR4, FALSE);
  tmr_primary_mode_select(TMR4, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR4, TRUE);

  /**
   * Users need to configure TMR4 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding TMR4 interrupt.
   *     --tmr_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void TMR4_GLOBAL_IRQHandler(void)
   */

  /* add user code begin tmr4_init 2 */
	tmr_interrupt_enable(TMR4, TMR_OVF_INT, TRUE);
  /* add user code end tmr4_init 2 */
}

/**
  * @brief  init tmr5 function.
  * @param  none
  * @retval none
  */
void wk_tmr5_init(void)
{
  /* add user code begin tmr5_init 0 */

  /* add user code end tmr5_init 0 */


  /* add user code begin tmr5_init 1 */

  /* add user code end tmr5_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR5, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR5, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR5, FALSE);
  tmr_base_init(TMR5, 4999, 239);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR5, FALSE);
  tmr_primary_mode_select(TMR5, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR5, TRUE);

  /**
   * Users need to configure TMR5 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding TMR5 interrupt.
   *     --tmr_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void TMR5_GLOBAL_IRQHandler(void)
   */

  /* add user code begin tmr5_init 2 */
	tmr_interrupt_enable(TMR5, TMR_OVF_INT, TRUE);
  /* add user code end tmr5_init 2 */
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
  tmr_base_init(TMR6, 9999, 23999);

  /* configure primary mode settings */
  tmr_primary_mode_select(TMR6, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR6, TRUE);

  /**
   * Users need to configure TMR6 interrupt functions according to the actual application.
   * 1. Call the below function to enable the corresponding TMR6 interrupt.
   *     --tmr_interrupt_enable(...)
   * 2. Add the user's interrupt handler code into the below function in the at32f403a_407_int.c file.
   *     --void TMR6_GLOBAL_IRQHandler(void)
   */

  /* add user code begin tmr6_init 2 */
	tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);
  /* add user code end tmr6_init 2 */
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
  /* configure the IN10 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN11 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN12 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_2;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN13 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_3;
  gpio_init(GPIOC, &gpio_init_struct);

  /* configure the IN0 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_0;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the IN1 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = GPIO_PINS_1;
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

  adc_reset(ADC1);
  crm_adc_clock_div_set(CRM_ADC_DIV_6);

  /*adc_common_settings-------------------------------------------------------------*/ 
  adc_combine_mode_select(ADC_INDEPENDENT_MODE);

  /*adc_settings--------------------------------------------------------------------*/ 
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = TRUE;
  adc_base_struct.repeat_mode = TRUE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 9;
  adc_base_config(ADC1, &adc_base_struct);

  /* adc_ordinary_conversionmode-------------------------------------------- */
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_10, 4, ADC_SAMPLETIME_239_5);  //物理连接PC0
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_11, 3, ADC_SAMPLETIME_239_5);  //物理连接PC1
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_12, 2, ADC_SAMPLETIME_239_5);  //物理连接PC2
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_13, 1, ADC_SAMPLETIME_239_5);  //物理连接PC3
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_0, 5, ADC_SAMPLETIME_239_5);   //物理连接PA0
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_1, 6, ADC_SAMPLETIME_239_5);//物理连接PA1
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_2, 7, ADC_SAMPLETIME_239_5);//物理连接PA2
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_3, 8, ADC_SAMPLETIME_239_5);//物理连接PA3
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_4, 9, ADC_SAMPLETIME_239_5);//物理连接PA4

  adc_ordinary_conversion_trigger_set(ADC1, ADC12_ORDINARY_TRIG_SOFTWARE, TRUE);

  adc_ordinary_part_mode_enable(ADC1, FALSE);

  adc_dma_mode_enable(ADC1, TRUE);
  /* add user code begin adc1_init 2 */

  /* add user code end adc1_init 2 */
  
  adc_enable(ADC1, TRUE);
  
  /* adc calibration-------------------------------------------------------- */
  adc_calibration_init(ADC1);
  while(adc_calibration_init_status_get(ADC1));
  adc_calibration_start(ADC1);
  while(adc_calibration_status_get(ADC1));

}

/**
  * @brief  init dma1 channel1 for "adc1"
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
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_HALFWORD;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = TRUE;
  dma_init(DMA1_CHANNEL1, &dma_init_struct);

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL1, DMA_FLEXIBLE_ADC1);
  /* add user code begin dma1_channel1 1 */

  /* add user code end dma1_channel1 1 */
}

/**
  * @brief  init dma1 channel2 for "usart2_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL2, DMA_FLEXIBLE_UART2_RX);
  /* add user code begin dma1_channel2 1 */

  /* add user code end dma1_channel2 1 */
}

/**
  * @brief  init dma1 channel3 for "usart3_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL3, DMA_FLEXIBLE_UART3_RX);
  /* add user code begin dma1_channel3 1 */

  /* add user code end dma1_channel3 1 */
}

/**
  * @brief  init dma1 channel4 for "uart4_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL4, DMA_FLEXIBLE_UART4_RX);
  /* add user code begin dma1_channel4 1 */

  /* add user code end dma1_channel4 1 */
}

/**
  * @brief  init dma1 channel5 for "uart5_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL5, DMA_FLEXIBLE_UART5_RX);
  /* add user code begin dma1_channel5 1 */

  /* add user code end dma1_channel5 1 */
}

/**
  * @brief  init dma1 channel6 for "usart6_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL6, DMA_FLEXIBLE_UART6_RX);
  /* add user code begin dma1_channel6 1 */

  /* add user code end dma1_channel6 1 */
}

/**
  * @brief  init dma1 channel7 for "uart7_rx"
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

  /* flexible function enable */
  dma_flexible_config(DMA1, FLEX_CHANNEL7, DMA_FLEXIBLE_UART7_RX);
  /* add user code begin dma1_channel7 1 */

  /* add user code end dma1_channel7 1 */
}

/**
  * @brief  init dma2 channel1 for "uart8_rx"
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
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_LOW;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA2_CHANNEL1, &dma_init_struct);

  /* flexible function enable */
  dma_flexible_config(DMA2, FLEX_CHANNEL1, DMA_FLEXIBLE_UART8_RX);
  /* add user code begin dma2_channel1 1 */

  /* add user code end dma2_channel1 1 */
}


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

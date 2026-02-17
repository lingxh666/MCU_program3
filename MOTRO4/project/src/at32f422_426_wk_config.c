/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f422_426_wk_config.c
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

#include "at32f422_426_wk_config.h"

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
  *         - sclk                = 160000000
  *         - ahbdiv              = 1
  *         - ahbclk              = 160000000
  *         - apb1div             = 1
  *         - apb1clk             = 160000000
  *         - apb2div             = 1
  *         - apb2clk             = 160000000
  *         - apb3div             = 2
  *         - apb3clk             = 80000000
  *         - pll_mult            = 20
  *         - flash_wtcyc         = 4 cycle
  * @param  none
  * @retval none
  */
void wk_system_clock_config(void)
{
  /* reset crm */
  crm_reset();

  /* config flash psr register */
  flash_psr_set(FLASH_WAIT_CYCLE_4);

  /* enable pwc periph clock */
  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);

  /* config ldo voltage */
  pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);

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
  crm_pll_config(CRM_PLL_SOURCE_HEXT, CRM_PLL_MULT_20);

  /* enable pll */
  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);

  /* wait till pll is ready */
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  /* config ahbclk */
  crm_ahb_div_set(CRM_AHB_DIV_1);

  /* config apb3clk, the maximum frequency of APB3 clock is 90 MHz  */
  crm_apb3_div_set(CRM_APB3_DIV_2);

  /* config apb2clk, the maximum frequency of APB1/APB2 clock is 180 MHz  */
  crm_apb2_div_set(CRM_APB2_DIV_1);

  /* config apb1clk, the maximum frequency of APB1/APB2 clock is 180 MHz  */
  crm_apb1_div_set(CRM_APB1_DIV_1);

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

  /* enable gpioa periph clock */
  crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);

  /* enable gpiob periph clock */
  crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);

  /* enable gpiof periph clock */
  crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);

  /* enable scfg periph clock */
  crm_periph_clock_enable(CRM_SCFG_PERIPH_CLOCK, TRUE);

  /* enable adc1 periph clock */
  crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);

  /* enable usart1 periph clock */
  crm_periph_clock_enable(CRM_USART1_PERIPH_CLOCK, TRUE);

  /* enable tmr15 periph clock */
  crm_periph_clock_enable(CRM_TMR15_PERIPH_CLOCK, TRUE);

  /* enable tmr16 periph clock */
  crm_periph_clock_enable(CRM_TMR16_PERIPH_CLOCK, TRUE);

  /* enable tmr17 periph clock */
  crm_periph_clock_enable(CRM_TMR17_PERIPH_CLOCK, TRUE);

  /* enable tmr3 periph clock */
  crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK, TRUE);

  /* enable tmr6 periph clock */
  crm_periph_clock_enable(CRM_TMR6_PERIPH_CLOCK, TRUE);

  /* enable tmr4 periph clock */
  crm_periph_clock_enable(CRM_TMR4_PERIPH_CLOCK, TRUE);

  /* enable can1 periph clock */
  crm_periph_clock_enable(CRM_CAN1_PERIPH_CLOCK, TRUE);
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
  NVIC_SetPriority(PendSV_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 0, 0));
  NVIC_SetPriority(SysTick_IRQn, NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 15, 0));
  nvic_irq_enable(DMA1_Channel1_IRQn, 0, 0);
  nvic_irq_enable(TMR6_GLOBAL_IRQn, 0, 0);
  nvic_irq_enable(TMR15_GLOBAL_IRQn, 0, 0);
  nvic_irq_enable(TMR16_GLOBAL_IRQn, 0, 0);
  nvic_irq_enable(CAN1_RX_IRQn, 0, 0);
  nvic_irq_enable(CAN1_ERR_IRQn, 0, 0);
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

  /* gpio output config */
  gpio_bits_reset(LED4_GPIO_PORT, LED4_PIN);
  gpio_bits_reset(GPIOB, LED1_PIN | LED2_PIN | LED3_PIN | CW4_PIN | CW1_PIN);
  gpio_bits_reset(GPIOF, CW2_PIN | CW3_PIN);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = LED4_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(LED4_GPIO_PORT, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = LED1_PIN | LED2_PIN | LED3_PIN | CW4_PIN | CW1_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOB, &gpio_init_struct);

  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_OUTPUT;
  gpio_init_struct.gpio_pins = CW2_PIN | CW3_PIN;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOF, &gpio_init_struct);

  /* add user code begin gpio_config 2 */

  /* add user code end gpio_config 2 */
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
  /* configure the IN3 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = AD4_PIN;
  gpio_init(AD4_GPIO_PORT, &gpio_init_struct);

  /* configure the IN4 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = AD3_PIN;
  gpio_init(AD3_GPIO_PORT, &gpio_init_struct);

  /* configure the IN5 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = AD1_PIN;
  gpio_init(AD1_GPIO_PORT, &gpio_init_struct);

  /* configure the IN6 pin */
  gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
  gpio_init_struct.gpio_pins = AD2_PIN;
  gpio_init(AD2_GPIO_PORT, &gpio_init_struct);

  adc_reset(ADC1);
  crm_adc_clock_div_set(CRM_ADC_DIV_6);

  /*adc_settings--------------------------------------------------------------------*/ 
  adc_base_default_para_init(&adc_base_struct);
  adc_base_struct.sequence_mode = TRUE;
  adc_base_struct.repeat_mode = TRUE;
  adc_base_struct.data_align = ADC_RIGHT_ALIGNMENT;
  adc_base_struct.ordinary_channel_length = 4;
  adc_base_config(ADC1, &adc_base_struct);

  /* adc_ordinary_conversionmode-------------------------------------------- */
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_3, 1, ADC_SAMPLETIME_71_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_4, 2, ADC_SAMPLETIME_71_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_5, 3, ADC_SAMPLETIME_71_5);
  adc_ordinary_channel_set(ADC1, ADC_CHANNEL_6, 4, ADC_SAMPLETIME_71_5);

  /* When "ADCx_ORDINARY_TRIG_SOFTWARE" is selected, user can only use software trigger. \
  The software trigger function is adc_ordinary_software_trigger_enable(ADCx, TRUE); */
  adc_ordinary_conversion_trigger_set(ADC1, ADC_ORDINARY_TRIG_SOFTWARE, TRUE);

  adc_xeoc_sync_mode_set(ADC1, ADC_XEOC_SYNC_RISING_EDGE);

  adc_fail_auto_processing_enale(ADC1, TRUE);
  adc_fail_auto_processing_mode_set(ADC1, ADC_AUTO_STORAGE_TEMPORARY_DATA);

  /* add user code begin adc1_init 2 */

  /* add user code end adc1_init 2 */

  adc_enable(ADC1, TRUE);
  while(adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET);
  
  /* adc calibration-------------------------------------------------------- */
  adc_calibration_init(ADC1);
  while(adc_calibration_init_status_get(ADC1));
  adc_calibration_start(ADC1);
  while(adc_calibration_status_get(ADC1));

  /* add user code begin adc1_init 3 */

  /* add user code end adc1_init 3 */
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
  can_bittime_type can_bittime_struct;
  can_filter_config_type can_filter_struct;

  /* add user code begin can1_init 1 */

  /* add user code end can1_init 1 */

  /*gpio-----------------------------------------------------------------------------*/ 
  gpio_default_para_init(&gpio_init_struct);

  /* configure the CAN1 TX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE12, GPIO_MUX_4);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_12;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure the CAN1 RX pin */
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE11, GPIO_MUX_4);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_11;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  crm_can_clock_select(CRM_CAN1, CRM_CAN_CLOCK_SOURCE_PLL);

  can_software_reset(CAN1, TRUE);

  /*can_bit_time_setting-------------------------------------------------------------*/
  can_bittime_default_para_init(&can_bittime_struct);

  /*set boudrate = pclk/(bittime_div *(bts1_size + bts2_size))-----------------------*/
  can_bittime_struct.bittime_div = 1;
  can_bittime_struct.ac_bts1_size = 240;
  can_bittime_struct.ac_bts2_size = 80;
  can_bittime_struct.ac_rsaw_size = 80;
  can_bittime_set(CAN1, &can_bittime_struct);

  /*can_filter_0_config--------------------------------------------------------------*/
  can_filter_default_para_init(&can_filter_struct);

  can_filter_struct.mask_para.id_type = TRUE;
  can_filter_struct.code_para.id_type = CAN_ID_STANDARD;
  can_filter_struct.mask_para.id = 0x7FF;
  can_filter_struct.code_para.id = 0x0;
  can_filter_struct.mask_para.data_length = 0xF;
  can_filter_struct.code_para.data_length = 0x0;
  can_filter_struct.mask_para.frame_type = TRUE;
  can_filter_struct.code_para.frame_type = CAN_FRAME_DATA;
  can_filter_struct.mask_para.recv_frame = TRUE;
  can_filter_struct.code_para.recv_frame = CAN_RECV_NORMAL;
  can_filter_set(CAN1, CAN_FILTER_NUM_0, &can_filter_struct);

  can_software_reset(CAN1, FALSE);

  can_filter_enable(CAN1, CAN_FILTER_NUM_0, TRUE);

  /*can_base_config------------------------------------------------------------------*/
  can_retransmission_limit_set(CAN1, CAN_RE_TRANS_TIMES_UNLIMIT);
  can_rearbitration_limit_set(CAN1, CAN_RE_ARBI_TIMES_UNLIMIT);
  can_mode_set(CAN1, CAN_MODE_COMMUNICATE);
  can_stb_transmit_mode_set(CAN1, CAN_STB_TRANSMIT_BY_FIFO);
  can_rxbuf_warning_set(CAN1, 1);
  can_rxbuf_overflow_mode_set(CAN1, CAN_RXBUF_OVERFLOW_BE_OVWR);
  can_error_warning_set(CAN1, 11);
  can_restricted_operation_enable(CAN1, FALSE);
  can_receive_all_enable(CAN1, FALSE);

  /* add user code begin can1_init 2 */

  /* add user code end can1_init 2 */
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

  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin tmr3_init 1 */

  /* add user code end tmr3_init 1 */

  /* configure the tmr3 CH1 pin (STEP4/加药泵, PB4) */
  gpio_pin_mux_config(STEP4_GPIO_PORT, GPIO_PINS_SOURCE4, GPIO_MUX_1);
  gpio_init_struct.gpio_pins = STEP4_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(STEP4_GPIO_PORT, &gpio_init_struct);

  /* configure the tmr3 CH2 pin (STEP3/留样转盘, PB5) */
  gpio_pin_mux_config(STEP3_GPIO_PORT, GPIO_PINS_SOURCE5, GPIO_MUX_1);
  gpio_init_struct.gpio_pins = STEP3_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(STEP3_GPIO_PORT, &gpio_init_struct);

  /* configure counter settings */
  tmr_cnt_dir_set(TMR3, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR3, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR3, FALSE);
  tmr_base_init(TMR3, 159, 99);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR3, FALSE);
  tmr_primary_mode_select(TMR3, TMR_PRIMARY_SEL_RESET);

  /* configure channel 1 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_1, &tmr_output_struct);
  tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_1, 0);
  tmr_output_channel_buffer_enable(TMR3, TMR_SELECT_CHANNEL_1, TRUE);

  tmr_output_channel_immediately_set(TMR3, TMR_SELECT_CHANNEL_1, FALSE);

  /* configure channel 2 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR3, TMR_SELECT_CHANNEL_2, &tmr_output_struct);
  tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_2, 0);
  tmr_output_channel_buffer_enable(TMR3, TMR_SELECT_CHANNEL_2, FALSE);

  tmr_output_channel_immediately_set(TMR3, TMR_SELECT_CHANNEL_2, FALSE);


  tmr_counter_enable(TMR3, TRUE);

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

  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin tmr4_init 1 */

  /* add user code end tmr4_init 1 */

  /* configure the tmr4 CH2 pin (STEP2/送留样, PB7) */
  gpio_pin_mux_config(STEP2_GPIO_PORT, GPIO_PINS_SOURCE7, GPIO_MUX_5);
  gpio_init_struct.gpio_pins = STEP2_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(STEP2_GPIO_PORT, &gpio_init_struct);

  /* configure counter settings */
  tmr_cnt_dir_set(TMR4, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR4, TMR_CLOCK_DIV1);
  tmr_period_buffer_enable(TMR4, FALSE);
  tmr_base_init(TMR4, 159, 99);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR4, FALSE);
  tmr_primary_mode_select(TMR4, TMR_PRIMARY_SEL_RESET);

  /* configure channel 2 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR4, TMR_SELECT_CHANNEL_2, &tmr_output_struct);
  tmr_channel_value_set(TMR4, TMR_SELECT_CHANNEL_2, 0);
  tmr_output_channel_buffer_enable(TMR4, TMR_SELECT_CHANNEL_2, TRUE);

  tmr_output_channel_immediately_set(TMR4, TMR_SELECT_CHANNEL_2, FALSE);


  tmr_counter_enable(TMR4, TRUE);

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
  tmr_base_init(TMR6, 999, 159);

  /* configure primary mode settings */
  tmr_primary_mode_select(TMR6, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR6, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR6, TMR_OVF_INT, TRUE);

  /* add user code begin tmr6_init 2 */

  /* add user code end tmr6_init 2 */
}

/**
  * @brief  init tmr15 function.
  * @param  none
  * @retval none
  */
void wk_tmr15_init(void)
{
  /* add user code begin tmr15_init 0 */

  /* add user code end tmr15_init 0 */


  /* add user code begin tmr15_init 1 */

  /* add user code end tmr15_init 1 */

  /* configure counter settings */
  tmr_cnt_dir_set(TMR15, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR15, TMR_CLOCK_DIV1);
  tmr_repetition_counter_set(TMR15, 0);
  tmr_period_buffer_enable(TMR15, FALSE);
  tmr_base_init(TMR15, 9999, 15999);

  /* configure primary mode settings */
  tmr_sub_sync_mode_set(TMR15, FALSE);
  tmr_primary_mode_select(TMR15, TMR_PRIMARY_SEL_RESET);

  tmr_counter_enable(TMR15, TRUE);

  /* enable ovfien interrupt */
  tmr_interrupt_enable(TMR15, TMR_OVF_INT, TRUE);

  /* add user code begin tmr15_init 2 */

  /* add user code end tmr15_init 2 */
}

/**
  * @brief  init tmr17 function.
  * @param  none
  * @retval none
  */
void wk_tmr17_init(void)
{
  /* add user code begin tmr17_init 0 */

  /* add user code end tmr17_init 0 */

  gpio_init_type gpio_init_struct;
  tmr_output_config_type tmr_output_struct;
  tmr_brkdt_config_type tmr_brkdt_struct;

  gpio_default_para_init(&gpio_init_struct);

  /* add user code begin tmr17_init 1 */

  /* add user code end tmr17_init 1 */

  /* configure the tmr17 CH1 pin */
  gpio_pin_mux_config(STEP1_GPIO_PORT, GPIO_PINS_SOURCE9, GPIO_MUX_2);
  gpio_init_struct.gpio_pins = STEP1_PIN;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init(STEP1_GPIO_PORT, &gpio_init_struct);

  /* configure counter settings */
  tmr_cnt_dir_set(TMR17, TMR_COUNT_UP);
  tmr_clock_source_div_set(TMR17, TMR_CLOCK_DIV1);
  tmr_repetition_counter_set(TMR17, 0);
  tmr_period_buffer_enable(TMR17, FALSE);
  tmr_base_init(TMR17, 159, 99);

  /* configure channel 1 output settings */
  tmr_output_struct.oc_mode = TMR_OUTPUT_CONTROL_PWM_MODE_A;
  tmr_output_struct.oc_output_state = TRUE;
  tmr_output_struct.occ_output_state = FALSE;
  tmr_output_struct.oc_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.occ_polarity = TMR_OUTPUT_ACTIVE_HIGH;
  tmr_output_struct.oc_idle_state = FALSE;
  tmr_output_struct.occ_idle_state = FALSE;
  tmr_output_channel_config(TMR17, TMR_SELECT_CHANNEL_1, &tmr_output_struct);
  tmr_channel_value_set(TMR17, TMR_SELECT_CHANNEL_1, 0);
  tmr_output_channel_buffer_enable(TMR17, TMR_SELECT_CHANNEL_1, TRUE);

  tmr_output_channel_immediately_set(TMR17, TMR_SELECT_CHANNEL_1, FALSE);

  /* configure break and dead-time settings */
  tmr_brkdt_struct.brk_enable = FALSE;
  tmr_brkdt_struct.auto_output_enable = FALSE;
  tmr_brkdt_struct.brk_polarity = TMR_BRK_INPUT_ACTIVE_LOW;
  tmr_brkdt_struct.fcsoen_state = FALSE;
  tmr_brkdt_struct.fcsodis_state = FALSE;
  tmr_brkdt_struct.wp_level = TMR_WP_OFF;
  tmr_brkdt_struct.deadtime = 0;
  tmr_brkdt_config(TMR17, &tmr_brkdt_struct);

  tmr_brk_filter_value_set(TMR17, 0);

  tmr_output_enable(TMR17, TRUE);

  tmr_counter_enable(TMR17, TRUE);

  /* add user code begin tmr17_init 2 */

  /* add user code end tmr17_init 2 */
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
  gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE9, GPIO_MUX_1);
  gpio_init_struct.gpio_drive_strength = GPIO_DRIVE_STRENGTH_MODERATE;
  gpio_init_struct.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
  gpio_init_struct.gpio_mode = GPIO_MODE_MUX;
  gpio_init_struct.gpio_pins = GPIO_PINS_9;
  gpio_init_struct.gpio_pull = GPIO_PULL_NONE;
  gpio_init(GPIOA, &gpio_init_struct);

  /* configure param */
  usart_init(USART1, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
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

  /* enable dma1 channel1 full data transfer interrupt */
  dma_interrupt_enable(DMA1_CHANNEL1, DMA_FDT_INT, TRUE);

  /* enable dma1 channel1 half data transfer interrupt */
  dma_interrupt_enable(DMA1_CHANNEL1, DMA_HDT_INT, TRUE);

  /* add user code begin dma1_channel1 1 */

  /* add user code end dma1_channel1 1 */
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

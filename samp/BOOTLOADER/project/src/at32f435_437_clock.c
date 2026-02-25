/**
 * @file    at32f435_437_clock.c
 * @brief   Bootloader时钟配置 (AT32F435, HEXT 8MHz → PLL 288MHz)
 */
#include "at32f435_437_clock.h"

void system_clock_config(void)
{
    crm_reset();

    /* LDO电压配置 */
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
    pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);

    /* Flash时钟分频 */
    flash_clock_divider_set(FLASH_CLOCK_DIV_3);

    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, FALSE);

    /* 启用HEXT */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HEXT, TRUE);
    while (crm_hext_stable_wait() == ERROR) {}

    /* PLL: HEXT 8MHz * 144 / 1 / 4 = 288MHz */
    crm_pll_config(CRM_PLL_SOURCE_HEXT, 144, 1, CRM_PLL_FR_4);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET) {}

    /* 总线分频 */
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb2_div_set(CRM_APB2_DIV_2);
    crm_apb1_div_set(CRM_APB1_DIV_2);

    /* 切换系统时钟到PLL */
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL) {}
    crm_auto_step_mode_enable(FALSE);

    system_core_clock_update();
}

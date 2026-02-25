/**
 * @file    at32f435_437_conf.h
 * @brief   Bootloader外设模块配置 (AT32F435, 仅启用必要模块)
 */
#ifndef __AT32F435_437_CONF_H
#define __AT32F435_437_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined HEXT_VALUE
#define HEXT_VALUE               ((uint32_t)8000000)
#endif

#define HEXT_STARTUP_TIMEOUT     ((uint16_t)0x3000)
#define HICK_VALUE               ((uint32_t)8000000)

/* Bootloader仅需CRM + Flash + MISC */
#define CRM_MODULE_ENABLED
#define FLASH_MODULE_ENABLED
#define MISC_MODULE_ENABLED
#define PWC_MODULE_ENABLED
#define GPIO_MODULE_ENABLED

#ifdef CRM_MODULE_ENABLED
#include "at32f435_437_crm.h"
#endif
#ifdef FLASH_MODULE_ENABLED
#include "at32f435_437_flash.h"
#endif
#ifdef MISC_MODULE_ENABLED
#include "at32f435_437_misc.h"
#endif
#ifdef PWC_MODULE_ENABLED
#include "at32f435_437_pwc.h"
#endif
#ifdef GPIO_MODULE_ENABLED
#include "at32f435_437_gpio.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* __AT32F435_437_CONF_H */

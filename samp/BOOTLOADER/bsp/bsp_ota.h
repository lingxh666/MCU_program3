/**
 * @file    bsp_ota.h
 * @brief   Bootloader OTA升级接口 (AT32F435)
 */
#ifndef __BSP_OTA_H__
#define __BSP_OTA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f435_437.h"
#include "bsp_flash.h"

/* APP起始地址 — 复用bsp_flash.h中的FLASH_APP_ADDR */
#define APP_START_ADDR              FLASH_APP_ADDR

/* OTA升级标志地址 — Bank1末尾扇区 */
#define OTA_UPGRADE_FLAG_ADDR       ((uint32_t)0x0807F000)

/* 升级标志值 "ATOA" */
#define OTA_UPGRADE_FLAG            ((uint32_t)0x41544F41)

typedef void (*otafun)(void);

void ota_upgrade_app_handle(void);
void app_load(uint32_t app_addr);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_OTA_H__ */

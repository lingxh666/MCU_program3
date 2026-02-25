/**
 * @file    bsp_flash.h
 * @brief   Bootloader Flash操作接口 (AT32F435)
 */
#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f435_437.h"

/* ---------- Flash布局 ---------- */

/* Bank1扇区大小: 4KB */
#define FLASH_SECTOR_SIZE           ((uint32_t)0x1000)

/* APP区域: 0x08004000 ~ 0x0803FFFF (240KB, 60个扇区) */
#define FLASH_APP_ADDR              ((uint32_t)0x08004000)
#define FLASH_APP_SECTORS           60

/* OTA临时区域: 0x08040000 ~ 0x0807EFFF (252KB) */
#define FLASH_BKP_ADDR              ((uint32_t)0x08040000)

/* KVDB区域: Bank2 0x08080000 ~ 0x080FFFFF (512KB, 2KB/扇区) */
#define FLASH_KVDB_ADDR             ((uint32_t)0x08080000)
#define FLASH_KVDB_END              ((uint32_t)0x08100000)
#define FLASH_BANK2_SECTOR_SIZE     ((uint32_t)0x0800)

/* ---------- 接口函数 ---------- */

error_status app_flash_update(void);
flag_status  flash_upgrade_flag_read(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_FLASH_H__ */

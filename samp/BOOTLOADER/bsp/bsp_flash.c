/**
 * @file    bsp_flash.c
 * @brief   Bootloader Flash操作 (AT32F435, Bank1 4KB扇区)
 */
#include <string.h>
#include "bsp_flash.h"
#include "bsp_ota.h"

static uint8_t data_buf1[FLASH_SECTOR_SIZE];
static uint8_t data_buf2[FLASH_SECTOR_SIZE];

flag_status flash_upgrade_flag_read(void)
{
    if ((*(uint32_t *)OTA_UPGRADE_FLAG_ADDR) == OTA_UPGRADE_FLAG)
        return SET;
    return RESET;
}

static void flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        flash_halfword_program(addr + i, *(const uint16_t *)(buf + i));
    }
}

static void flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        *(uint16_t *)(buf + i) = *(volatile uint16_t *)(addr + i);
    }
}

/**
 * @brief  从OTA临时区复制固件到APP区 (60个4KB扇区)
 */
error_status app_flash_update(void)
{
    uint8_t sector;
    uint32_t offset;

    flash_unlock();
    for (sector = 0; sector < FLASH_APP_SECTORS; sector++) {
        offset = (uint32_t)sector * FLASH_SECTOR_SIZE;

        flash_read(FLASH_BKP_ADDR + offset, data_buf1, FLASH_SECTOR_SIZE);
        flash_sector_erase(FLASH_APP_ADDR + offset);
        flash_write(FLASH_APP_ADDR + offset, data_buf1, FLASH_SECTOR_SIZE);

        /* 回读校验 */
        flash_read(FLASH_APP_ADDR + offset, data_buf2, FLASH_SECTOR_SIZE);
        if (memcmp(data_buf1, data_buf2, FLASH_SECTOR_SIZE) != 0) {
            flash_lock();
            return ERROR;
        }
    }
    flash_lock();
    return SUCCESS;
}

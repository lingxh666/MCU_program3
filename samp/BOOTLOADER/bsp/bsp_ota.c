/**
 * @file    bsp_ota.c
 * @brief   Bootloader OTA升级逻辑 (AT32F435)
 */
#include "bsp_ota.h"

#if defined (__CC_ARM)
  #pragma O0
#elif defined (__ICCARM__)
  #pragma optimize=s none
#endif
/**
 * @brief  跳转到APP
 * @param  app_addr APP起始地址
 */
void app_load(uint32_t app_addr)
{
    otafun jump;

    /* 检查复位向量是否指向Flash区域 (0x08xxxxxx) */
    if (((*(uint32_t *)(app_addr + 4)) & 0xFF000000) != 0x08000000)
        return;

    jump = (otafun)*(uint32_t *)(app_addr + 4);
    __set_MSP(*(uint32_t *)app_addr);
    jump();
}

/**
 * @brief  OTA升级处理：复制固件 → 清标志 → 擦KVDB → 跳转APP
 * @note   最多重试3次，成功后即跳转，避免重复写Flash
 */
void ota_upgrade_app_handle(void)
{
    uint8_t retry;
    uint32_t addr;

    if (flash_upgrade_flag_read() != SET)
        return;

    for (retry = 0; retry < 3; retry++) {
        if (app_flash_update() != SUCCESS)
            continue;

        /* 清除升级标志 */
        flash_unlock();
        flash_sector_erase(OTA_UPGRADE_FLAG_ADDR);

        /* 擦除KVDB区域 (Bank2: 0x08080000~0x080FFFFF, 2KB/扇区) */
        for (addr = FLASH_KVDB_ADDR; addr < FLASH_KVDB_END; addr += FLASH_BANK2_SECTOR_SIZE) {
            flash_sector_erase(addr);
        }
        flash_lock();

        app_load(APP_START_ADDR);
        break;  /* app_load返回说明APP无效，不再重试 */
    }
}

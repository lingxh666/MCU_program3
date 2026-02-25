/**
 * @file    main.c
 * @brief   Bootloader入口 (AT32F435)
 *          检查升级标志 → 有则复制固件 → 跳转APP
 */
#include "at32f435_437_clock.h"
#include "bsp_ota.h"

int main(void)
{
    system_clock_config();

    if (flash_upgrade_flag_read() == RESET) {
        app_load(APP_START_ADDR);
    }

    while (1) {
        ota_upgrade_app_handle();
    }
}

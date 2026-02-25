#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f435_437.h"

/* AT32F435 内部Flash布局:
 *   Bank1: 0x08000000 ~ 0x0807FFFF (512KB, 4KB/扇区)
 *   Bank2: 0x08080000 ~ 0x080FFFFF (512KB, 2KB/扇区)
 */
#define FLASH_BANK1_BASE         ((uint32_t)0x08000000)
#define FLASH_BANK2_BASE         ((uint32_t)0x08080000)
#define FLASH_BANK1_SECTOR_SIZE  4096   /* Bank1: 4KB/扇区 */
#define FLASH_BANK2_SECTOR_SIZE  2048   /* Bank2: 2KB/扇区 */

/* OTA升级标志 — 放在Bank1末尾扇区 */
#define OTA_UPGRADE_FLAG_ADDR    ((uint32_t)0x0807F000)
#define OTA_UPGRADE_FLAG         ((uint32_t)0x41544F41)  /* "ATOA" */

/* 基础读写 */
void     bsp_flash_read(uint32_t addr, uint16_t *buf, uint16_t count);
error_status bsp_flash_write_nocheck(uint32_t addr, uint16_t *buf, uint16_t count);
error_status bsp_flash_write(uint32_t addr, uint16_t *buf, uint16_t count);

/* 整扇区写入 */
error_status bsp_flash_sector_write(uint32_t sector_addr, uint8_t *data, uint16_t len);

/* OTA升级标志 */
flag_status  bsp_flash_upgrade_flag_read(void);
void         bsp_flash_upgrade_flag_write(void);
void         bsp_flash_upgrade_flag_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_FLASH_H__ */

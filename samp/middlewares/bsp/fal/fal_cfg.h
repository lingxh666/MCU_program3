#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include "fal_def.h"

#ifdef __cplusplus
extern "C" {
#endif

int fal_init(void);

extern const struct fal_flash_dev AT32_onchip_flash;
extern struct fal_flash_dev QSPI_NOR_flash;

#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &AT32_onchip_flash,                                              \
    &QSPI_NOR_flash,                                                 \
}

/* ====================== Flash Layout (AT32F435) ==========================
 *
 * Bank1 (512KB, 4KB/sector): 0x08000000 ~ 0x0807FFFF
 *   Bootloader : 0x08000000 ~ 0x08003FFF  (16KB)
 *   APP        : 0x08004000 ~ 0x0803FFFF  (240KB)
 *   OTA临时区  : 0x08040000 ~ 0x0807EFFF  (252KB)
 *   升级标志   : 0x0807F000 ~ 0x0807FFFF  (4KB)
 *
 * Bank2 (512KB, 2KB/sector): 0x08080000 ~ 0x080FFFFF
 *   KVDB       : offset 0, 510KB  (FAL管理)
 *
 * QSPI NOR (8MB): 外部Flash
 *   TSDB       : offset 0, 8MB    (FAL管理)
 *
 * 注: Bootloader/APP/OTA区由 bsp_flash.h 和 app_ota.h 中的宏直接管理,
 *     不纳入FAL分区表。FAL仅管理KVDB和TSDB。
 * ========================================================================= */
#define FAL_PART_HAS_TABLE_CFG

#define FAL_PART_TABLE                                                                       \
{                                                                                            \
    {FAL_PART_MAGIC_WORD, "fdb_kvdb", "AT32_onchip", 0,          510*1024,     0}, \
    {FAL_PART_MAGIC_WORD, "fdb_tsdb", "qspi_nor",    0,          8*1024*1024,  0}, \
}

#ifdef __cplusplus
}
#endif

#endif /* _FAL_CFG_H_ */

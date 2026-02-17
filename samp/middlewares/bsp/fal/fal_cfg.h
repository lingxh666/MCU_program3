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

/* ====================== Partition Configuration ========================== */
#define FAL_PART_HAS_TABLE_CFG

#define FAL_PART_TABLE                                                                       \
{                                                                                            \
    {FAL_PART_MAGIC_WORD, "fdb_kvdb", "AT32_onchip", 0,          512*1024,     0}, \
    {FAL_PART_MAGIC_WORD, "fdb_tsdb", "qspi_nor",    0,          8*1024*1024,  0}, \
}

#ifdef __cplusplus
}
#endif

#endif /* _FAL_CFG_H_ */

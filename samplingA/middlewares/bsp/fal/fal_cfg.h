#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* FAL 配置：仅声明接口与常量，具体注册在 fal_cfg.c 中完成 */

/* 初始化 FAL（注册设备与分区表） */
int fal_init(void);

/* 设备与分区名称宏统一移除，避免与实际分区/设备名不一致产生混淆 */

#ifdef __cplusplus
}
#endif

#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

extern const struct fal_flash_dev AT32_onchip_flash;
extern struct fal_flash_dev SPI_NOR_flash;  /* 非 const，支持动态更新 */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &AT32_onchip_flash,                                              \
    &SPI_NOR_flash,                                                  \
}
/* ====================== Partition Configuration ========================== */
#define FAL_PART_HAS_TABLE_CFG 
/* partition table */
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    {FAL_PART_MAGIC_WORD,       "fdb_kvdb",     "AT32_onchip",   0,          512*1024,      0}, \
    {FAL_PART_MAGIC_WORD,       "fdb_tsdb",     "spi_nor",       0,          8*1024*1024,   0}, \
}
/* 注意：fdb_tsdb 分区大小会在 spi_nor_init() 时根据实际芯片容量自动调整 */


#endif /* _FAL_CFG_H_ */

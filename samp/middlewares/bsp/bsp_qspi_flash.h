#ifndef BSP_QSPI_FLASH_H
#define BSP_QSPI_FLASH_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ZD25Q64B: 8MB, 64KB block, 4KB sector, 256B page */
#define QFLASH_CHIP_SIZE         (8 * 1024 * 1024)
#define QFLASH_BLOCK_SIZE        (64 * 1024)
#define QFLASH_SECTOR_SIZE       4096
#define QFLASH_PAGE_SIZE         256

/* ZD25Q / W25Q Device IDs */
#define ZD25Q64_ID               0xBA16
#define ZD25Q128_ID              0xBA17
#define W25Q64_ID                0xEF16
#define W25Q128_ID               0xEF17

/* Flash Commands */
#define QFLASH_CMD_WRITE_ENABLE  0x06
#define QFLASH_CMD_READ_SR1      0x05
#define QFLASH_CMD_READ_DATA     0x03
#define QFLASH_CMD_FAST_READ     0x0B
#define QFLASH_CMD_PAGE_PROGRAM  0x02
#define QFLASH_CMD_SECTOR_ERASE  0x20
#define QFLASH_CMD_BLOCK_ERASE   0xD8
#define QFLASH_CMD_CHIP_ERASE    0xC7
#define QFLASH_CMD_READ_ID       0x90
#define QFLASH_CMD_JEDEC_ID      0x9F

/* API */
uint8_t  qspi_flash_init(void);
uint16_t qspi_flash_read_id(void);
void     qspi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len);
void     qspi_flash_write(uint32_t addr, uint8_t *buf, uint32_t len);
void     qspi_flash_erase_sector(uint32_t addr);
void     qspi_flash_erase_block(uint32_t addr);
void     qspi_flash_erase_chip(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_QSPI_FLASH_H */

#include <fal.h>
#include "bsp_qspi_flash.h"

static size_t g_qspi_nor_size = 8 * 1024 * 1024;

/* Forward declaration — defined at bottom of this file */
struct fal_flash_dev QSPI_NOR_flash;

static void feed_dog(void)
{
    wdt_counter_reload();
}

static int qspi_nor_init(void)
{
    uint16_t flash_id = qspi_flash_read_id();

    switch(flash_id) {
        case ZD25Q64_ID:
        case W25Q64_ID:
            g_qspi_nor_size = 8 * 1024 * 1024;
            break;
        case ZD25Q128_ID:
        case W25Q128_ID:
            g_qspi_nor_size = 16 * 1024 * 1024;
            break;
        default:
            g_qspi_nor_size = 8 * 1024 * 1024;
            break;
    }

    QSPI_NOR_flash.len = g_qspi_nor_size;
    return 1;
}

static int qspi_nor_read(long offset, uint8_t *buf, size_t size)
{
    if ((size_t)offset + size > g_qspi_nor_size) return -1;
    feed_dog();
    qspi_flash_read((uint32_t)offset, buf, (uint32_t)size);
    feed_dog();
    return (int)size;
}

static int qspi_nor_write(long offset, const uint8_t *buf, size_t size)
{
    if ((size_t)offset + size > g_qspi_nor_size) return -1;
    feed_dog();
    qspi_flash_write((uint32_t)offset, (uint8_t*)buf, (uint32_t)size);
    feed_dog();
    return (int)size;
}

static int qspi_nor_erase(long offset, size_t size)
{
    if ((size_t)offset + size > g_qspi_nor_size) return -1;

    uint32_t addr = (uint32_t)offset & ~(4096U - 1);
    uint32_t end  = (uint32_t)offset + (uint32_t)size;
    for (; addr < end; addr += 4096U)
    {
        qspi_flash_erase_sector(addr);
        feed_dog();
    }
    return (int)size;
}

struct fal_flash_dev QSPI_NOR_flash =
{
    .name       = "qspi_nor",
    .addr       = 0x00000000,
    .len        = 8 * 1024 * 1024,
    .blk_size   = 4096,
    .ops        = {qspi_nor_init, qspi_nor_read, qspi_nor_write, qspi_nor_erase},
    .write_gran = 1
};

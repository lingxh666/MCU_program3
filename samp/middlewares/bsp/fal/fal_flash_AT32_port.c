#include <string.h>
#include <fal.h>
#include "at32f435_437.h"

/* AT32F435 internal flash: 2KB per sector (for bank2 area used by KVDB) */
#define ONCHIP_PAGE_SIZE     2048

static int init(void)
{
    return 1;
}

static void feed_dog(void)
{
    wdt_counter_reload();
}

static int read(long offset, uint8_t *buf, size_t size)
{
    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;

    memcpy(buf, (const void *)(AT32_onchip_flash.addr + offset), size);
    return size;
}

static int write(long offset, const uint8_t *buf, size_t size)
{
    uint32_t addr = AT32_onchip_flash.addr + offset;
    uint32_t write_data, read_data;
    size_t i;

    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;
    if (addr % 4 != 0)
        return -1;

    flash_unlock();

    for (i = 0; i + 4 <= size; i += 4, buf += 4, addr += 4)
    {
        memcpy(&write_data, buf, 4);
        flash_word_program(addr, write_data);
        read_data = *(uint32_t *)addr;
        if (read_data != write_data) {
            flash_lock();
            return -1;
        }
        feed_dog();
    }

    if (i < size)
    {
        uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(tail, buf, size - i);
        memcpy(&write_data, tail, 4);
        flash_word_program(addr, write_data);
        read_data = *(uint32_t *)addr;
        if (read_data != write_data) {
            flash_lock();
            return -1;
        }
        feed_dog();
    }

    flash_lock();
    return size;
}

static int erase(long offset, size_t size)
{
    uint32_t addr = AT32_onchip_flash.addr + offset;
    size_t erase_pages, i;
    flash_status_type flash_status;

    if (size == 0)
        return 0;
    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;

    erase_pages = size / ONCHIP_PAGE_SIZE;
    if (size % ONCHIP_PAGE_SIZE != 0)
        erase_pages++;

    flash_unlock();
    for (i = 0; i < erase_pages; i++)
    {
        flash_status = flash_sector_erase(addr + (ONCHIP_PAGE_SIZE * i));
        if (flash_status != FLASH_OPERATE_DONE) {
            flash_lock();
            return -1;
        }
        feed_dog();
    }
    flash_lock();

    return size;
}

/* KVDB on-chip flash: bank2 area starting at 0x08080000, 512KB, 2KB sector */
const struct fal_flash_dev AT32_onchip_flash =
{
    .name       = "AT32_onchip",
    .addr       = 0x08080000,
    .len        = 512 * 1024,
    .blk_size   = 2 * 1024,
    .ops        = {init, read, write, erase},
    .write_gran = 32
};

#include <string.h>
#include <fal.h>
#include "at32f403a_407_wk_config.h"
#include "spi_flash.h"

#define PAGE_SIZE     2048


static int init(void)
{
    /* do nothing now */
    return 1;
}

static int ef_err_port_cnt = 0;
int on_ic_read_cnt  = 0;
int on_ic_write_cnt = 0;

void feed_dog(void)
{
    wdt_counter_reload();
}

static int read(long offset, uint8_t *buf, size_t size)
{
    size_t i;
    uint32_t addr = AT32_onchip_flash.addr + offset;

    /* range check */
    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;

    if (addr % 4 != 0)
        ef_err_port_cnt++;

    for (i = 0; i < size; i++, addr++, buf++)
    {
        *buf = *(uint8_t *) addr; // Read byte by byte
    }
    on_ic_read_cnt++;
    return size;
}

static int write(long offset, const uint8_t *buf, size_t size)
{
    size_t i;
    uint32_t addr = AT32_onchip_flash.addr + offset;
    uint32_t write_data, read_data;


    /* range/alignment check */
    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;
    if (addr % 4 != 0) {
        ef_err_port_cnt++;
        return -1; /* 写入地址必须4字节对齐（write_gran=32） */
    }

    flash_unlock();
    /* 主循环：按4字节写，尾部不足4字节使用0xFF填充 */
    for (i = 0; i + 4 <= size; i += 4, buf += 4, addr += 4) {
        memcpy(&write_data, buf, 4);
        flash_word_program(addr, write_data);
        read_data = *(uint32_t *)addr;
        if (read_data != write_data) {
            flash_lock();
            return -1; // 校验失败
        }
        feed_dog();
    }

    /* 处理尾部不足4字节（如果有） */
    if (i < size) {
        uint8_t tail[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        size_t rem = size - i;
        for (size_t k = 0; k < rem; ++k) tail[k] = buf[k];
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

    on_ic_write_cnt++;
    return size;
}

static int erase(long offset, size_t size)
{
    uint32_t PageAddress, addr = AT32_onchip_flash.addr + offset;
    size_t erase_pages, i;
    flash_status_type flash_status;

    // Determine the page size based on the flash device
    size_t page_size;
        page_size = PAGE_SIZE;

    if (size == 0)
        return 0;
    /* range check */
    if ((size_t)offset + size > AT32_onchip_flash.len)
        return -1;

    // Calculate the number of pages to erase
    erase_pages = size / page_size;
    if (size % page_size != 0) {
        erase_pages++;
    }

    flash_unlock();
    for (i = 0; i < erase_pages; i++) {
        PageAddress = addr + (page_size * i);
        flash_status = flash_sector_erase(PageAddress); // Call the appropriate erase function
        if (flash_status != FLASH_OPERATE_DONE) {
            flash_lock();
            return -1; // Error if erase operation fails
        } else {
            feed_dog();
        }
    }
    flash_lock();

    return size;
}

// Define flash devices
const struct fal_flash_dev AT32_onchip_flash =
{
    .name       = "AT32_onchip",
    .addr       = 0x08080000,
    .len        = 512 * 1024,
    .blk_size   = 2 * 1024,
    .ops        = {init, read, write, erase},
    .write_gran = 32
};

/* ===================== SPI Nor 端口实现 (支持多型号) ===================== */
/* 支持: ZD25Q16/32/64/128, W25Q32/64/128 */

static size_t get_spi_flash_size(uint16_t device_id)
{
    switch(device_id) {
        case 0xBA14: /* ZD25Q16 */
            return 2 * 1024 * 1024;   /* 2MB */
        case 0xBA15: /* ZD25Q32 */
        case 0xEF15: /* W25Q32 */
            return 4 * 1024 * 1024;   /* 4MB */
        case 0xBA16: /* ZD25Q64 */
        case 0xEF16: /* W25Q64 */
            return 8 * 1024 * 1024;   /* 8MB */
        case 0xBA17: /* ZD25Q128 */
        case 0xEF17: /* W25Q128 */
            return 16 * 1024 * 1024;  /* 16MB */
        default:
            printf("Unknown SPI Flash ID: 0x%04X, using default 2MB\r\n", device_id);
            return 8 * 1024 * 1024;   /* 默认2MB */
    }
}

/* 全局变量存储实际Flash容量 */
static size_t g_spi_nor_size = 8 * 1024 * 1024;

/* 前置声明，用于动态更新设备长度 */
extern struct fal_flash_dev SPI_NOR_flash;

static int spi_nor_init(void)
{
    /* 读取Flash ID并确定容量 */
    uint16_t flash_id = spiflash_read_id();
    g_spi_nor_size = get_spi_flash_size(flash_id);
    
    /* ★ 动态更新 FAL 设备长度，确保与实际硬件一致 */
    SPI_NOR_flash.len = g_spi_nor_size;

    return 1;
}

static int spi_nor_read(long offset, uint8_t *buf, size_t size)
{
    if ((size_t)offset + size > g_spi_nor_size) return -1;
    
    /* ? TSDB迭代查询会连续多次小块读取(256B)，需要每次都喂狗 */
    feed_dog();
    
    spiflash_read(buf, (uint32_t)offset, (uint32_t)size);
    
    /* 读取完成后再次喂狗（SPI传输可能耗时） */
    feed_dog();
    
    return (int)size;
}

static int spi_nor_write(long offset, const uint8_t *buf, size_t size)
{
    if ((size_t)offset + size > g_spi_nor_size) return -1;
    
    /* 写操作前后喂狗（Flash写入耗时） */
    feed_dog();
    spiflash_write((uint8_t*)buf, (uint32_t)offset, (uint32_t)size);
    feed_dog();
    
    return (int)size;
}

static int spi_nor_erase(long offset, size_t size)
{
    if ((size_t)offset + size > g_spi_nor_size) return -1;
    uint32_t start = (uint32_t)offset;
    uint32_t end   = start + (uint32_t)size;
    const uint32_t SECTOR = 4096U;
    uint32_t addr = start - (start % SECTOR);
    for (; addr < end; addr += SECTOR) {
        /* spiflash_sector_erase() 接收的是扇区索引，非字节地址。
           之前传入字节地址会在内部再次乘以 SECTOR_SIZE，导致地址越界并可能卡死。 */
        spiflash_sector_erase(addr / SECTOR);
        
        /* 喂狗，防止看门狗复位（擦除操作耗时长） */
        feed_dog();
    }
    return (int)size;
}

/* ★ 改为非 const，允许 init 时动态更新 len */
struct fal_flash_dev SPI_NOR_flash =
{
    .name       = "spi_nor",
    .addr       = 0x00000000,
    .len        = 8 * 1024 * 1024,  /* 默认8MB，init时根据芯片ID动态更新 */
    .blk_size   = 4096,
    .ops        = {spi_nor_init, spi_nor_read, spi_nor_write, spi_nor_erase},
    .write_gran = 1
};

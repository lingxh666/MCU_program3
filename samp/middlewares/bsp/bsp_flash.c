#include "bsp_flash.h"
#include <string.h>

/* 内部缓冲区（按最大扇区大小分配） */
static uint16_t s_flash_buf[FLASH_BANK1_SECTOR_SIZE / 2];

/* 根据地址判断扇区大小 */
static uint16_t get_sector_size(uint32_t addr)
{
    return (addr >= FLASH_BANK2_BASE) ? FLASH_BANK2_SECTOR_SIZE
                                      : FLASH_BANK1_SECTOR_SIZE;
}

/*--- 基础读取（半字模式） ---*/
void bsp_flash_read(uint32_t addr, uint16_t *buf, uint16_t count)
{
    uint16_t i;
    for (i = 0; i < count; i++)
    {
        buf[i] = *(volatile uint16_t *)addr;
        addr += 2;
    }
}

/*--- 无擦除检查写入 ---*/
error_status bsp_flash_write_nocheck(uint32_t addr, uint16_t *buf, uint16_t count)
{
    uint16_t i;
    for (i = 0; i < count; i++)
    {
        if (flash_halfword_program(addr, buf[i]) != FLASH_OPERATE_DONE)
            return ERROR;
        addr += 2;
    }
    return SUCCESS;
}

/*--- 带擦除保护的智能写入（跨扇区自动处理） ---*/
error_status bsp_flash_write(uint32_t addr, uint16_t *buf, uint16_t count)
{
    uint32_t offset;
    uint32_t sec_pos;
    uint16_t sec_size, sec_half;
    uint16_t sec_off, sec_remain;
    uint16_t i;
    flash_status_type status;

    sec_size = get_sector_size(addr);
    sec_half = sec_size / 2;

    flash_unlock();

    offset  = addr - FLASH_BASE;
    sec_pos = offset / sec_size;
    sec_off = (offset % sec_size) / 2;
    sec_remain = sec_half - sec_off;
    if (count <= sec_remain)
        sec_remain = count;

    while (1)
    {
        uint32_t sec_base = sec_pos * sec_size + FLASH_BASE;
        bsp_flash_read(sec_base, s_flash_buf, sec_half);

        /* 检查目标区域是否需要擦除 */
        for (i = 0; i < sec_remain; i++)
        {
            if (s_flash_buf[sec_off + i] != 0xFFFF)
                break;
        }

        if (i < sec_remain)
        {
            /* 需要擦除：先读出整扇区，合并数据，再擦除写回 */
            status = flash_operation_wait_for(ERASE_TIMEOUT);
            if (status == FLASH_PROGRAM_ERROR || status == FLASH_EPP_ERROR)
                flash_flag_clear(FLASH_PRGMERR_FLAG | FLASH_EPPERR_FLAG);
            else if (status == FLASH_OPERATE_TIMEOUT)
            {
                flash_lock();
                return ERROR;
            }

            status = flash_sector_erase(sec_base);
            if (status != FLASH_OPERATE_DONE)
            {
                flash_lock();
                return ERROR;
            }

            for (i = 0; i < sec_remain; i++)
                s_flash_buf[i + sec_off] = buf[i];

            if (bsp_flash_write_nocheck(sec_base, s_flash_buf, sec_half) != SUCCESS)
            {
                flash_lock();
                return ERROR;
            }
        }
        else
        {
            /* 无需擦除：直接写入 */
            if (bsp_flash_write_nocheck(addr, buf, sec_remain) != SUCCESS)
            {
                flash_lock();
                return ERROR;
            }
        }

        if (count == sec_remain)
            break;

        /* 移动到下一扇区 */
        sec_pos++;
        sec_off = 0;
        buf   += sec_remain;
        addr  += sec_remain * 2;
        count -= sec_remain;

        sec_size = get_sector_size(addr);
        sec_half = sec_size / 2;
        sec_remain = (count > sec_half) ? sec_half : count;
    }

    flash_lock();
    return SUCCESS;
}

/*--- 整扇区写入（擦除后写入，len不超过扇区大小） ---*/
error_status bsp_flash_sector_write(uint32_t sector_addr, uint8_t *data, uint16_t len)
{
    uint16_t sec_size = get_sector_size(sector_addr);
    uint16_t i, write_data, read_data;

    if (len > sec_size)
        return ERROR;

    flash_unlock();
    if (flash_sector_erase(sector_addr) != FLASH_OPERATE_DONE)
    {
        flash_lock();
        return ERROR;
    }

    for (i = 0; i + 2 <= len; i += 2)
    {
        write_data = (data[i + 1] << 8) | data[i];
        flash_halfword_program(sector_addr + i, write_data);
        read_data = *(volatile uint16_t *)(sector_addr + i);
        if (read_data != write_data)
        {
            flash_lock();
            return ERROR;
        }
    }

    /* 处理奇数字节尾部 */
    if (i < len)
    {
        write_data = 0xFF00 | data[i];
        flash_halfword_program(sector_addr + i, write_data);
    }

    flash_lock();
    return SUCCESS;
}

/*--- OTA升级标志读取 ---*/
flag_status bsp_flash_upgrade_flag_read(void)
{
    if (*(volatile uint32_t *)OTA_UPGRADE_FLAG_ADDR == OTA_UPGRADE_FLAG)
        return SET;
    return RESET;
}

/*--- OTA升级标志写入 ---*/
void bsp_flash_upgrade_flag_write(void)
{
    flash_unlock();
    flash_sector_erase(OTA_UPGRADE_FLAG_ADDR);
    flash_word_program(OTA_UPGRADE_FLAG_ADDR, OTA_UPGRADE_FLAG);
    flash_lock();
}

/*--- OTA升级标志清除 ---*/
void bsp_flash_upgrade_flag_clear(void)
{
    flash_unlock();
    flash_sector_erase(OTA_UPGRADE_FLAG_ADDR);
    flash_lock();
}

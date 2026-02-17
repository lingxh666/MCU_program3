/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fal.h>
#include <string.h>
#include <stdlib.h>

#define FAL_PART_MAGIC_WORD         0x45503130

struct part_flash_info
{
    const struct fal_flash_dev *flash_dev;
};

#ifdef FAL_PART_HAS_TABLE_CFG

#if !defined(FAL_PART_TABLE)
#error "You must defined FAL_PART_TABLE on 'fal_cfg.h'"
#endif

static const struct fal_partition partition_table_def[] = FAL_PART_TABLE;
static const struct fal_partition *partition_table = NULL;
static struct part_flash_info part_flash_cache[sizeof(partition_table_def) / sizeof(partition_table_def[0])] = { 0 };

#else /* FAL_PART_HAS_TABLE_CFG */

#if !defined(FAL_PART_TABLE_FLASH_DEV_NAME)
#error "You must defined FAL_PART_TABLE_FLASH_DEV_NAME on 'fal_cfg.h'"
#endif
#if !defined(FAL_PART_TABLE_END_OFFSET)
#error "You must defined FAL_PART_TABLE_END_OFFSET on 'fal_cfg.h'"
#endif

static struct fal_partition *partition_table = NULL;
static struct part_flash_info *part_flash_cache = NULL;
#endif /* FAL_PART_HAS_TABLE_CFG */

static uint8_t init_ok = 0;
static size_t partition_table_len = 0;

void fal_show_part_table(void)
{
    char *item1 = "name", *item2 = "flash_dev";
    size_t i, part_name_max = strlen(item1), flash_dev_name_max = strlen(item2);
    const struct fal_partition *part;

    if (partition_table_len)
    {
        for (i = 0; i < partition_table_len; i++)
        {
            part = &partition_table[i];
            if (strlen(part->name) > part_name_max)
                part_name_max = strlen(part->name);
            if (strlen(part->flash_name) > flash_dev_name_max)
                flash_dev_name_max = strlen(part->flash_name);
        }
    }
    log_i("==================== FAL partition table ====================");
    log_i("| %-*.*s | %-*.*s |   offset   |    length  |", part_name_max, FAL_DEV_NAME_MAX, item1, flash_dev_name_max,
            FAL_DEV_NAME_MAX, item2);
    log_i("-------------------------------------------------------------");
    for (i = 0; i < partition_table_len; i++)
    {
        part = &partition_table[i];
        log_i("| %-*.*s | %-*.*s | 0x%08lx | 0x%08x |", part_name_max, FAL_DEV_NAME_MAX, part->name, flash_dev_name_max,
                FAL_DEV_NAME_MAX, part->flash_name, part->offset, part->len);
    }
    log_i("=============================================================");
}

static int check_and_update_part_cache(const struct fal_partition *table, size_t len)
{
    const struct fal_flash_dev *flash_dev = NULL;
    size_t i;

#ifndef FAL_PART_HAS_TABLE_CFG
    if (part_flash_cache)
    {
        FAL_FREE(part_flash_cache);
    }
    part_flash_cache = FAL_MALLOC(len * sizeof(struct part_flash_info));
    if (part_flash_cache == NULL)
    {
        log_e("Initialize failed! No memory for partition table cache");
        return -2;
    }
#endif

    for (i = 0; i < len; i++)
    {
        flash_dev = fal_flash_device_find(table[i].flash_name);
        if (flash_dev == NULL)
        {
            log_d("Warning: Do NOT found the flash device(%s).", table[i].flash_name);
            continue;
        }
        if (table[i].offset >= (long)flash_dev->len)
        {
            log_e("Initialize failed! Partition(%s) offset address(%ld) out of flash bound(<%d).",
                    table[i].name, table[i].offset, flash_dev->len);
            partition_table_len = 0;
            return -1;
        }
        part_flash_cache[i].flash_dev = flash_dev;
    }

    return 0;
}

int fal_partition_init(void)
{
    if (init_ok)
    {
        return partition_table_len;
    }

#ifdef FAL_PART_HAS_TABLE_CFG
    partition_table = &partition_table_def[0];
    partition_table_len = sizeof(partition_table_def) / sizeof(partition_table_def[0]);
#endif

    if (check_and_update_part_cache(partition_table, partition_table_len) != 0)
    {
        goto _exit;
    }

    init_ok = 1;

_exit:

#if FAL_DEBUG
    fal_show_part_table();
#endif

    return partition_table_len;
}

const struct fal_partition *fal_partition_find(const char *name)
{
    assert(init_ok);
    size_t i;

    for (i = 0; i < partition_table_len; i++)
    {
        if (!strcmp(name, partition_table[i].name))
        {
            return &partition_table[i];
        }
    }
    return NULL;
}

static const struct fal_flash_dev *flash_device_find_by_part(const struct fal_partition *part)
{
    assert(part >= partition_table);
    assert(part <= &partition_table[partition_table_len - 1]);
    return part_flash_cache[part - partition_table].flash_dev;
}

const struct fal_partition *fal_get_partition_table(size_t *len)
{
    assert(init_ok);
    assert(len);
    *len = partition_table_len;
    return partition_table;
}

void fal_set_partition_table_temp(struct fal_partition *table, size_t len)
{
    assert(init_ok);
    assert(table);
    check_and_update_part_cache(table, len);
    partition_table_len = len;
    partition_table = table;
}

int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    assert(part);
    assert(buf);

    if (addr + size > part->len)
    {
        log_e("Partition read error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        log_e("Partition read error! Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.read(part->offset + addr, buf, size);
    if (ret < 0)
    {
        log_e("Partition read error! Flash device(%s) read error!", part->flash_name);
    }

    return ret;
}

int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    assert(part);
    assert(buf);

    if (addr + size > part->len)
    {
        log_e("Partition write error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        log_e("Partition write error! Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.write(part->offset + addr, buf, size);
    if (ret < 0)
    {
        log_e("Partition write error! Flash device(%s) write error!", part->flash_name);
    }

    return ret;
}

int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    assert(part);

    if (addr + size > part->len)
    {
        log_e("Partition erase error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        log_e("Partition erase error! Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.erase(part->offset + addr, size);
    if (ret < 0)
    {
        log_e("Partition erase error! Flash device(%s) erase error!", part->flash_name);
    }

    return ret;
}

int fal_partition_erase_all(const struct fal_partition *part)
{
    return fal_partition_erase(part, 0, part->len);
}

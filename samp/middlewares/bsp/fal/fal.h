/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FAL_H_
#define _FAL_H_

#include <fal_cfg.h>
#include "fal_def.h"

int fal_init(void);

const struct fal_flash_dev *fal_flash_device_find(const char *name);
const struct fal_partition *fal_partition_find(const char *name);
const struct fal_partition *fal_get_partition_table(size_t *len);
void fal_set_partition_table_temp(struct fal_partition *table, size_t len);

int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size);
int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size);
int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size);
int fal_partition_erase_all(const struct fal_partition *part);
void fal_show_part_table(void);

#endif /* _FAL_H_ */

/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _FAL_DEF_H_
#define _FAL_DEF_H_

#include <stdint.h>
#include <stdio.h>

#define FAL_SW_VERSION                 "0.5.99"

#ifndef FAL_MALLOC
#define FAL_MALLOC                     malloc
#endif

#ifndef FAL_CALLOC
#define FAL_CALLOC                     calloc
#endif

#ifndef FAL_REALLOC
#define FAL_REALLOC                    realloc
#endif

#ifndef FAL_FREE
#define FAL_FREE                       free
#endif

#ifndef FAL_PRINTF
#define FAL_PRINTF                     printf
#endif

#ifndef FAL_DEBUG
#define FAL_DEBUG                      0
#endif

#if FAL_DEBUG
#ifdef assert
#undef assert
#endif
#define assert(EXPR)                                                           \
if (!(EXPR))                                                                   \
{                                                                              \
    FAL_PRINTF("(%s) has assert failed at %s.\n", #EXPR, __func__ );        \
    while (1);                                                                 \
}

#ifdef  log_d
#undef  log_d
#endif
#include <inttypes.h>
#define log_d(...)                     FAL_PRINTF("[D/FAL] (%s:%" PRIdLEAST16 ") ", __func__, __LINE__);           FAL_PRINTF(__VA_ARGS__);FAL_PRINTF("\n")

#else

#ifdef assert
#undef assert
#endif
#define assert(EXPR)                   ((void)0);

#ifdef  log_d
#undef  log_d
#endif
#define log_d(...)
#endif /* FAL_DEBUG */

#ifdef  log_e
#undef  log_e
#endif
#define log_e(...)                     FAL_PRINTF("\033[31;22m[E/FAL] (%s:%d) ", __func__, __LINE__);FAL_PRINTF(__VA_ARGS__);FAL_PRINTF("\033[0m\n")

#ifdef  log_i
#undef  log_i
#endif
#if FAL_DEBUG
#define log_i(...)                     FAL_PRINTF("\033[32;22m[I/FAL] ");                                FAL_PRINTF(__VA_ARGS__);FAL_PRINTF("\033[0m\n")
#else
#define log_i(...)
#endif

#ifndef FAL_DEV_NAME_MAX
#define FAL_DEV_NAME_MAX 24
#endif

struct fal_flash_dev
{
    char name[FAL_DEV_NAME_MAX];
    uint32_t addr;
    size_t len;
    size_t blk_size;

    struct
    {
        int (*init)(void);
        int (*read)(long offset, uint8_t *buf, size_t size);
        int (*write)(long offset, const uint8_t *buf, size_t size);
        int (*erase)(long offset, size_t size);
    } ops;

    size_t write_gran;
};
typedef struct fal_flash_dev *fal_flash_dev_t;

struct fal_partition
{
    uint32_t magic_word;
    char name[FAL_DEV_NAME_MAX];
    char flash_name[FAL_DEV_NAME_MAX];
    long offset;
    size_t len;
    uint32_t reserved;
};
typedef struct fal_partition *fal_partition_t;

#endif /* _FAL_DEF_H_ */

/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief configuration file
 */

#pragma once

/* FlashDB 配置：使用 FAL + KVDB */

/* 使能 FAL 存储模式（FlashDB 期望的宏名） */
#define FDB_USING_FAL_MODE   1

/* 使能 KVDB */
#define FDB_USING_KVDB       1

/* 启用 TSDB */
#define FDB_USING_TSDB    1

/* 写入粒度（位）。片内 Flash 通常按半字/字对齐，这里使用 32bit 更稳妥。 */
#ifndef FDB_WRITE_GRAN
#define FDB_WRITE_GRAN       32
#endif

/* TSDB 实例：使用 fal 分区 "fdb_tsdb"，在 freertos_app.c 中直接传字面量 */

/* MCU Endian Configuration, default is Little Endian Order. */
/* #define FDB_BIG_ENDIAN */ 

/* log print macro. default EF_PRINT macro is printf() */
/* #define FDB_PRINT(...)              my_printf(__VA_ARGS__) */

/* 关闭FlashDB调试信息输出，仅保留错误日志 */
#undef FDB_DEBUG_ENABLE
/* 关闭FlashDB Info级打印，避免大量TSDB_ITER_DEBUG */
#define FDB_INFO_SILENT

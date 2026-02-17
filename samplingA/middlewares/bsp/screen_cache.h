/**
 * @file screen_cache.h
 * @brief 屏幕分发器缓存机制 - KVDB/TSDB异步写入支持
 * @note 用于解决屏幕分发器中KVDB/TSDB写入阻塞问题
 */

#ifndef SCREEN_CACHE_H
#define SCREEN_CACHE_H

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 屏幕命令派发 ================= */

/* 命令类型枚举 */
typedef enum {
    SCMD_NONE = 0,
    SCMD_MANUAL_SAMPLING,           /* 手动采样 */
    SCMD_MANUAL_DELIVERY,           /* 手动送样(AB桶) */
    SCMD_MANUAL_INSTANT_DELIVERY,   /* 手动瞬时送样 */
    SCMD_MANUAL_RETENTION,          /* 手动留样 */
    SCMD_MANUAL_INSTANT_RETENTION,  /* 手动瞬时留样 */
    SCMD_BOTTLE_RESET,              /* 瓶盘复位 */
    SCMD_TSDB_FORMAT,               /* TSDB格式化 */
    SCMD_SYSTEM_RESET,              /* 系统复位(NVIC软复位) */
    SCMD_SYSTEM_RESET_FSM,          /* 系统复位状态机(排空+瓶盘复位+进入待机) */
} ScreenCmdType;

/* 命令消息结构（携带参数快照，避免竞争） */
typedef struct {
    ScreenCmdType type;
    uint8_t bucket_or_mode;     /* 采样桶(1/2) / 送样模式(0/1/2) / 留样模式(0/1/2) */
    uint16_t volume;            /* 采样量/送样量/留样量 */
    uint8_t bottle_number;      /* 留样瓶号 */
} ScreenCommand;

/* 命令队列句柄 (在freertos_app.c中定义) */
extern QueueHandle_t queue_screen_cmd;

/* ================= KVDB 缓存机制 ================= */

/* KVDB配置类型枚举 */
typedef enum {
    KVDB_CACHE_SAMPLE = 0,
    KVDB_CACHE_DELIVERY,
    KVDB_CACHE_RETAIN,
    KVDB_CACHE_RETAIN_STATE,
    KVDB_CACHE_COMM,
    KVDB_CACHE_SYSTEM,
    KVDB_CACHE_CALIB,
    KVDB_CACHE_MAX
} KvdbCacheType;

/* KVDB缓存接口 */
void kvdb_cache_init(void);
void kvdb_cache_mark_dirty(KvdbCacheType type);
void kvdb_cache_flush_all(void);       /* 刷写所有脏数据到Flash */
uint8_t kvdb_cache_has_dirty(void);    /* 检查是否有待写入数据 */

/* ================= TSDB 缓存机制 ================= */

#define TSDB_CACHE_SIZE  32  /* 环形缓冲区大小 */

/* TSDB事件缓存条目 */
typedef struct {
    uint16_t event_type;
    uint8_t  body[16];     /* 最大16字节事件体 */
    uint8_t  body_len;
} TsdbCacheEntry;

/* TSDB缓存接口 */
void tsdb_cache_init(void);
uint8_t tsdb_cache_append(uint16_t event_type, const void *body, size_t body_len);
void tsdb_cache_flush_all(void);       /* 刷写所有缓存到TSDB */
uint8_t tsdb_cache_count(void);        /* 返回缓存中的条目数 */

/* ================= Task10 接口 ================= */

void task10_func(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* SCREEN_CACHE_H */


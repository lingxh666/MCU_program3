/**
 * @file    app_screen_cache.c
 * @brief   KVDB脏标记缓存 + TSDB环形缓冲区缓存
 *          减少高频写入对Flash的磨损，由屏幕任务定期flush
 */
#include "app_screen_cache.h"
#include "app_config.h"
#include "flashDB/app_flashdb.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "at32f435_437_wdt.h"
#include <string.h>
#include <stdio.h>

/* ======================== KVDB 脏标记缓存 ======================== */

static uint8_t s_kvdb_dirty[KVDB_CACHE_MAX];

void kvdb_cache_init(void)
{
    memset(s_kvdb_dirty, 0, sizeof(s_kvdb_dirty));
    printf("[KVDB-Cache] Init done\r\n");
}

void kvdb_cache_mark_dirty(kvdb_cache_type_t type)
{
    if (type < KVDB_CACHE_MAX) {
        s_kvdb_dirty[type] = 1;
    }
}

uint8_t kvdb_cache_has_dirty(void)
{
    for (uint8_t i = 0; i < KVDB_CACHE_MAX; i++) {
        if (s_kvdb_dirty[i]) return 1;
    }
    return 0;
}

/* S1-4完成后取消此宏，启用system/calib/retain_state的flush */
#define SCREEN_CACHE_FULL_CONFIG

void kvdb_cache_flush_all(void)
{
    if (s_kvdb_dirty[KVDB_CACHE_SAMPLE]) {
        if (cfg_save_sample(&g_sampling_cfg)) {
            s_kvdb_dirty[KVDB_CACHE_SAMPLE] = 0;
            printf("[KVDB-Cache] Sample config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_DELIVERY]) {
        if (cfg_save_delivery(&g_delivery_cfg)) {
            s_kvdb_dirty[KVDB_CACHE_DELIVERY] = 0;
            printf("[KVDB-Cache] Delivery config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_RETAIN]) {
        if (cfg_save_retain(&g_retain_cfg)) {
            s_kvdb_dirty[KVDB_CACHE_RETAIN] = 0;
            printf("[KVDB-Cache] Retain config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_COMM]) {
        if (cfg_save_comm(&g_comm_cfg)) {
            s_kvdb_dirty[KVDB_CACHE_COMM] = 0;
            printf("[KVDB-Cache] Comm config flushed\r\n");
        }
    }
#ifdef SCREEN_CACHE_FULL_CONFIG
    if (s_kvdb_dirty[KVDB_CACHE_RETAIN_STATE]) {
        if (cfg_save_retain_state(&g_retain_bottle_state)) {
            s_kvdb_dirty[KVDB_CACHE_RETAIN_STATE] = 0;
            printf("[KVDB-Cache] Bottle state flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_SYSTEM]) {
        if (cfg_save_system(&g_system_setting_cfg)) {
            s_kvdb_dirty[KVDB_CACHE_SYSTEM] = 0;
            printf("[KVDB-Cache] System config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_CALIB]) {
        if (cfg_save_calib(&g_calib_params)) {
            s_kvdb_dirty[KVDB_CACHE_CALIB] = 0;
            printf("[KVDB-Cache] Calib params flushed\r\n");
        }
    }
#endif
}

/* ======================== TSDB 环形缓冲区缓存 ======================== */

#define TSDB_CACHE_SIZE  32

static tsdb_cache_entry_t s_tsdb_cache[TSDB_CACHE_SIZE];
static volatile uint8_t   s_tsdb_head = 0;
static volatile uint8_t   s_tsdb_tail = 0;
static SemaphoreHandle_t  s_tsdb_cache_mtx = NULL;

void tsdb_cache_init(void)
{
    s_tsdb_head = 0;
    s_tsdb_tail = 0;
    memset(s_tsdb_cache, 0, sizeof(s_tsdb_cache));
    if (s_tsdb_cache_mtx == NULL) {
        s_tsdb_cache_mtx = xSemaphoreCreateMutex();
    }
    printf("[TSDB-Cache] Init done, size=%d\r\n", TSDB_CACHE_SIZE);
}

uint8_t tsdb_cache_append(uint16_t event_type, const void *body, size_t len)
{
    if (len > 16) len = 16;

    /* 快速加锁，最多等1ms */
    if (s_tsdb_cache_mtx && xSemaphoreTake(s_tsdb_cache_mtx, pdMS_TO_TICKS(1)) != pdTRUE) {
        printf("[TSDB-Cache] Lock busy, drop event type=0x%04X\r\n", event_type);
        return 0;
    }

    uint8_t next_tail = (s_tsdb_tail + 1) % TSDB_CACHE_SIZE;
    if (next_tail == s_tsdb_head) {
        /* 缓冲区满，覆盖最旧条目 */
        s_tsdb_head = (s_tsdb_head + 1) % TSDB_CACHE_SIZE;
        printf("[TSDB-Cache] Buffer full, overwrite oldest\r\n");
    }

    tsdb_cache_entry_t *entry = &s_tsdb_cache[s_tsdb_tail];
    entry->event_type = event_type;
    entry->body_len   = (uint8_t)len;
    if (body && len > 0) {
        memcpy(entry->body, body, len);
    }
    s_tsdb_tail = next_tail;

    if (s_tsdb_cache_mtx) {
        xSemaphoreGive(s_tsdb_cache_mtx);
    }
    return 1;
}

void tsdb_cache_flush_all(void)
{
    uint8_t flushed = 0;

    while (s_tsdb_head != s_tsdb_tail) {
        tsdb_cache_entry_t *entry = &s_tsdb_cache[s_tsdb_head];

        /* 写入TSDB */
        tsdb_event_append(entry->event_type, entry->body, entry->body_len);

        s_tsdb_head = (s_tsdb_head + 1) % TSDB_CACHE_SIZE;
        flushed++;

        /* 每5条喂一次看门狗 */
        if (flushed % 5 == 0) {
            wdt_counter_reload();
        }
    }

    if (flushed > 0) {
        printf("[TSDB-Cache] Flushed %u events\r\n", flushed);
    }
}

uint8_t tsdb_cache_count(void)
{
    if (s_tsdb_tail >= s_tsdb_head) {
        return s_tsdb_tail - s_tsdb_head;
    }
    return TSDB_CACHE_SIZE - s_tsdb_head + s_tsdb_tail;
}

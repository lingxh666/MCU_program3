/**
 * @file screen_cache.c
 * @brief Screen dispatcher cache mechanism implementation
 */

#include "screen_cache.h"
#include "freertos_app.h"    /* extern global variables */
#include "app_flashdb.h"
#include "work.h"            /* g_RetainBottleState */
#include "at32f403a_407_wdt.h"  /* wdt_counter_reload */
#include <string.h>
#include <stdio.h>

/* ================= KVDB Cache Implementation ================= */

/* KVDB dirty flag array */
static volatile uint8_t s_kvdb_dirty[KVDB_CACHE_MAX] = {0};

void kvdb_cache_init(void)
{
    memset((void*)s_kvdb_dirty, 0, sizeof(s_kvdb_dirty));
}

void kvdb_cache_mark_dirty(KvdbCacheType type)
{
    if (type < KVDB_CACHE_MAX) {
        s_kvdb_dirty[type] = 1;
    }
}

uint8_t kvdb_cache_has_dirty(void)
{
    for (int i = 0; i < KVDB_CACHE_MAX; i++) {
        if (s_kvdb_dirty[i]) return 1;
    }
    return 0;
}

void kvdb_cache_flush_all(void)
{
    /* Check and flush each dirty config */
    if (s_kvdb_dirty[KVDB_CACHE_SAMPLE]) {
        if (cfg_save_sample(&g_SampleConfig)) {
            s_kvdb_dirty[KVDB_CACHE_SAMPLE] = 0;
            printf("[KVDB-Cache] Sample config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_DELIVERY]) {
        if (cfg_save_delivery(&g_DeliveryConfig)) {
            s_kvdb_dirty[KVDB_CACHE_DELIVERY] = 0;
            printf("[KVDB-Cache] Delivery config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_RETAIN]) {
        if (cfg_save_retain(&g_RetainSampleConfig)) {
            s_kvdb_dirty[KVDB_CACHE_RETAIN] = 0;
            printf("[KVDB-Cache] Retain config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_RETAIN_STATE]) {
        if (cfg_save_retain_state(&g_RetainBottleState)) {
            s_kvdb_dirty[KVDB_CACHE_RETAIN_STATE] = 0;
            printf("[KVDB-Cache] Bottle state flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_COMM]) {
        if (cfg_save_comm(&g_CommSettingConfig)) {
            s_kvdb_dirty[KVDB_CACHE_COMM] = 0;
            printf("[KVDB-Cache] Comm config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_SYSTEM]) {
        if (cfg_save_system(&g_SystemSettingConfig)) {
            s_kvdb_dirty[KVDB_CACHE_SYSTEM] = 0;
            printf("[KVDB-Cache] System config flushed\r\n");
        }
    }
    if (s_kvdb_dirty[KVDB_CACHE_CALIB]) {
        if (cfg_save_calib(&g_CalibrationParams)) {
            s_kvdb_dirty[KVDB_CACHE_CALIB] = 0;
            printf("[KVDB-Cache] Calib params flushed\r\n");
        }
    }
}

/* ================= TSDB Cache Implementation ================= */

static TsdbCacheEntry s_tsdb_cache[TSDB_CACHE_SIZE];
static volatile uint8_t s_tsdb_head = 0;  /* read position */
static volatile uint8_t s_tsdb_tail = 0;  /* write position */
static SemaphoreHandle_t s_tsdb_cache_mtx = NULL;

void tsdb_cache_init(void)
{
    s_tsdb_head = 0;
    s_tsdb_tail = 0;
    memset(s_tsdb_cache, 0, sizeof(s_tsdb_cache));
    if (s_tsdb_cache_mtx == NULL) {
        s_tsdb_cache_mtx = xSemaphoreCreateMutex();
    }
}

uint8_t tsdb_cache_count(void)
{
    uint8_t h = s_tsdb_head;
    uint8_t t = s_tsdb_tail;
    return (t >= h) ? (t - h) : (TSDB_CACHE_SIZE - h + t);
}

uint8_t tsdb_cache_append(uint16_t event_type, const void *body, size_t body_len)
{
    if (body_len > 16) body_len = 16;  /* truncate */
    
    /* Quick lock (max 1ms wait) */
    if (s_tsdb_cache_mtx && xSemaphoreTake(s_tsdb_cache_mtx, pdMS_TO_TICKS(1)) != pdTRUE) {
        printf("[TSDB-Cache] Lock busy, drop event type=0x%04X\r\n", event_type);
        return 0;
    }
    
    /* Check if full */
    uint8_t next_tail = (s_tsdb_tail + 1) % TSDB_CACHE_SIZE;
    if (next_tail == s_tsdb_head) {
        /* Buffer full, overwrite oldest */
        s_tsdb_head = (s_tsdb_head + 1) % TSDB_CACHE_SIZE;
        printf("[TSDB-Cache] Buffer full, overwrite oldest\r\n");
    }
    
    /* Write to cache */
    TsdbCacheEntry *entry = &s_tsdb_cache[s_tsdb_tail];
    entry->event_type = event_type;
    entry->body_len = (uint8_t)body_len;
    if (body && body_len > 0) {
        memcpy(entry->body, body, body_len);
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

    /* 获取互斥锁保护 */
    if (s_tsdb_cache_mtx && xSemaphoreTake(s_tsdb_cache_mtx, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("[TSDB-Cache] Flush lock timeout\r\n");
        return;
    }

    while (s_tsdb_head != s_tsdb_tail) {
        TsdbCacheEntry *entry = &s_tsdb_cache[s_tsdb_head];

        /* Actually write to TSDB */
        uint8_t result = tsdb_event_append(entry->event_type, entry->body, entry->body_len);

        if (result == 0) {
            /* 写入失败，停止flush，保留数据下次重试 */
            printf("[TSDB-Cache] Write failed, stop flush\r\n");
            break;
        }

        s_tsdb_head = (s_tsdb_head + 1) % TSDB_CACHE_SIZE;
        flushed++;

        /* Feed watchdog every 5 entries */
        if (flushed % 5 == 0) {
            wdt_counter_reload();
        }
    }

    if (s_tsdb_cache_mtx) {
        xSemaphoreGive(s_tsdb_cache_mtx);
    }

    if (flushed > 0) {
        printf("[TSDB-Cache] Flushed %u events\r\n", flushed);
    }
}

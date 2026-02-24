#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "app_flashdb.h"
#include "fal.h"
#include "fdb_low_lvl.h"

#include "at32f435_437.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static void feed_dog(void)
{
    wdt_counter_reload();
}

/* ================= Internal objects ================= */
static struct fdb_kvdb g_kvdb_obj;
static fdb_kvdb_t g_kvdb = &g_kvdb_obj;

static struct fdb_tsdb g_tsdb_obj;
static fdb_tsdb_t g_tsdb = &g_tsdb_obj;

static fdb_time_t g_tsdb_prev_time = 0;
static volatile uint8_t g_tsdb_ready = 0;

static SemaphoreHandle_t g_kvdb_mutex = NULL;
static SemaphoreHandle_t g_tsdb_mutex = NULL;

/* ================= Timestamp callback (RTC-based) ================= */
static fdb_time_t tsdb_time_cb(void)
{
    ertc_time_type time;
    ertc_calendar_get(&time);

    /* 简单秒计数: 年月日时分秒 → 累计秒数(非精确Unix, 仅用于TSDB排序) */
    uint32_t days = (uint32_t)time.year * 365u + (uint32_t)(time.month - 1) * 30u + (uint32_t)time.day;
    fdb_time_t now_ts = days * 86400u + (uint32_t)time.hour * 3600u + (uint32_t)time.min * 60u + (uint32_t)time.sec;

    if (now_ts <= g_tsdb_prev_time)
        now_ts = g_tsdb_prev_time + 1;

    g_tsdb_prev_time = now_ts;
    return now_ts;
}

/* ================= KVDB Mutex ================= */
void kvdb_lock(void)
{
    if (g_kvdb_mutex)
        xSemaphoreTake(g_kvdb_mutex, pdMS_TO_TICKS(5000));
}

void kvdb_unlock(void)
{
    if (g_kvdb_mutex)
        xSemaphoreGive(g_kvdb_mutex);
}

/* ================= Blob helpers ================= */
static uint8_t kv_set_blob(const char *key, const void *data, size_t len)
{
    struct fdb_blob blob;
    return (fdb_kv_set_blob(g_kvdb, key, fdb_blob_make(&blob, data, len)) == FDB_NO_ERR) ? 1u : 0u;
}

static uint8_t kv_get_blob(const char *key, void *data, size_t len)
{
    struct fdb_blob blob;
    size_t rd = fdb_kv_get_blob(g_kvdb, key, fdb_blob_make(&blob, data, len));
    return (rd == len) ? 1u : 0u;
}

/* ================= KV key names ================= */
#define KV_SAMPLE       "sample_cfg"
#define KV_DELIVERY     "delivery_cfg"
#define KV_RETAIN       "retain_cfg"
#define KV_COMM         "comm_cfg"
#define KV_SYSTEM       "system_cfg"
#define KV_CALIB        "calib_params"
#define KV_RETAIN_STATE "retain_state"

/* ================= KVDB default KV table (file scope for ARM V5) ================= */
static struct fdb_default_kv_node g_kv_nodes[] = {
    {KV_SAMPLE,   NULL, 0},
    {KV_DELIVERY, NULL, 0},
    {KV_RETAIN,   NULL, 0},
    {KV_COMM,     NULL, 0},
    {KV_SYSTEM,   NULL, 0},
    {KV_CALIB,    NULL, 0},
};
static struct fdb_default_kv g_kv_def = {g_kv_nodes, sizeof(g_kv_nodes) / sizeof(g_kv_nodes[0])};

/* ================= KVDB Init ================= */
uint8_t cfg_kv_init(void)
{
    if (fal_init() <= 0)
        return 0u;

    g_kvdb_mutex = xSemaphoreCreateMutex();
    g_tsdb_mutex = xSemaphoreCreateMutex();

    return (fdb_kvdb_init(g_kvdb, "kvdb", "fdb_kvdb", &g_kv_def, NULL) == FDB_NO_ERR) ? 1u : 0u;
}

/* ================= Config Save/Load (locked blob wrappers) ================= */
static uint8_t cfg_save(const char *key, const void *p, size_t len)
{
    kvdb_lock();
    uint8_t r = kv_set_blob(key, p, len);
    kvdb_unlock();
    return r;
}

static uint8_t cfg_load(const char *key, void *p, size_t len)
{
    kvdb_lock();
    uint8_t r = kv_get_blob(key, p, len);
    kvdb_unlock();
    return r;
}

uint8_t cfg_save_sample(const void *p)       { return cfg_save(KV_SAMPLE,   p, 32); }
uint8_t cfg_load_sample(void *p)             { return cfg_load(KV_SAMPLE,   p, 32); }
uint8_t cfg_save_delivery(const void *p)     { return cfg_save(KV_DELIVERY, p, 64); }
uint8_t cfg_load_delivery(void *p)           { return cfg_load(KV_DELIVERY, p, 64); }
uint8_t cfg_save_retain(const void *p)       { return cfg_save(KV_RETAIN,   p, 32); }
uint8_t cfg_load_retain(void *p)             { return cfg_load(KV_RETAIN,   p, 32); }
uint8_t cfg_save_comm(const void *p)         { return cfg_save(KV_COMM,     p, 64); }
uint8_t cfg_load_comm(void *p)               { return cfg_load(KV_COMM,     p, 64); }
uint8_t cfg_save_system(const void *p)       { return cfg_save(KV_SYSTEM,   p, 32); }
uint8_t cfg_load_system(void *p)             { return cfg_load(KV_SYSTEM,   p, 32); }
uint8_t cfg_save_calib(const void *p)        { return cfg_save(KV_CALIB,    p, 64); }
uint8_t cfg_load_calib(void *p)              { return cfg_load(KV_CALIB,    p, 64); }
uint8_t cfg_save_retain_state(const void *p) { return cfg_save(KV_RETAIN_STATE, p, sizeof(RetainBottleState)); }
uint8_t cfg_load_retain_state(void *p)       { return cfg_load(KV_RETAIN_STATE, p, sizeof(RetainBottleState)); }

uint8_t cfg_reset_all(void)
{
    kvdb_lock();
    fdb_kv_set_default(g_kvdb);
    kvdb_unlock();
    return 1;
}

/* ================= TSDB: Event Append ================= */
uint8_t tsdb_event_append(uint16_t event_type, const void *body, size_t body_len)
{
    if (!g_tsdb_ready) return 0;

    /* Pack: 2 bytes event_type + body */
    uint8_t buf[256];
    if (body_len + 2 > sizeof(buf)) return 0;

    buf[0] = (uint8_t)(event_type & 0xFF);
    buf[1] = (uint8_t)(event_type >> 8);
    if (body_len > 0)
        memcpy(&buf[2], body, body_len);

    struct fdb_blob blob;
    fdb_blob_make(&blob, buf, body_len + 2);

    if (g_tsdb_mutex)
        xSemaphoreTake(g_tsdb_mutex, pdMS_TO_TICKS(5000));

    fdb_err_t err = fdb_tsl_append(g_tsdb, &blob);

    if (g_tsdb_mutex)
        xSemaphoreGive(g_tsdb_mutex);

    return (err == FDB_NO_ERR) ? 1u : 0u;
}

/* ================= TSDB: Iterate by time range ================= */
typedef struct {
    tsdb_event_iter_cb user_cb;
    void *user_arg;
} iter_ctx_t;

static bool tsdb_iter_cb_wrapper(fdb_tsl_t tsl, void *arg)
{
    iter_ctx_t *ctx = (iter_ctx_t *)arg;
    struct fdb_blob blob;
    uint8_t buf[256];

    fdb_blob_make(&blob, buf, sizeof(buf));
    if (fdb_blob_read((fdb_db_t)g_tsdb, fdb_tsl_to_blob(tsl, &blob)) < 2)
        return false;

    TsdbEventInfo info;
    info.event_type = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    info.ts = tsl->time;
    info.body_len = blob.saved.len - 2;

    ctx->user_cb(&info, &buf[2], ctx->user_arg);
    feed_dog();
    return false; /* continue iteration */
}

void tsdb_iter_range(fdb_time_t from, fdb_time_t to, tsdb_event_iter_cb cb, void *user)
{
    if (!g_tsdb_ready || !cb) return;

    iter_ctx_t ctx = { .user_cb = cb, .user_arg = user };

    if (g_tsdb_mutex)
        xSemaphoreTake(g_tsdb_mutex, pdMS_TO_TICKS(10000));

    fdb_tsl_iter_by_time(g_tsdb, from, to, tsdb_iter_cb_wrapper, &ctx);

    if (g_tsdb_mutex)
        xSemaphoreGive(g_tsdb_mutex);
}

/* ================= TSDB Status ================= */
uint8_t tsdb_is_ready(void)
{
    return g_tsdb_ready;
}

/* ================= Unified Init ================= */
void settings_init_load(void)
{
    if (!cfg_kv_init())
    {
        printf("[FDB] KVDB init failed!\r\n");
        return;
    }
    printf("[FDB] KVDB init OK\r\n");
}

void fdb_start_tasks(void)
{
    /* Initialize TSDB on external QSPI flash */
    fdb_err_t err = fdb_tsdb_init(g_tsdb, "tsdb", "fdb_tsdb", tsdb_time_cb, 256, NULL);
    if (err == FDB_NO_ERR)
    {
        g_tsdb_ready = 1;
        printf("[FDB] TSDB init OK\r\n");
    }
    else
    {
        printf("[FDB] TSDB init failed: %d\r\n", err);
    }
}

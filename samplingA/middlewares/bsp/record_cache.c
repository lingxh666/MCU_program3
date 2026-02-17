/**
 * @file record_cache.c
 * @brief 记录缓存管理器实现
 */

#include "record_cache.h"
#include <string.h>
#include <stdio.h>
#include "task.h"
#include "flashDB/app_flashdb.h"

// 外部函数声明
extern uint32_t rtc_counter_get(void);

/* 1970-01-01到2000-01-01的秒数：30年+7个闰日 */
#define UNIX_OFFSET_2000 ((30UL * 365UL + 7UL) * 24UL * 3600UL)

static inline uint32_t normalize_unix_ts(uint32_t ts)
{
    /* 兼容旧固件：历史记录可能使用“2000年基准秒数”，统一转换为Unix秒 */
    return (ts > 0 && ts < UNIX_OFFSET_2000) ? (ts + UNIX_OFFSET_2000) : ts;
}

//==============================================================================
// 全局变量
//==============================================================================

RecordCacheManager g_cache_mgr = {0};

//==============================================================================
// TSDB加载辅助结构和回调
//==============================================================================

typedef struct {
    uint16_t count;           // 0-1: 已加载数量
    uint16_t target;          // 2-3: 目标数量（回调用）
    uint8_t reserved[24];     // 4-27: 填充
    uint32_t target_count;    // 28-31: TSDB提前终止用
} LoadContext;

/**
 * @brief 采样记录加载回调（收集START和COMPLETE记录，通过sample_id配对）
 */
static void _load_sampling_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    LoadContext *ctx = (LoadContext *)user;
    if (!ctx || !info || !body) return;
    if (ctx->count >= ctx->target) return;

    // 处理采样完成记录
    if (info->event_type == LOG_SAMPLING_COMPLETE) {
        if (info->body_len < sizeof(SamplingCompleteRecord)) return;

        uint16_t idx = ctx->count;
        SamplingCompleteRecord complete = *(const SamplingCompleteRecord *)body;
        complete.end_time = normalize_unix_ts(complete.end_time);

        // 填充complete记录
        g_cache_mgr.sampling.complete_records[idx] = complete;

        // 初始化start记录（等待配对）
        SamplingStartRecord *start = &g_cache_mgr.sampling.start_records[idx];
        memset(start, 0, sizeof(SamplingStartRecord));
        memcpy(start->sample_id, complete.sample_id, sizeof(start->sample_id));
        start->start_time = complete.end_time;

        ctx->count++;
    }
    // 处理采样开始记录（查找配对的complete记录并填充bucket_id等字段）
    else if (info->event_type == LOG_SAMPLING_START) {
        if (info->body_len < sizeof(SamplingStartRecord)) return;

        const SamplingStartRecord *start_src = (const SamplingStartRecord *)body;

        // 查找匹配的complete记录（通过sample_id）
        for (uint16_t i = 0; i < ctx->count; i++) {
            SamplingStartRecord *start_dst = &g_cache_mgr.sampling.start_records[i];
            if (memcmp(start_dst->sample_id, start_src->sample_id, 18) == 0) {
                // 找到配对，复制bucket_id和sampling_mode
                start_dst->bucket_id = start_src->bucket_id;
                start_dst->sampling_mode = start_src->sampling_mode;
                start_dst->sequence = start_src->sequence;
                start_dst->target_volume = start_src->target_volume;
                start_dst->is_manual = start_src->is_manual;
                start_dst->start_time = normalize_unix_ts(start_src->start_time);
                break;
            }
        }
    }
}

/**
 * @brief 送样记录加载回调（收集START和COMPLETE记录，通过sample_id配对）
 */
static void _load_delivery_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    LoadContext *ctx = (LoadContext *)user;
    if (!ctx || !info || !body) return;
    if (ctx->count >= ctx->target) return;

    // 处理送样完成记录
    if (info->event_type == LOG_DELIVERY_COMPLETE) {
        if (info->body_len < sizeof(DeliveryCompleteRecord)) return;

        uint16_t idx = ctx->count;
        DeliveryCompleteRecord complete = *(const DeliveryCompleteRecord *)body;
        complete.end_time = normalize_unix_ts(complete.end_time);

        // 填充complete记录
        g_cache_mgr.delivery.complete_records[idx] = complete;

        // 初始化start记录（等待配对）
        DeliveryStartRecord *start = &g_cache_mgr.delivery.start_records[idx];
        memset(start, 0, sizeof(DeliveryStartRecord));
        memcpy(start->sample_id, complete.sample_id, sizeof(start->sample_id));
        start->start_time = complete.end_time;

        ctx->count++;
    }
    // 处理送样开始记录（查找配对的complete记录并填充bucket_id等字段）
    else if (info->event_type == LOG_DELIVERY_START) {
        if (info->body_len < sizeof(DeliveryStartRecord)) return;

        const DeliveryStartRecord *start_src = (const DeliveryStartRecord *)body;

        // 查找匹配的complete记录（通过sample_id）
        for (uint16_t i = 0; i < ctx->count; i++) {
            DeliveryStartRecord *start_dst = &g_cache_mgr.delivery.start_records[i];
            if (memcmp(start_dst->sample_id, start_src->sample_id, 18) == 0) {
                // 找到配对，复制bucket_id和delivery_mode
                start_dst->bucket_id = start_src->bucket_id;
                start_dst->delivery_mode = start_src->delivery_mode;
                start_dst->target_volume = start_src->target_volume;
                start_dst->is_manual = start_src->is_manual;
                start_dst->start_time = normalize_unix_ts(start_src->start_time);
                break;
            }
        }
    }
}

/**
 * @brief 留样记录加载回调
 */
static void _load_retain_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    LoadContext *ctx = (LoadContext *)user;
    if (!ctx || !info || !body) return;
    if (ctx->count >= ctx->target) return;

    if (info->event_type != LOG_RETAIN_RECORD) return;
    if (info->body_len < sizeof(RetainLogRecord)) return;

    uint16_t idx = ctx->count;
    RetainLogRecord record = *(const RetainLogRecord *)body;
    record.start_time = normalize_unix_ts(record.start_time);
    record.end_time = normalize_unix_ts(record.end_time);
    record.delivery_time = normalize_unix_ts(record.delivery_time);
    g_cache_mgr.retain.records[idx] = record;
    ctx->count++;
}

/**
 * @brief 电源记录加载回调
 */
static void _load_power_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    LoadContext *ctx = (LoadContext *)user;
    if (!ctx || !info || !body) return;
    if (ctx->count >= ctx->target) return;

    if (info->event_type != POWER_EVT_FAILURE_DETECTED &&
        info->event_type != POWER_EVT_RECOVERY_COMPLETE) return;

    uint16_t idx = ctx->count;
    g_cache_mgr.power.events[idx].event_type =
        (info->event_type == POWER_EVT_FAILURE_DETECTED) ? 0x0000 : 0x0001;
    g_cache_mgr.power.events[idx].timestamp = (uint32_t)info->ts;
    ctx->count++;
}

/**
 * @brief 门禁记录加载回调
 */
static void _load_door_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    LoadContext *ctx = (LoadContext *)user;
    if (!ctx || !info || !body) return;
    if (ctx->count >= ctx->target) return;

    if (info->event_type != DOOR_EVT_UNLOCKED &&
        info->event_type != DOOR_EVT_LOCKED) return;

    uint16_t idx = ctx->count;
    g_cache_mgr.door.events[idx].event_type =
        (info->event_type == DOOR_EVT_UNLOCKED) ? 0x0000 : 0x0001;
    g_cache_mgr.door.events[idx].timestamp = (uint32_t)info->ts;
    ctx->count++;
}

/**
 * @brief 从TSDB加载采样记录到缓存
 */
static uint8_t _load_sampling_from_tsdb(void)
{
    // 初始化缓存状态
    g_cache_mgr.sampling.count = 0;
    g_cache_mgr.sampling.window_start_idx = 0;
    g_cache_mgr.sampling.window_end_idx = 0;
    g_cache_mgr.sampling.oldest_ts = 0;
    g_cache_mgr.sampling.newest_ts = 0;
    g_cache_mgr.sampling.last_page = 0;
    g_cache_mgr.sampling.last_direction = 0;
    g_cache_mgr.sampling.is_full = 0;

    if (!tsdb_is_ready()) {
        return 0;
    }

    // 获取当前时间作为查询终点
    uint32_t now = rtc_counter_get();

    // 设置加载上下文
    LoadContext ctx = {0};
    ctx.target = CACHE_CAPACITY_SAMPLING;
    ctx.target_count = CACHE_CAPACITY_SAMPLING;

    // 反向遍历TSDB
    fdb_time_t last_ts = 0;
    tsdb_iter_reverse_until((fdb_time_t)now, _load_sampling_cb, &ctx, &last_ts);

    // 更新缓存元数据
    g_cache_mgr.sampling.count = ctx.count;
    if (ctx.count > 0) {
        // 反向遍历后，索引0是最新记录，需要反转使索引0为最旧记录
        // 反转start_records和complete_records数组
        for (uint16_t i = 0; i < ctx.count / 2; i++) {
            uint16_t j = ctx.count - 1 - i;
            // 交换start_records
            SamplingStartRecord tmp_start = g_cache_mgr.sampling.start_records[i];
            g_cache_mgr.sampling.start_records[i] = g_cache_mgr.sampling.start_records[j];
            g_cache_mgr.sampling.start_records[j] = tmp_start;
            // 交换complete_records
            SamplingCompleteRecord tmp_complete = g_cache_mgr.sampling.complete_records[i];
            g_cache_mgr.sampling.complete_records[i] = g_cache_mgr.sampling.complete_records[j];
            g_cache_mgr.sampling.complete_records[j] = tmp_complete;
        }
        // 更新时间戳（反转后索引0是最旧，索引count-1是最新）
        g_cache_mgr.sampling.oldest_ts = g_cache_mgr.sampling.complete_records[0].end_time;
        g_cache_mgr.sampling.newest_ts = g_cache_mgr.sampling.complete_records[ctx.count - 1].end_time;
    }

    return 1;
}

/**
 * @brief 从TSDB加载送样记录到缓存
 */
static uint8_t _load_delivery_from_tsdb(void)
{
    g_cache_mgr.delivery.count = 0;
    g_cache_mgr.delivery.window_start_idx = 0;
    g_cache_mgr.delivery.window_end_idx = 0;
    g_cache_mgr.delivery.oldest_ts = 0;
    g_cache_mgr.delivery.newest_ts = 0;
    g_cache_mgr.delivery.last_page = 0;
    g_cache_mgr.delivery.last_direction = 0;
    g_cache_mgr.delivery.is_full = 0;

    if (!tsdb_is_ready()) {
        return 0;
    }

    uint32_t now = rtc_counter_get();
    LoadContext ctx = {0};
    ctx.target = CACHE_CAPACITY_DELIVERY;
    ctx.target_count = CACHE_CAPACITY_DELIVERY;
    fdb_time_t last_ts = 0;

    tsdb_iter_reverse_until((fdb_time_t)now, _load_delivery_cb, &ctx, &last_ts);

    g_cache_mgr.delivery.count = ctx.count;
    if (ctx.count > 0) {
        for (uint16_t i = 0; i < ctx.count / 2; i++) {
            uint16_t j = ctx.count - 1 - i;
            DeliveryStartRecord tmp_start = g_cache_mgr.delivery.start_records[i];
            g_cache_mgr.delivery.start_records[i] = g_cache_mgr.delivery.start_records[j];
            g_cache_mgr.delivery.start_records[j] = tmp_start;
            DeliveryCompleteRecord tmp_complete = g_cache_mgr.delivery.complete_records[i];
            g_cache_mgr.delivery.complete_records[i] = g_cache_mgr.delivery.complete_records[j];
            g_cache_mgr.delivery.complete_records[j] = tmp_complete;
        }
        g_cache_mgr.delivery.oldest_ts = g_cache_mgr.delivery.complete_records[0].end_time;
        g_cache_mgr.delivery.newest_ts = g_cache_mgr.delivery.complete_records[ctx.count - 1].end_time;
    }

    return 1;
}

/**
 * @brief 从TSDB加载留样记录到缓存
 */
static uint8_t _load_retain_from_tsdb(void)
{
    g_cache_mgr.retain.count = 0;
    g_cache_mgr.retain.window_start_idx = 0;
    g_cache_mgr.retain.window_end_idx = 0;
    g_cache_mgr.retain.oldest_ts = 0;
    g_cache_mgr.retain.newest_ts = 0;
    g_cache_mgr.retain.last_page = 0;
    g_cache_mgr.retain.last_direction = 0;
    g_cache_mgr.retain.is_full = 0;

    if (!tsdb_is_ready()) {
        return 0;
    }

    uint32_t now = rtc_counter_get();
    LoadContext ctx = {0};
    ctx.target = CACHE_CAPACITY_RETAIN;
    ctx.target_count = CACHE_CAPACITY_RETAIN;
    fdb_time_t last_ts = 0;

    tsdb_iter_reverse_until((fdb_time_t)now, _load_retain_cb, &ctx, &last_ts);

    g_cache_mgr.retain.count = ctx.count;
    if (ctx.count > 0) {
        for (uint16_t i = 0; i < ctx.count / 2; i++) {
            uint16_t j = ctx.count - 1 - i;
            RetainLogRecord tmp = g_cache_mgr.retain.records[i];
            g_cache_mgr.retain.records[i] = g_cache_mgr.retain.records[j];
            g_cache_mgr.retain.records[j] = tmp;
        }
        g_cache_mgr.retain.oldest_ts = g_cache_mgr.retain.records[0].end_time;
        g_cache_mgr.retain.newest_ts = g_cache_mgr.retain.records[ctx.count - 1].end_time;
    }

    return 1;
}

/**
 * @brief 从TSDB加载电源记录到缓存
 */
static uint8_t _load_power_from_tsdb(void)
{
    g_cache_mgr.power.count = 0;
    g_cache_mgr.power.window_start_idx = 0;
    g_cache_mgr.power.window_end_idx = 0;
    g_cache_mgr.power.oldest_ts = 0;
    g_cache_mgr.power.newest_ts = 0;
    g_cache_mgr.power.last_page = 0;
    g_cache_mgr.power.last_direction = 0;
    g_cache_mgr.power.is_full = 0;

    if (!tsdb_is_ready()) {
        return 0;
    }

    uint32_t now = rtc_counter_get();
    LoadContext ctx = {0};
    ctx.target = CACHE_CAPACITY_POWER;
    ctx.target_count = CACHE_CAPACITY_POWER;
    fdb_time_t last_ts = 0;

    tsdb_iter_reverse_until((fdb_time_t)now, _load_power_cb, &ctx, &last_ts);

    g_cache_mgr.power.count = ctx.count;
    if (ctx.count > 0) {
        for (uint16_t i = 0; i < ctx.count / 2; i++) {
            uint16_t j = ctx.count - 1 - i;
            PowerEventCache_t tmp = g_cache_mgr.power.events[i];
            g_cache_mgr.power.events[i] = g_cache_mgr.power.events[j];
            g_cache_mgr.power.events[j] = tmp;
        }
        g_cache_mgr.power.oldest_ts = g_cache_mgr.power.events[0].timestamp;
        g_cache_mgr.power.newest_ts = g_cache_mgr.power.events[ctx.count - 1].timestamp;
    }

    return 1;
}

/**
 * @brief 从TSDB加载门禁记录到缓存
 */
static uint8_t _load_door_from_tsdb(void)
{
    g_cache_mgr.door.count = 0;
    g_cache_mgr.door.window_start_idx = 0;
    g_cache_mgr.door.window_end_idx = 0;
    g_cache_mgr.door.oldest_ts = 0;
    g_cache_mgr.door.newest_ts = 0;
    g_cache_mgr.door.last_page = 0;
    g_cache_mgr.door.last_direction = 0;
    g_cache_mgr.door.is_full = 0;

    if (!tsdb_is_ready()) {
        return 0;
    }

    uint32_t now = rtc_counter_get();
    LoadContext ctx = {0};
    ctx.target = CACHE_CAPACITY_DOOR;
    ctx.target_count = CACHE_CAPACITY_DOOR;
    fdb_time_t last_ts = 0;

    tsdb_iter_reverse_until((fdb_time_t)now, _load_door_cb, &ctx, &last_ts);

    g_cache_mgr.door.count = ctx.count;
    if (ctx.count > 0) {
        for (uint16_t i = 0; i < ctx.count / 2; i++) {
            uint16_t j = ctx.count - 1 - i;
            DoorEventCache_t tmp = g_cache_mgr.door.events[i];
            g_cache_mgr.door.events[i] = g_cache_mgr.door.events[j];
            g_cache_mgr.door.events[j] = tmp;
        }
        g_cache_mgr.door.oldest_ts = g_cache_mgr.door.events[0].timestamp;
        g_cache_mgr.door.newest_ts = g_cache_mgr.door.events[ctx.count - 1].timestamp;
    }

    return 1;
}

/**
 * @brief 初始化缓存管理器
 */
uint8_t cache_manager_init(void)
{
    if (g_cache_mgr.initialized)
    {
        return 1;
    }

    // 创建互斥锁
    g_cache_mgr.mutex = xSemaphoreCreateMutex();
    if (g_cache_mgr.mutex == NULL)
    {
        return 0;
    }

    // 从TSDB加载各类记录到缓存
    if (!_load_sampling_from_tsdb() ||
        !_load_delivery_from_tsdb() ||
        !_load_retain_from_tsdb() ||
        !_load_power_from_tsdb() ||
        !_load_door_from_tsdb())
    {
        printf("[CACHE] 初始化失败，TSDB未就绪或读取失败\r\n");
        vSemaphoreDelete(g_cache_mgr.mutex);
        return 0;
    }

    g_cache_mgr.initialized = 1;

    // 不补发历史数据，启动时设为当前时间，只发送新数据
    uint32_t now = rtc_counter_get();
    for (int i = 0; i < 5; i++) {
        g_cache_mgr.mqtt_last_sent_ts[i] = now;
    }
    printf("[MQTT] 初始化完成，只发送新数据 (now=%u)\r\n", (unsigned)now);

    return 1;
}

/**
 * @brief 反初始化缓存管理器
 */
void cache_manager_deinit(void)
{
    if (!g_cache_mgr.initialized)
        return;

    // 删除所有预加载任务
    if (g_cache_mgr.preload_task_sampling != NULL)
    {
        vTaskDelete(g_cache_mgr.preload_task_sampling);
        g_cache_mgr.preload_task_sampling = NULL;
    }
    if (g_cache_mgr.preload_task_delivery != NULL)
    {
        vTaskDelete(g_cache_mgr.preload_task_delivery);
        g_cache_mgr.preload_task_delivery = NULL;
    }
    if (g_cache_mgr.preload_task_retain != NULL)
    {
        vTaskDelete(g_cache_mgr.preload_task_retain);
        g_cache_mgr.preload_task_retain = NULL;
    }
    if (g_cache_mgr.preload_task_power != NULL)
    {
        vTaskDelete(g_cache_mgr.preload_task_power);
        g_cache_mgr.preload_task_power = NULL;
    }
    if (g_cache_mgr.preload_task_door != NULL)
    {
        vTaskDelete(g_cache_mgr.preload_task_door);
        g_cache_mgr.preload_task_door = NULL;
    }

    // 删除互斥锁
    if (g_cache_mgr.mutex != NULL)
    {
        vSemaphoreDelete(g_cache_mgr.mutex);
        g_cache_mgr.mutex = NULL;
    }

    // 清空所有缓存
    memset(&g_cache_mgr, 0, sizeof(RecordCacheManager));
}

//==============================================================================
// 添加记录接口（FIFO滑动窗口）
//==============================================================================

/**
 * @brief 添加采样记录到缓存
 */
void cache_add_sampling(const SamplingStartRecord *start,
                        const SamplingCompleteRecord *complete)
{
    if (!g_cache_mgr.initialized || start == NULL || complete == NULL)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    uint16_t idx;

    if (g_cache_mgr.sampling.count < CACHE_CAPACITY_SAMPLING)
    {
        // 缓存未满，直接追加
        idx = g_cache_mgr.sampling.count;
        g_cache_mgr.sampling.count++;

        if (g_cache_mgr.sampling.count == CACHE_CAPACITY_SAMPLING)
        {
            g_cache_mgr.sampling.is_full = 1;
        }
    }
    else
    {
        // 缓存已满，移除最旧记录（FIFO）
        // 后移所有记录（从index 0开始）
        memmove(&g_cache_mgr.sampling.start_records[0],
                &g_cache_mgr.sampling.start_records[1],
                (CACHE_CAPACITY_SAMPLING - 1) * sizeof(SamplingStartRecord));
        memmove(&g_cache_mgr.sampling.complete_records[0],
                &g_cache_mgr.sampling.complete_records[1],
                (CACHE_CAPACITY_SAMPLING - 1) * sizeof(SamplingCompleteRecord));

        // 在末尾添加新记录
        idx = CACHE_CAPACITY_SAMPLING - 1;

        // 更新最旧记录时间戳
        g_cache_mgr.sampling.oldest_ts = g_cache_mgr.sampling.complete_records[0].end_time;
    }

    // 复制新记录
    g_cache_mgr.sampling.start_records[idx] = *start;
    g_cache_mgr.sampling.complete_records[idx] = *complete;

    // 更新最新记录时间戳
    g_cache_mgr.sampling.newest_ts = complete->end_time;

    // 如果这是第一条记录，更新最旧时间戳
    if (g_cache_mgr.sampling.count == 1)
    {
        g_cache_mgr.sampling.oldest_ts = complete->end_time;
    }

    xSemaphoreGive(g_cache_mgr.mutex);

    // printf("[Cache] 采样记录已添加：sample_id=%s\r\n", start->sample_id);
}

/**
 * @brief 添加送样记录到缓存
 */
void cache_add_delivery(const DeliveryStartRecord *start,
                        const DeliveryCompleteRecord *complete)
{
    if (!g_cache_mgr.initialized || start == NULL || complete == NULL)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    uint16_t idx;

    if (g_cache_mgr.delivery.count < CACHE_CAPACITY_DELIVERY)
    {
        idx = g_cache_mgr.delivery.count;
        g_cache_mgr.delivery.count++;

        if (g_cache_mgr.delivery.count == CACHE_CAPACITY_DELIVERY)
        {
            g_cache_mgr.delivery.is_full = 1;
        }
    }
    else
    {
        memmove(&g_cache_mgr.delivery.start_records[0],
                &g_cache_mgr.delivery.start_records[1],
                (CACHE_CAPACITY_DELIVERY - 1) * sizeof(DeliveryStartRecord));
        memmove(&g_cache_mgr.delivery.complete_records[0],
                &g_cache_mgr.delivery.complete_records[1],
                (CACHE_CAPACITY_DELIVERY - 1) * sizeof(DeliveryCompleteRecord));

        idx = CACHE_CAPACITY_DELIVERY - 1;
        g_cache_mgr.delivery.oldest_ts = g_cache_mgr.delivery.complete_records[0].end_time;
    }

    g_cache_mgr.delivery.start_records[idx] = *start;
    g_cache_mgr.delivery.complete_records[idx] = *complete;
    g_cache_mgr.delivery.newest_ts = complete->end_time;

    if (g_cache_mgr.delivery.count == 1)
    {
        g_cache_mgr.delivery.oldest_ts = complete->end_time;
    }

    xSemaphoreGive(g_cache_mgr.mutex);
}

/**
 * @brief 添加留样记录到缓存
 */
void cache_add_retain(const RetainLogRecord *record)
{
    if (!g_cache_mgr.initialized || record == NULL)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    uint16_t idx;

    if (g_cache_mgr.retain.count < CACHE_CAPACITY_RETAIN)
    {
        idx = g_cache_mgr.retain.count;
        g_cache_mgr.retain.count++;

        if (g_cache_mgr.retain.count == CACHE_CAPACITY_RETAIN)
        {
            g_cache_mgr.retain.is_full = 1;
        }
    }
    else
    {
        memmove(&g_cache_mgr.retain.records[0],
                &g_cache_mgr.retain.records[1],
                (CACHE_CAPACITY_RETAIN - 1) * sizeof(RetainLogRecord));

        idx = CACHE_CAPACITY_RETAIN - 1;
        g_cache_mgr.retain.oldest_ts = g_cache_mgr.retain.records[0].end_time;
    }

    g_cache_mgr.retain.records[idx] = *record;
    g_cache_mgr.retain.newest_ts = record->end_time;

    if (g_cache_mgr.retain.count == 1)
    {
        g_cache_mgr.retain.oldest_ts = record->end_time;
    }

    xSemaphoreGive(g_cache_mgr.mutex);
}

/**
 * @brief 添加电源记录到缓存
 * @param event_type 事件类型
 * @param timestamp 事件时间戳
 */
void cache_add_power(uint16_t event_type, uint32_t timestamp)
{
    if (!g_cache_mgr.initialized)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    // ★ 转换event_type为串口屏期望的格式（与TSDB加载回调保持一致）
    uint16_t display_type = (event_type == POWER_EVT_FAILURE_DETECTED) ? 0x0000 : 0x0001;

    uint16_t idx;

    if (g_cache_mgr.power.count < CACHE_CAPACITY_POWER)
    {
        idx = g_cache_mgr.power.count;
        g_cache_mgr.power.count++;

        if (g_cache_mgr.power.count == CACHE_CAPACITY_POWER)
        {
            g_cache_mgr.power.is_full = 1;
        }
    }
    else
    {
        memmove(&g_cache_mgr.power.events[0],
                &g_cache_mgr.power.events[1],
                (CACHE_CAPACITY_POWER - 1) * sizeof(PowerEventCache_t));

        idx = CACHE_CAPACITY_POWER - 1;
        g_cache_mgr.power.oldest_ts = g_cache_mgr.power.events[0].timestamp;
    }

    g_cache_mgr.power.events[idx].event_type = display_type;  // 使用转换后的值
    g_cache_mgr.power.events[idx].timestamp = timestamp;
    g_cache_mgr.power.newest_ts = timestamp;

    if (g_cache_mgr.power.count == 1)
    {
        g_cache_mgr.power.oldest_ts = timestamp;
    }

    xSemaphoreGive(g_cache_mgr.mutex);
}

/**
 * @brief 添加门禁记录到缓存
 * @param event_type 事件类型
 * @param timestamp 事件时间戳
 */
void cache_add_door(uint16_t event_type, uint32_t timestamp)
{
    if (!g_cache_mgr.initialized)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return;
    }

    // ★ 转换event_type为串口屏期望的格式（与TSDB加载回调保持一致）
    uint16_t display_type = (event_type == DOOR_EVT_UNLOCKED) ? 0x0000 : 0x0001;

    uint16_t idx;
    uint16_t old_count = g_cache_mgr.door.count;

    if (g_cache_mgr.door.count < CACHE_CAPACITY_DOOR)
    {
        idx = g_cache_mgr.door.count;
        g_cache_mgr.door.count++;

        if (g_cache_mgr.door.count == CACHE_CAPACITY_DOOR)
        {
            g_cache_mgr.door.is_full = 1;
        }
    }
    else
    {
        memmove(&g_cache_mgr.door.events[0],
                &g_cache_mgr.door.events[1],
                (CACHE_CAPACITY_DOOR - 1) * sizeof(DoorEventCache_t));

        idx = CACHE_CAPACITY_DOOR - 1;
        g_cache_mgr.door.oldest_ts = g_cache_mgr.door.events[0].timestamp;
    }

    g_cache_mgr.door.events[idx].event_type = display_type;  // 使用转换后的值
    g_cache_mgr.door.events[idx].timestamp = timestamp;
    g_cache_mgr.door.newest_ts = timestamp;

    if (g_cache_mgr.door.count == 1)
    {
        g_cache_mgr.door.oldest_ts = timestamp;
    }

    printf("[DOOR_CACHE] Added: type=%s, ts=%u, idx=%u, count=%u->%u\r\n",
           (display_type == 0x0000) ? "OPEN" : "CLOSE",
           timestamp, idx, old_count, g_cache_mgr.door.count);

    xSemaphoreGive(g_cache_mgr.mutex);
}

//==============================================================================
// 查询接口
//==============================================================================

/**
 * @brief 查询采样记录
 */
uint8_t cache_query_sampling(uint8_t page, uint8_t per_page,
                              SamplingStartRecord *out_starts,
                              SamplingCompleteRecord *out_completes,
                              uint8_t *out_count)
{
    if (!g_cache_mgr.initialized || out_starts == NULL || out_completes == NULL || out_count == NULL)
        return 0;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 0;
    }

    uint16_t start_idx = page * per_page;
    uint16_t total = g_cache_mgr.sampling.count;

    if (start_idx >= total)
    {
        // 超出缓存范围
        xSemaphoreGive(g_cache_mgr.mutex);
        return 0;
    }

    // 计算实际可返回数量
    uint16_t end_idx = start_idx + per_page;
    uint16_t avail = (end_idx <= total) ? per_page : (total - start_idx);

    // 从缓存复制（最新记录在最后，反向索引）
    for (uint16_t i = 0; i < avail; i++)
    {
        uint16_t cache_idx = total - 1 - (start_idx + i);
        out_starts[i] = g_cache_mgr.sampling.start_records[cache_idx];
        out_completes[i] = g_cache_mgr.sampling.complete_records[cache_idx];
    }

    *out_count = (uint8_t)avail;

    // 更新翻页方向
    int8_t direction = 0;
    if (g_cache_mgr.sampling.last_page < page)
        direction = 1; // 向后翻
    else if (g_cache_mgr.sampling.last_page > page)
        direction = -1; // 向前翻

    g_cache_mgr.sampling.last_direction = direction;
    g_cache_mgr.sampling.last_page = page;

    xSemaphoreGive(g_cache_mgr.mutex);
    return 1; // 缓存命中
}

/**
 * @brief 查询送样记录
 */
uint8_t cache_query_delivery(uint8_t page, uint8_t per_page,
                              DeliveryStartRecord *out_starts,
                              DeliveryCompleteRecord *out_completes,
                              uint8_t *out_count)
{
    if (!g_cache_mgr.initialized || out_starts == NULL || out_completes == NULL || out_count == NULL)
        return 0;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 0;
    }

    uint16_t start_idx = page * per_page;
    uint16_t total = g_cache_mgr.delivery.count;

    if (start_idx >= total)
    {
        xSemaphoreGive(g_cache_mgr.mutex);
        return 0;
    }

    uint16_t end_idx = start_idx + per_page;
    uint16_t avail = (end_idx <= total) ? per_page : (total - start_idx);

    for (uint16_t i = 0; i < avail; i++)
    {
        uint16_t cache_idx = total - 1 - (start_idx + i);
        out_starts[i] = g_cache_mgr.delivery.start_records[cache_idx];
        out_completes[i] = g_cache_mgr.delivery.complete_records[cache_idx];
    }

    *out_count = (uint8_t)avail;

    int8_t direction = 0;
    if (g_cache_mgr.delivery.last_page < page)
        direction = 1;
    else if (g_cache_mgr.delivery.last_page > page)
        direction = -1;

    g_cache_mgr.delivery.last_direction = direction;
    g_cache_mgr.delivery.last_page = page;

    xSemaphoreGive(g_cache_mgr.mutex);
    return 1;
}

/**
 * @brief 查询留样记录
 */
uint8_t cache_query_retain(uint8_t page, uint8_t per_page,
                            RetainLogRecord *out_records,
                            uint8_t *out_count)
{
    if (!g_cache_mgr.initialized || out_records == NULL || out_count == NULL)
        return 0;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 0;
    }

    uint16_t start_idx = page * per_page;
    uint16_t total = g_cache_mgr.retain.count;

    if (start_idx >= total)
    {
        xSemaphoreGive(g_cache_mgr.mutex);
        return 0;
    }

    uint16_t end_idx = start_idx + per_page;
    uint16_t avail = (end_idx <= total) ? per_page : (total - start_idx);

    for (uint16_t i = 0; i < avail; i++)
    {
        uint16_t cache_idx = total - 1 - (start_idx + i);
        out_records[i] = g_cache_mgr.retain.records[cache_idx];
    }

    *out_count = (uint8_t)avail;

    int8_t direction = 0;
    if (g_cache_mgr.retain.last_page < page)
        direction = 1;
    else if (g_cache_mgr.retain.last_page > page)
        direction = -1;

    g_cache_mgr.retain.last_direction = direction;
    g_cache_mgr.retain.last_page = page;

    xSemaphoreGive(g_cache_mgr.mutex);
    return 1;
}

/**
 * @brief 查询电源记录
 */
uint8_t cache_query_power(uint8_t page, uint8_t per_page,
                           PowerEventCache_t *out_events,
                           uint8_t *out_count)
{
    if (!g_cache_mgr.initialized || out_events == NULL || out_count == NULL)
        return 0;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 0;
    }

    uint16_t start_idx = page * per_page;
    uint16_t total = g_cache_mgr.power.count;

    if (start_idx >= total)
    {
        xSemaphoreGive(g_cache_mgr.mutex);
        return 0;
    }

    uint16_t end_idx = start_idx + per_page;
    uint16_t avail = (end_idx <= total) ? per_page : (total - start_idx);

    for (uint16_t i = 0; i < avail; i++)
    {
        uint16_t cache_idx = total - 1 - (start_idx + i);
        out_events[i] = g_cache_mgr.power.events[cache_idx];
    }

    *out_count = (uint8_t)avail;

    int8_t direction = 0;
    if (g_cache_mgr.power.last_page < page)
        direction = 1;
    else if (g_cache_mgr.power.last_page > page)
        direction = -1;

    g_cache_mgr.power.last_direction = direction;
    g_cache_mgr.power.last_page = page;

    xSemaphoreGive(g_cache_mgr.mutex);
    return 1;
}

/**
 * @brief 查询门禁记录
 */
uint8_t cache_query_door(uint8_t page, uint8_t per_page,
                          DoorEventCache_t *out_events,
                          uint8_t *out_count)
{
    if (!g_cache_mgr.initialized || out_events == NULL || out_count == NULL)
        return 0;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        return 0;
    }

    uint16_t start_idx = page * per_page;
    uint16_t total = g_cache_mgr.door.count;

    if (start_idx >= total)
    {
        xSemaphoreGive(g_cache_mgr.mutex);
        return 0;
    }

    uint16_t end_idx = start_idx + per_page;
    uint16_t avail = (end_idx <= total) ? per_page : (total - start_idx);

    for (uint16_t i = 0; i < avail; i++)
    {
        uint16_t cache_idx = total - 1 - (start_idx + i);
        out_events[i] = g_cache_mgr.door.events[cache_idx];
    }

    *out_count = (uint8_t)avail;

    int8_t direction = 0;
    if (g_cache_mgr.door.last_page < page)
        direction = 1;
    else if (g_cache_mgr.door.last_page > page)
        direction = -1;

    g_cache_mgr.door.last_direction = direction;
    g_cache_mgr.door.last_page = page;

    xSemaphoreGive(g_cache_mgr.mutex);
    return 1;
}

//==============================================================================
// 动态加载接口
//==============================================================================

/**
 * @brief 动态加载更多采样记录（滑动窗口向前）
 */
uint8_t cache_load_more_sampling(void)
{
    if (!g_cache_mgr.initialized || !tsdb_is_ready()) return 0;
    if (g_cache_mgr.sampling.oldest_ts == 0) return 0;

    // 简化实现：当前缓存已满时不再加载更多
    // 完整实现需要滑动窗口机制
    if (g_cache_mgr.sampling.count >= CACHE_CAPACITY_SAMPLING) {
        return 0;
    }

    // TODO: 实现动态加载逻辑
    // 需要: LoadContext, 临时缓存数组, 滑动窗口机制
    return 0;
}

/**
 * @brief 动态加载更多送样记录
 */
uint8_t cache_load_more_delivery(void)
{
    if (!g_cache_mgr.initialized || !tsdb_is_ready()) return 0;
    if (g_cache_mgr.delivery.count >= CACHE_CAPACITY_DELIVERY) return 0;
    return 0;
}

/**
 * @brief 动态加载更多留样记录
 */
uint8_t cache_load_more_retain(void)
{
    if (!g_cache_mgr.initialized || !tsdb_is_ready()) return 0;
    if (g_cache_mgr.retain.count >= CACHE_CAPACITY_RETAIN) return 0;
    return 0;
}

/**
 * @brief 动态加载更多电源记录
 */
uint8_t cache_load_more_power(void)
{
    if (!g_cache_mgr.initialized || !tsdb_is_ready()) return 0;
    if (g_cache_mgr.power.count >= CACHE_CAPACITY_POWER) return 0;
    return 0;
}

/**
 * @brief 动态加载更多门禁记录
 */
uint8_t cache_load_more_door(void)
{
    if (!g_cache_mgr.initialized || !tsdb_is_ready()) return 0;
    if (g_cache_mgr.door.count >= CACHE_CAPACITY_DOOR) return 0;
    return 0;
}

/**
 * @brief 获取缓存总记录数
 */
uint16_t cache_get_count(CacheType type)
{
    if (!g_cache_mgr.initialized) return 0;

    switch (type) {
    case CACHE_TYPE_SAMPLING: return g_cache_mgr.sampling.count;
    case CACHE_TYPE_DELIVERY: return g_cache_mgr.delivery.count;
    case CACHE_TYPE_RETAIN:   return g_cache_mgr.retain.count;
    case CACHE_TYPE_POWER:    return g_cache_mgr.power.count;
    case CACHE_TYPE_DOOR:     return g_cache_mgr.door.count;
    default: return 0;
    }
}

//==============================================================================
// 工具函数
//==============================================================================

/**
 * @brief 获取缓存统计信息
 */
void cache_get_stats(CacheType type,
                     uint16_t *out_count,
                     fdb_time_t *out_oldest_ts,
                     fdb_time_t *out_newest_ts)
{
    if (!g_cache_mgr.initialized || out_count == NULL)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    switch (type)
    {
    case CACHE_TYPE_SAMPLING:
        *out_count = g_cache_mgr.sampling.count;
        if (out_oldest_ts)
            *out_oldest_ts = g_cache_mgr.sampling.oldest_ts;
        if (out_newest_ts)
            *out_newest_ts = g_cache_mgr.sampling.newest_ts;
        break;

    case CACHE_TYPE_DELIVERY:
        *out_count = g_cache_mgr.delivery.count;
        if (out_oldest_ts)
            *out_oldest_ts = g_cache_mgr.delivery.oldest_ts;
        if (out_newest_ts)
            *out_newest_ts = g_cache_mgr.delivery.newest_ts;
        break;

    case CACHE_TYPE_RETAIN:
        *out_count = g_cache_mgr.retain.count;
        if (out_oldest_ts)
            *out_oldest_ts = g_cache_mgr.retain.oldest_ts;
        if (out_newest_ts)
            *out_newest_ts = g_cache_mgr.retain.newest_ts;
        break;

    case CACHE_TYPE_POWER:
        *out_count = g_cache_mgr.power.count;
        if (out_oldest_ts)
            *out_oldest_ts = g_cache_mgr.power.oldest_ts;
        if (out_newest_ts)
            *out_newest_ts = g_cache_mgr.power.newest_ts;
        break;

    case CACHE_TYPE_DOOR:
        *out_count = g_cache_mgr.door.count;
        if (out_oldest_ts)
            *out_oldest_ts = g_cache_mgr.door.oldest_ts;
        if (out_newest_ts)
            *out_newest_ts = g_cache_mgr.door.newest_ts;
        break;

    default:
        *out_count = 0;
        break;
    }

    xSemaphoreGive(g_cache_mgr.mutex);
}

/**
 * @brief 清空指定类型缓存
 */
void cache_clear(CacheType type)
{
    if (!g_cache_mgr.initialized)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    switch (type)
    {
    case CACHE_TYPE_SAMPLING:
        memset(&g_cache_mgr.sampling, 0, sizeof(SamplingRecordCache));
        break;

    case CACHE_TYPE_DELIVERY:
        memset(&g_cache_mgr.delivery, 0, sizeof(DeliveryRecordCache));
        break;

    case CACHE_TYPE_RETAIN:
        memset(&g_cache_mgr.retain, 0, sizeof(RetainRecordCache));
        break;

    case CACHE_TYPE_POWER:
        memset(&g_cache_mgr.power, 0, sizeof(PowerRecordCache));
        break;

    case CACHE_TYPE_DOOR:
        memset(&g_cache_mgr.door, 0, sizeof(DoorRecordCache));
        break;

    default:
        break;
    }

    xSemaphoreGive(g_cache_mgr.mutex);
}

/**
 * @brief 清空所有缓存
 */
void cache_clear_all(void)
{
    if (!g_cache_mgr.initialized)
        return;

    if (xSemaphoreTake(g_cache_mgr.mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return;

    memset(&g_cache_mgr.sampling, 0, sizeof(SamplingRecordCache));
    memset(&g_cache_mgr.delivery, 0, sizeof(DeliveryRecordCache));
    memset(&g_cache_mgr.retain, 0, sizeof(RetainRecordCache));
    memset(&g_cache_mgr.power, 0, sizeof(PowerRecordCache));
    memset(&g_cache_mgr.door, 0, sizeof(DoorRecordCache));

    xSemaphoreGive(g_cache_mgr.mutex);
}

//==============================================================================
// 滑动窗口和智能预加载（简化版）
//==============================================================================

/**
 * @brief 智能预加载决策
 * @note 简化版：当前实现只支持基础FIFO缓存，预加载功能待后续优化
 */
PreloadDirection decide_preload(uint8_t requested_page,
                                 uint8_t cached_start_page,
                                 uint8_t cached_end_page,
                                 int8_t last_direction)
{
    // 简化实现：始终不预加载，依靠FIFO缓存自动更新
    // 完整的滑动窗口+智能预加载需要更复杂的TSDB查询逻辑
    (void)requested_page;
    (void)cached_start_page;
    (void)cached_end_page;
    (void)last_direction;

    return PRELOAD_NONE;
}

/**
 * @brief 滑动窗口操作（简化版）
 * @note 当前实现使用FIFO策略，滑动窗口功能待后续优化
 */
void cache_slide_window_forward(CacheType type,
                                 void *new_records,
                                 uint16_t new_count)
{
    // 简化实现：不支持主动滑动窗口
    // FIFO缓存会自动处理新记录添加
    (void)type;
    (void)new_records;
    (void)new_count;
}

void cache_slide_window_backward(CacheType type,
                                  void *new_records,
                                  uint16_t new_count)
{
    // 简化实现：不支持主动滑动窗口
    (void)type;
    (void)new_records;
    (void)new_count;
}

/**
 * @brief 重建缓存窗口（简化版）
 * @note 当前实现不支持围绕特定页重建，需要完整的TSDB迭代器支持
 */
void cache_rebuild_window_around_page(CacheType type, uint8_t page)
{
    (void)type;
    (void)page;
}

/**
 * @brief 预加载任务（简化版）
 */
void preload_task(void *param)
{
    PreloadRequest *req = (PreloadRequest *)param;

    if (req == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    // 简化实现：不执行实际预加载

    vPortFree(req);
    vTaskDelete(NULL);
}

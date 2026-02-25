#include "app_record_query.h"
#include "app_flashdb.h"
#include "bsp_screen.h"
#include <string.h>
#include <stdio.h>

/* ======================== 查询会话 ======================== */
static rq_session_t s_session[RQ_TYPE_COUNT];

/* ======================== 缓存存储 ======================== */
/* 采样: 20页 × 7条 = 140条 */
static rq_sample_entry_t s_cache_samp[RQ_CACHE_SAMPLING * RQ_PAGE_SIZE];
static uint16_t s_cache_samp_start;   /* 缓存起始页号 */
static uint8_t  s_cache_samp_pages;   /* 缓存中有效页数 */

/* 送样: 5页 × 7条 = 35条 */
static rq_sample_entry_t s_cache_deliv[RQ_CACHE_DELIVERY * RQ_PAGE_SIZE];
static uint16_t s_cache_deliv_start;
static uint8_t  s_cache_deliv_pages;

/* 留样: 5页 × 7条 = 35条 */
static rq_retain_entry_t s_cache_retain[RQ_CACHE_RETAIN * RQ_PAGE_SIZE];
static uint16_t s_cache_retain_start;
static uint8_t  s_cache_retain_pages;

/* ======================== 迭代上下文 ======================== */
typedef struct {
    rq_type_t type;
    uint16_t  count;
} count_ctx_t;

typedef struct {
    rq_type_t type;
    uint16_t  skip;
    uint16_t  take;
    uint16_t  skipped;
    uint16_t  taken;
    uint8_t  *out_buf;
    uint8_t   entry_size;
} page_ctx_t;

/* ======================== 辅助函数 ======================== */
static uint8_t event_type_matches(uint16_t evt, rq_type_t type)
{
    switch (type) {
    case RQ_SAMPLING: return (evt == EVT_SAMPLE_DONE)   ? 1u : 0u;
    case RQ_DELIVERY: return (evt == EVT_DELIVERY_DONE) ? 1u : 0u;
    case RQ_RETAIN:   return (evt == EVT_RETAIN_DONE)   ? 1u : 0u;
    case RQ_POWER:    return (evt == EVT_POWER_OFF || evt == EVT_POWER_ON)   ? 1u : 0u;
    case RQ_DOOR:     return (evt == EVT_DOOR_OPEN || evt == EVT_DOOR_CLOSE) ? 1u : 0u;
    default:          return 0u;
    }
}

/* ======================== 计数回调 ======================== */
static void count_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    count_ctx_t *ctx = (count_ctx_t *)user;
    (void)body;
    if (event_type_matches(info->event_type, ctx->type))
        ctx->count++;
}

/* ======================== 分页回调 ======================== */
static void page_cb(const TsdbEventInfo *info, const void *body, void *user)
{
    page_ctx_t *ctx = (page_ctx_t *)user;
    uint8_t *dst;

    if (ctx->taken >= ctx->take) return;  /* 已收集够 */
    if (!event_type_matches(info->event_type, ctx->type)) return;

    if (ctx->skipped < ctx->skip) {
        ctx->skipped++;
        return;
    }

    dst = ctx->out_buf + (ctx->taken * ctx->entry_size);

    switch (ctx->type) {
    case RQ_SAMPLING:
    case RQ_DELIVERY: {
        rq_sample_entry_t *e = (rq_sample_entry_t *)dst;
        if (ctx->type == RQ_SAMPLING) {
            const SampleLogData *d = (const SampleLogData *)body;
            e->mode = (uint16_t)d->trigger_source;
            e->bucket_or_source = (uint16_t)d->bucket_id;
            e->volume = d->sample_volume;
        } else {
            const DeliveryLogData *d = (const DeliveryLogData *)body;
            e->mode = (uint16_t)d->trigger_source;
            e->bucket_or_source = (uint16_t)d->water_source;
            e->volume = d->delivery_volume;
        }
        e->timestamp = (uint32_t)info->ts;
        break;
    }
    case RQ_RETAIN: {
        rq_retain_entry_t *e = (rq_retain_entry_t *)dst;
        const RetainSampleLogData *d = (const RetainSampleLogData *)body;
        e->mode = (uint16_t)d->trigger_source;
        e->bottle = (uint16_t)d->bottle_id;
        e->volume = d->retain_volume;
        e->timestamp = (uint32_t)info->ts;
        e->success = d->success;
        e->acid = d->acid_added;
        break;
    }
    case RQ_POWER:
    case RQ_DOOR: {
        rq_event_entry_t *e = (rq_event_entry_t *)dst;
        e->event_type = info->event_type;
        e->timestamp = (uint32_t)info->ts;
        break;
    }
    default:
        break;
    }
    ctx->taken++;
}

/* ======================== 查询辅助 ======================== */
static uint16_t rq_count_records(rq_type_t type)
{
    count_ctx_t ctx;
    ctx.type = type;
    ctx.count = 0;
    tsdb_iter_reverse_all(count_cb, &ctx);
    return ctx.count;
}

static uint8_t rq_load_page(rq_type_t type, uint16_t page,
                             void *buf, uint8_t entry_size,
                             uint8_t *out_count)
{
    page_ctx_t ctx;
    ctx.type = type;
    ctx.skip = page * RQ_PAGE_SIZE;
    ctx.take = RQ_PAGE_SIZE;
    ctx.skipped = 0;
    ctx.taken = 0;
    ctx.out_buf = (uint8_t *)buf;
    ctx.entry_size = entry_size;

    memset(buf, 0, (size_t)RQ_PAGE_SIZE * entry_size);
    tsdb_iter_reverse_all(page_cb, &ctx);

    if (out_count) *out_count = (uint8_t)ctx.taken;
    return (uint8_t)ctx.taken;
}

/* ======================== 缓存管理 ======================== */

/* 缓存描述符：统一访问各类型的缓存数组/起始页/有效页数 */
typedef struct {
    uint8_t  *buf;          /* 缓存数组首地址 */
    uint16_t *start;        /* 缓存起始页号指针 */
    uint8_t  *pages;        /* 缓存有效页数指针 */
    uint8_t   max_pages;    /* 最大缓存页数 */
    uint8_t   entry_size;   /* 单条记录大小 */
} cache_desc_t;

/* 获取指定类型的缓存描述符，电源/门禁返回0(不缓存) */
static uint8_t cache_get_desc(rq_type_t type, cache_desc_t *d)
{
    switch (type) {
    case RQ_SAMPLING:
        d->buf = (uint8_t *)s_cache_samp;  d->start = &s_cache_samp_start;
        d->pages = &s_cache_samp_pages;     d->max_pages = RQ_CACHE_SAMPLING;
        d->entry_size = (uint8_t)sizeof(rq_sample_entry_t);
        return 1;
    case RQ_DELIVERY:
        d->buf = (uint8_t *)s_cache_deliv;  d->start = &s_cache_deliv_start;
        d->pages = &s_cache_deliv_pages;    d->max_pages = RQ_CACHE_DELIVERY;
        d->entry_size = (uint8_t)sizeof(rq_sample_entry_t);
        return 1;
    case RQ_RETAIN:
        d->buf = (uint8_t *)s_cache_retain; d->start = &s_cache_retain_start;
        d->pages = &s_cache_retain_pages;   d->max_pages = RQ_CACHE_RETAIN;
        d->entry_size = (uint8_t)sizeof(rq_retain_entry_t);
        return 1;
    default:
        return 0;  /* 电源/门禁不缓存 */
    }
}

static uint8_t rq_cache_max_pages(rq_type_t type)
{
    cache_desc_t d;
    return cache_get_desc(type, &d) ? d.max_pages : 0;
}

static void cache_load_window(rq_type_t type, uint16_t center_page)
{
    uint8_t max_pg = rq_cache_max_pages(type);
    uint16_t total_pages;
    uint16_t win_start;
    uint16_t win_end;
    uint16_t pg;
    uint8_t idx;

    if (max_pg == 0) return;  /* 电源/门禁不缓存 */

    total_pages = (s_session[type].total_count + RQ_PAGE_SIZE - 1) / RQ_PAGE_SIZE;
    if (total_pages == 0) total_pages = 1;

    /* 计算窗口范围 */
    win_start = (center_page > max_pg / 2) ? (center_page - max_pg / 2) : 0;
    win_end = win_start + max_pg;
    if (win_end > total_pages) {
        win_end = total_pages;
        win_start = (win_end > max_pg) ? (win_end - max_pg) : 0;
    }

    /* 通过描述符统一加载（cache_get_desc已在max_pg检查前保证有效） */
    {
        cache_desc_t d;
        cache_get_desc(type, &d);
        idx = 0;
        for (pg = win_start; pg < win_end; pg++) {
            rq_load_page(type, pg,
                &d.buf[idx * RQ_PAGE_SIZE * d.entry_size],
                d.entry_size, NULL);
            idx++;
        }
        *d.start = win_start;
        *d.pages = idx;
    }
}

static uint8_t cache_get_page(rq_type_t type, uint16_t page,
                              void *buf, uint8_t entry_size,
                              uint8_t *out_count)
{
    uint8_t max_pg = rq_cache_max_pages(type);
    uint16_t offset;

    /* 电源/门禁: 直查不缓存 */
    if (max_pg == 0)
        return rq_load_page(type, page, buf, entry_size, out_count);

    /* 通过描述符统一检查缓存命中 */
    {
        cache_desc_t d;
        cache_get_desc(type, &d);
        if (page >= *d.start && page < *d.start + *d.pages) {
            offset = (page - *d.start) * RQ_PAGE_SIZE * d.entry_size;
            memcpy(buf, &d.buf[offset], (size_t)RQ_PAGE_SIZE * entry_size);
            if (out_count) *out_count = RQ_PAGE_SIZE;
            return RQ_PAGE_SIZE;
        }
    }

    /* 缓存未命中: 重新加载窗口 */
    cache_load_window(type, page);
    return rq_load_page(type, page, buf, entry_size, out_count);
}

/* ======================== 屏幕帧常量 ======================== */
#define SCR_LOG_ADDR        0x5182
#define SAMP_REC_BYTES      12   /* mode(2)+bucket(2)+volume(2)+datetime(6) */
#define RETAIN_REC_BYTES    14   /* mode(2)+bottle(2)+volume(2)+datetime(6)+result(2) */
#define EVENT_REC_BYTES      8   /* type(2)+datetime(6) */

/* ======================== 时间戳解码 ======================== */
/* TSDB时间戳公式: year*365+(month-1)*30+day → days*86400+h*3600+m*60+s */
static void encode_datetime(uint8_t *dst, uint32_t ts)
{
    uint32_t day_sec, days, day_in_year;
    uint8_t year, month, day, hour, minute, second;

    if (!dst) return;

    day_sec = ts % 86400u;
    days    = ts / 86400u;

    second  = (uint8_t)(day_sec % 60u);
    minute  = (uint8_t)((day_sec / 60u) % 60u);
    hour    = (uint8_t)(day_sec / 3600u);

    year       = (uint8_t)(days / 365u);
    day_in_year = days % 365u;
    month      = (uint8_t)(day_in_year / 30u) + 1;
    day        = (uint8_t)(day_in_year % 30u);
    if (day == 0) { day = 30; if (month > 1) month--; }

    dst[0] = year;
    dst[1] = month;
    dst[2] = day;
    dst[3] = hour;
    dst[4] = minute;
    dst[5] = second;
}

/* ======================== 采样/送样页发送(共用格式) ======================== */
static void send_sample_type_page(rq_type_t type, uint16_t page)
{
    rq_sample_entry_t items[RQ_PAGE_SIZE];
    uint8_t count = 0;
    uint8_t data[RQ_PAGE_SIZE * SAMP_REC_BYTES];
    uint8_t i;
    uint8_t *slot;

    memset(data, 0, sizeof(data));
    cache_get_page(type, page, items,
                   (uint8_t)sizeof(rq_sample_entry_t), &count);

    for (i = 0; i < RQ_PAGE_SIZE; i++) {
        slot = &data[i * SAMP_REC_BYTES];
        if (i >= count) continue;
        slot[0] = (uint8_t)(items[i].mode >> 8);
        slot[1] = (uint8_t)(items[i].mode & 0xFFu);
        slot[2] = (uint8_t)(items[i].bucket_or_source >> 8);
        slot[3] = (uint8_t)(items[i].bucket_or_source & 0xFFu);
        slot[4] = (uint8_t)(items[i].volume >> 8);
        slot[5] = (uint8_t)(items[i].volume & 0xFFu);
        encode_datetime(&slot[6], items[i].timestamp);
    }
    screen_write_var(SCR_LOG_ADDR, data, sizeof(data));
}

/* ======================== 留样页发送 ======================== */
static void send_retain_page(uint16_t page)
{
    rq_retain_entry_t items[RQ_PAGE_SIZE];
    uint8_t count = 0;
    uint8_t data[RQ_PAGE_SIZE * RETAIN_REC_BYTES];
    uint8_t i;
    uint8_t *slot;

    memset(data, 0, sizeof(data));
    cache_get_page(RQ_RETAIN, page, items,
                   (uint8_t)sizeof(rq_retain_entry_t), &count);

    for (i = 0; i < RQ_PAGE_SIZE; i++) {
        slot = &data[i * RETAIN_REC_BYTES];
        if (i >= count) continue;
        slot[0] = (uint8_t)(items[i].mode >> 8);
        slot[1] = (uint8_t)(items[i].mode & 0xFFu);
        slot[2] = (uint8_t)(items[i].bottle >> 8);
        slot[3] = (uint8_t)(items[i].bottle & 0xFFu);
        slot[4] = (uint8_t)(items[i].volume >> 8);
        slot[5] = (uint8_t)(items[i].volume & 0xFFu);
        encode_datetime(&slot[6], items[i].timestamp);
        slot[12] = 0;
        slot[13] = items[i].success;
    }
    screen_write_var(SCR_LOG_ADDR, data, sizeof(data));
}

/* ======================== 电源/门禁页发送(共用格式) ======================== */
static void send_event_type_page(rq_type_t type, uint16_t page)
{
    rq_event_entry_t items[RQ_PAGE_SIZE];
    uint8_t count = 0;
    uint8_t data[RQ_PAGE_SIZE * EVENT_REC_BYTES];
    uint8_t i;
    uint8_t *slot;

    memset(data, 0, sizeof(data));
    cache_get_page(type, page, items,
                   (uint8_t)sizeof(rq_event_entry_t), &count);

    for (i = 0; i < RQ_PAGE_SIZE; i++) {
        slot = &data[i * EVENT_REC_BYTES];
        if (i >= count) continue;
        slot[0] = (uint8_t)(items[i].event_type >> 8);
        slot[1] = (uint8_t)(items[i].event_type & 0xFFu);
        encode_datetime(&slot[2], items[i].timestamp);
    }
    screen_write_var(SCR_LOG_ADDR, data, sizeof(data));
}

/* ======================== 页面分发 ======================== */
static void notify_page(rq_type_t type, uint16_t page)
{
    switch (type) {
    case RQ_SAMPLING:
    case RQ_DELIVERY: send_sample_type_page(type, page); break;
    case RQ_RETAIN:   send_retain_page(page);            break;
    case RQ_POWER:
    case RQ_DOOR:     send_event_type_page(type, page);  break;
    default: break;
    }
}

/* ======================== API ======================== */
void record_query_init(rq_type_t type)
{
    uint16_t total;
    if ((uint8_t)type >= RQ_TYPE_COUNT) return;

    total = rq_count_records(type);
    s_session[type].total_count  = total;
    s_session[type].current_page = 0;
    s_session[type].valid        = 1;

    printf("[记录查询] 类型=%d, 总数=%u\r\n", (int)type, (unsigned)total);

    /* 预加载缓存窗口 */
    if (rq_cache_max_pages(type) > 0)
        cache_load_window(type, 0);

    notify_page(type, 0);
}

void record_query_page_nav(rq_type_t type, uint8_t direction)
{
    uint16_t total, max_page, cur;

    if ((uint8_t)type >= RQ_TYPE_COUNT) return;
    if (!s_session[type].valid) return;

    total = s_session[type].total_count;
    if (total == 0) return;

    /* 计算最大页码 (0-based) */
    max_page = (total - 1) / RQ_PAGE_SIZE;
    cur = s_session[type].current_page;

    if (direction == 0x01) {
        /* 上一页 */
        if (cur == 0) return;
        cur--;
    } else if (direction == 0x02) {
        /* 下一页 */
        if (cur >= max_page) return;
        cur++;
    } else {
        return;
    }

    s_session[type].current_page = (uint8_t)cur;
    notify_page(type, cur);

    /* 智能预加载：翻页后重新加载以当前页为中心的缓存窗口 */
    if (rq_cache_max_pages(type) > 0)
        cache_load_window(type, cur);
}

/* ======================== 实时追加通知 ======================== */
void record_query_notify_new(rq_type_t type)
{
    if ((uint8_t)type >= RQ_TYPE_COUNT) return;
    if (!s_session[type].valid) return;

    /* 更新总数 */
    s_session[type].total_count++;

    /* 如果当前在第0页(最新记录页)，重新加载缓存并刷新显示 */
    if (s_session[type].current_page == 0) {
        if (rq_cache_max_pages(type) > 0)
            cache_load_window(type, 0);
        notify_page(type, 0);
    }
}

/* ======================== 会话查询 ======================== */
uint8_t record_query_is_active(rq_type_t type)
{
    if ((uint8_t)type >= RQ_TYPE_COUNT) return 0;
    return s_session[type].valid;
}

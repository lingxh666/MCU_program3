#ifndef APP_RECORD_QUERY_H
#define APP_RECORD_QUERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 查询类型 */
typedef enum {
    RQ_SAMPLING = 0,
    RQ_DELIVERY = 1,
    RQ_RETAIN   = 2,
    RQ_POWER    = 3,
    RQ_DOOR     = 4,
    RQ_TYPE_COUNT
} rq_type_t;

/* 每页记录数 */
#define RQ_PAGE_SIZE  7

/* 缓存页数 */
#define RQ_CACHE_SAMPLING  20
#define RQ_CACHE_DELIVERY   5
#define RQ_CACHE_RETAIN     5
#define RQ_CACHE_POWER      0
#define RQ_CACHE_DOOR       0

/* 查询会话 */
typedef struct {
    uint8_t  current_page;
    uint16_t total_count;
    uint8_t  valid;
} rq_session_t;

/* 缓存条目 - 采样/送样 */
typedef struct {
    uint16_t mode;
    uint16_t bucket_or_source;
    uint16_t volume;
    uint32_t timestamp;
} rq_sample_entry_t;

/* 缓存条目 - 留样 */
typedef struct {
    uint16_t mode;
    uint16_t bottle;
    uint16_t volume;
    uint32_t timestamp;
    uint8_t  success;
    uint8_t  acid;
} rq_retain_entry_t;

/* 缓存条目 - 电源/门禁 */
typedef struct {
    uint16_t event_type;
    uint32_t timestamp;
} rq_event_entry_t;

/* API */
void record_query_init(rq_type_t type);
void record_query_page_nav(rq_type_t type, uint8_t direction);

/* 实时追加：新记录写入后调用，增量更新缓存 */
void record_query_notify_new(rq_type_t type);

/* 查询会话是否有效 */
uint8_t record_query_is_active(rq_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* APP_RECORD_QUERY_H */

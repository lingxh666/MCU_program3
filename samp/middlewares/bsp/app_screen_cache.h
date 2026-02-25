#ifndef __APP_SCREEN_CACHE_H__
#define __APP_SCREEN_CACHE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* KVDB缓存类型 */
typedef enum {
    KVDB_CACHE_SAMPLE = 0,
    KVDB_CACHE_DELIVERY,
    KVDB_CACHE_RETAIN,
    KVDB_CACHE_RETAIN_STATE,
    KVDB_CACHE_COMM,
    KVDB_CACHE_SYSTEM,
    KVDB_CACHE_CALIB,
    KVDB_CACHE_MAX
} kvdb_cache_type_t;

/* TSDB缓存条目 */
typedef struct {
    uint16_t event_type;
    uint8_t  body[16];
    uint8_t  body_len;
} tsdb_cache_entry_t;

/* KVDB缓存 */
void    kvdb_cache_init(void);
void    kvdb_cache_mark_dirty(kvdb_cache_type_t type);
uint8_t kvdb_cache_has_dirty(void);
void    kvdb_cache_flush_all(void);

/* TSDB缓存 */
void    tsdb_cache_init(void);
uint8_t tsdb_cache_append(uint16_t event_type, const void *body, size_t len);
void    tsdb_cache_flush_all(void);
uint8_t tsdb_cache_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SCREEN_CACHE_H__ */

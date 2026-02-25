#ifndef APP_FLASHDB_H
#define APP_FLASHDB_H

#include <stdint.h>
#include <stddef.h>
#include "flashdb.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== TSDB event info (callback helper) ===== */
typedef struct {
  uint16_t event_type;
  fdb_time_t ts;
  size_t body_len;
} TsdbEventInfo;

/* ===== Business event payloads ===== */
typedef struct {
  uint8_t trigger_source;   /* 0=manual 1=timed 2=flow 3=comm */
  uint8_t bucket_id;        /* 1=A 2=B */
  uint16_t sample_volume;   /* ml */
  uint8_t result;           /* 0=fail 1=ok */
} SampleLogData;

typedef struct {
  uint8_t trigger_source;
  uint8_t water_source;     /* 1=A 2=B 3=mix */
  uint16_t delivery_volume; /* ml */
  uint8_t result;
} DeliveryLogData;

typedef struct {
  uint8_t trigger_source;
  uint8_t bottle_id;        /* 1-24 */
  uint16_t retain_volume;   /* ml */
  uint8_t success;
  uint8_t acid_added;       /* 0=no 1=yes */
} RetainSampleLogData;

/* RetainBottleState 定义在 app_config.h 中 */

/* ===== Event Type 常量 ===== */
#define EVT_SAMPLE_DONE     0x0040
#define EVT_DELIVERY_DONE   0x0042
#define EVT_RETAIN_DONE     0x0044
#define EVT_DOOR_OPEN       0x0070
#define EVT_DOOR_CLOSE      0x0071
#define EVT_POWER_OFF       0x00F2
#define EVT_POWER_ON        0x00F3

/* ===== TSDB API ===== */
uint8_t tsdb_event_append(uint16_t event_type, const void *body, size_t body_len);

typedef void (*tsdb_event_iter_cb)(const TsdbEventInfo *info, const void *body, void *user);
void tsdb_iter_range(fdb_time_t from, fdb_time_t to, tsdb_event_iter_cb cb, void *user);
void tsdb_iter_reverse_all(tsdb_event_iter_cb cb, void *user);

/* ===== KVDB API ===== */
uint8_t cfg_kv_init(void);
uint8_t cfg_save_sample(const void *p);
uint8_t cfg_load_sample(void *p);
uint8_t cfg_save_delivery(const void *p);
uint8_t cfg_load_delivery(void *p);
uint8_t cfg_save_retain(const void *p);
uint8_t cfg_load_retain(void *p);
uint8_t cfg_save_comm(const void *p);
uint8_t cfg_load_comm(void *p);
uint8_t cfg_save_system(const void *p);
uint8_t cfg_load_system(void *p);
uint8_t cfg_save_calib(const void *p);
uint8_t cfg_load_calib(void *p);
uint8_t cfg_reset_all(void);

/* Retain bottle state */
uint8_t cfg_save_retain_state(const void *p);
uint8_t cfg_load_retain_state(void *p);

/* KVDB mutex */
void kvdb_lock(void);
void kvdb_unlock(void);

/* Unified init entry */
void settings_init_load(void);
void fdb_start_tasks(void);

/* TSDB status */
uint8_t tsdb_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASHDB_H */

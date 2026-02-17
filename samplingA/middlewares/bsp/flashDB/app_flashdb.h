#ifndef APP_FLASHDB_H
#define APP_FLASHDB_H

#include <stdint.h>
#include <stddef.h>
#include "rtc.h"      /* for calendar_type */
#include "flashdb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== FlashDB TSDB 简化设计：遵循官方示例 ===== */
/* 
 * 设计理念：
 * 1. 不再使用自定义的 LogEventHeader，完全依赖 FlashDB 的 tsl->time
 * 2. 每条记录只存储：2字节event_type + 实际业务数据
 * 3. 时间戳由 FlashDB 自动管理（通过 tsdb_time_cb）
 */

/* 用于回调的临时头结构（仅用于接口兼容性） */
typedef struct {
  uint16_t event_type;    /* 事件类型：1采样 2送样 3留样 4=SamplingLogRecord 等 */
  fdb_time_t ts;          /* 时间戳（从 tsl->time 获取） */
  size_t body_len;        /* 事件体长度（字节） */
} TsdbEventInfo;

/* ===== 业务事件体（沿用旧结构，作为payload） ===== */
typedef struct {
  uint8_t trigger_source;   /* 0=手动 1=定时 2=流量 3=通讯 等 */
  uint8_t bucket_id;        /* 桶号：1=A桶 2=B桶 */
  uint16_t sample_volume;   /* 采样量(ml) */
  uint8_t result;           /* 0=失败 1=成功 */
} SampleLogData;

typedef struct {
  uint8_t trigger_source;
  uint8_t water_source;     /* 1=A桶 2=B桶 3=混合 */
  uint16_t delivery_volume; /* 送样量(ml) */
  uint8_t result;           /* 0=失败 1=成功 */
} DeliveryLogData;

typedef struct {
  uint8_t trigger_source;
  uint8_t bottle_id;        /* 1-24 */
  uint16_t retain_volume;   /* 留样量(ml) */
  uint8_t success;          /* 0=失败 1=成功 */
  uint8_t acid_added;       /* 0=否 1=是 */
} RetainSampleLogData;


/* 留样瓶位状态：用于持久化当前瓶位与已占用掩码（低24位） */
typedef struct {
  uint8_t currentBottle;  /* 1..24 */
  uint32_t usedMask;      /* bit0->瓶1, bit23->瓶24 */
} RetainBottleState;
/* ===== TSDB: 事件直写与回调式读取 ===== */
/* 直接写入一条事件（2字节type + 数据体） */
uint8_t tsdb_event_append(uint16_t event_type, const void *body, size_t body_len);

/* 按时间范围遍历事件（回调式），cb中收到事件信息和数据体指针 */
typedef void (*tsdb_event_iter_cb)(const TsdbEventInfo *info, const void *body, void *user);
void tsdb_iter_range(fdb_time_t from, fdb_time_t to, tsdb_event_iter_cb cb, void *user);

/**
 * @brief 反向遍历TSDB（从最新到最旧），支持提前终止
 * @param to 结束时间（从此时间向前遍历）
 * @param cb 回调函数
 * @param user 用户参数（包含target_count字段，收集够后停止）
 * @param out_last_ts 输出：遍历停止时的时间戳（用于继续查询）
 * @return 遍历的记录数
 */
uint32_t tsdb_iter_reverse_until(fdb_time_t to, tsdb_event_iter_cb cb, void *user, fdb_time_t *out_last_ts);

/* ===== KVDB: 初始化并加载设置（读不到则回写默认） ===== */
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

/* 留样瓶位状态：KVDB 读写接口 */
uint8_t cfg_save_retain_state(const void *p);
uint8_t cfg_load_retain_state(void *p);

/* 带超时的KVDB保存接口（非阻塞，用于关键路径） */
uint8_t cfg_save_retain_timeout(const void *p, uint32_t timeout_ms);
uint8_t cfg_save_retain_state_timeout(const void *p, uint32_t timeout_ms);

/* ===== 断电记录：KVDB存储 ===== */
typedef struct {
    uint32_t last_heartbeat_time;    // 最后心跳时间（Unix秒，30s更新一次）
    uint32_t last_shutdown_time;     // 最后正常关机时间（Unix秒，手动关机时记录）
    uint32_t power_count;           // 断电次数统计
    uint32_t total_power_off_time;  // 累计断电时间（秒）
    uint32_t last_power_off_duration; // 上次断电持续时间（秒）
} PowerRecord_t;

/* MQTT发送状态持久化结构 */
typedef struct {
    uint32_t sampling_last_ts;   // 采样记录上次发送时间戳
    uint32_t delivery_last_ts;   // 送样记录上次发送时间戳
    uint32_t retain_last_ts;     // 留样记录上次发送时间戳
    uint32_t power_last_ts;      // 电源记录上次发送时间戳
    uint32_t door_last_ts;       // 门禁记录上次发送时间戳
    uint32_t reserved[3];        // 预留扩展
} MqttSentState_t;

/* MQTT发送状态：KVDB 读写接口 */
uint8_t cfg_save_mqtt_state(const MqttSentState_t *p);
uint8_t cfg_load_mqtt_state(MqttSentState_t *p);

/* GPS/LBS定位信息结构 */
typedef struct {
    float longitude;    // 经度（保留2位小数）
    float latitude;     // 纬度（保留2位小数）
    uint8_t valid;      // 定位是否有效 0=无效 1=有效
    uint32_t timestamp; // 定位时间戳
} LocationInfo_t;

/* LBS定位信息：KVDB 读写接口 */
uint8_t cfg_save_location(const LocationInfo_t *p);
uint8_t cfg_load_location(LocationInfo_t *p);

/* 断电记录：KVDB 读写接口 */
uint8_t cfg_save_power_record(const void *p);
uint8_t cfg_load_power_record(void *p);

/* 断电记录：TSDB事件码 */
#define POWER_EVT_FAILURE_DETECTED  0x00F2  // 断电事件检测
#define POWER_EVT_RECOVERY_COMPLETE 0x00F3  // 断电恢复完成

/* 断电事件数据结构 */
typedef struct {
    uint32_t power_off_time;       // 推算的断电时间
    uint32_t power_on_time;        // 来电时间
    uint32_t power_off_duration;   // 断电持续时间（秒）
    uint32_t power_count;          // 总断电次数
} PowerFailureEvent_t;

/* 门禁记录：TSDB事件码 */
#define DOOR_EVT_UNLOCKED          0x0070  // 门已开锁（开门）
#define DOOR_EVT_LOCKED            0x0071  // 门已上锁（关门）

/* 门禁事件数据结构 */
typedef struct {
    uint32_t timestamp;           // 事件发生时间戳（Unix秒，1970基准）
    uint32_t duration;            // 持续时间（毫秒，开锁或上锁的持续时间）
} DoorEvent_t;

/* 断电记录功能接口 */
void power_record_init(void);
void power_update_heartbeat(void);
void power_heartbeat_periodic_save(void);  /* 定期保存心跳，避免频繁Flash写入 */
void power_failure_detection_and_record(void);
void power_record_normal_shutdown(void);

/* KVDB互斥锁保护 */
void kvdb_lock(void);
void kvdb_unlock(void);
uint8_t kvdb_lock_was_timeout(void);  /* 检查上次kvdb_lock是否超时 */

/* 统一的设置加载入口：开机/重启调用，保证KV就绪且全局结构体与KV一致 */
void settings_init_load(void);
void fdb_start_tasks(void);

/* TSDB状态检查函数 */
uint8_t tsdb_is_ready(void);
void tsdb_debug_info(void);

/* TSDB测试函数 */
void tsdb_test_query(fdb_time_t from, fdb_time_t to);

/* TSDB 准备：DMA方式写入所有扇区的header（status+magic），用于产线预格式化 */
void tsdb_write_headers(void);

/* TSDB 格式化：整片擦除TSDB分区并重新写入headers */
void tsdb_format_full(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_FLASHDB_H */

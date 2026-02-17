/**
 * @file record_cache.h
 * @brief 记录缓存管理器（滑动窗口 + 智能预加载）
 * @details 支持5种记录类型的缓存管理：
 *          - 采样记录（start + complete）
 *          - 送样记录（start + complete）
 *          - 留样记录
 *          - 电源记录
 *          - 门禁记录
 *          缓存容量：140条/类型（覆盖20页，每页7条）
 *          智能预加载：检测翻页方向，异步预加载前后页
 */

#ifndef __RECORD_CACHE_H__
#define __RECORD_CACHE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>
#include "sampling.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "flashDB/app_flashdb.h"

//==============================================================================
// 配置参数
//==============================================================================

#define CACHE_PER_PAGE 7        // 每页显示条数
#define PRELOAD_THRESHOLD 2     // 触发预加载的边界距离（距离边界<2页时预加载）
#define PRELOAD_PAGE_COUNT 5    // 每次预加载页数（5页×7条=35条）

// 各类型独立缓存容量配置
#define CACHE_CAPACITY_SAMPLING  420  // 采样：60页×7条（可翻50页）
#define CACHE_CAPACITY_DELIVERY  105  // 送样：15页×7条
#define CACHE_CAPACITY_RETAIN    105  // 留样：15页×7条
#define CACHE_CAPACITY_POWER     70   // 电源：10页×7条（不变）
#define CACHE_CAPACITY_DOOR      70   // 门禁：10页×7条（不变）

// 兼容旧代码的默认容量（用于电源/门禁）
#define CACHE_CAPACITY CACHE_CAPACITY_POWER

//==============================================================================
// 枚举类型
//==============================================================================

/**
 * @brief 缓存类型
 */
typedef enum
{
    CACHE_TYPE_SAMPLING = 0, // 采样记录
    CACHE_TYPE_DELIVERY,     // 送样记录
    CACHE_TYPE_RETAIN,       // 留样记录
    CACHE_TYPE_POWER,        // 电源记录
    CACHE_TYPE_DOOR,         // 门禁记录
    CACHE_TYPE_COUNT         // 类型总数
} CacheType;

/**
 * @brief 预加载方向
 */
typedef enum
{
    PRELOAD_NONE = 0,    // 不需要预加载
    PRELOAD_FORWARD,     // 向前预加载（更早的记录）
    PRELOAD_BACKWARD     // 向后预加载（更新的记录）
} PreloadDirection;

//==============================================================================
// 数据结构
//==============================================================================

/**
 * @brief 采样记录缓存（滑动窗口420条）
 */
typedef struct
{
    SamplingStartRecord start_records[CACHE_CAPACITY_SAMPLING];
    SamplingCompleteRecord complete_records[CACHE_CAPACITY_SAMPLING];
    uint16_t count;              // 当前记录数
    uint16_t window_start_idx;   // 窗口起始索引（最旧记录）
    uint16_t window_end_idx;     // 窗口结束索引（最新记录）
    fdb_time_t oldest_ts;        // 最旧记录时间戳
    fdb_time_t newest_ts;        // 最新记录时间戳
    uint8_t last_page;           // 上次查询的页码
    int8_t last_direction;       // 上次翻页方向（1=向后，-1=向前，0=首次）
    uint8_t is_full;             // 缓存是否已满
    uint8_t reserved[1];
} SamplingRecordCache;

/**
 * @brief 送样记录缓存（滑动窗口105条）
 */
typedef struct
{
    DeliveryStartRecord start_records[CACHE_CAPACITY_DELIVERY];
    DeliveryCompleteRecord complete_records[CACHE_CAPACITY_DELIVERY];
    uint16_t count;
    uint16_t window_start_idx;
    uint16_t window_end_idx;
    fdb_time_t oldest_ts;
    fdb_time_t newest_ts;
    uint8_t last_page;
    int8_t last_direction;
    uint8_t is_full;
    uint8_t reserved[1];
} DeliveryRecordCache;

/**
 * @brief 留样记录缓存（滑动窗口105条）
 */
typedef struct
{
    RetainLogRecord records[CACHE_CAPACITY_RETAIN];
    uint16_t count;
    uint16_t window_start_idx;
    uint16_t window_end_idx;
    fdb_time_t oldest_ts;
    fdb_time_t newest_ts;
    uint8_t last_page;
    int8_t last_direction;
    uint8_t is_full;
    uint8_t reserved[1];
} RetainRecordCache;

/**
 * @brief 电源事件缓存记录（包含event_type）
 * @note 用于缓存，区分断电(0x0000)和恢复(0x0001)
 */
typedef struct __attribute__((packed))
{
    uint16_t event_type;  // 0x0000=断电, 0x0001=恢复
    uint32_t timestamp;   // 事件时间戳（Unix秒，1970基准）
} PowerEventCache_t;

/**
 * @brief 门禁事件缓存记录（包含event_type）
 * @note 用于缓存，区分开门(0x0000)和关门(0x0001)
 */
typedef struct __attribute__((packed))
{
    uint16_t event_type;  // 0x0000=开门, 0x0001=关门
    uint32_t timestamp;   // 事件时间戳（Unix秒，1970基准）
} DoorEventCache_t;

/**
 * @brief 电源记录缓存（滑动窗口70条）
 */
typedef struct
{
    PowerEventCache_t events[CACHE_CAPACITY_POWER]; // 电源事件（6字节/条）
    uint16_t count;
    uint16_t window_start_idx;
    uint16_t window_end_idx;
    fdb_time_t oldest_ts;
    fdb_time_t newest_ts;
    uint8_t last_page;
    int8_t last_direction;
    uint8_t is_full;
    uint8_t reserved[1];
} PowerRecordCache;

/**
 * @brief 门禁记录缓存（滑动窗口70条）
 */
typedef struct
{
    DoorEventCache_t events[CACHE_CAPACITY_DOOR]; // 门禁事件（6字节/条）
    uint16_t count;
    uint16_t window_start_idx;
    uint16_t window_end_idx;
    fdb_time_t oldest_ts;
    fdb_time_t newest_ts;
    uint8_t last_page;
    int8_t last_direction;
    uint8_t is_full;
    uint8_t reserved[1];
} DoorRecordCache;

/**
 * @brief 预加载请求结构体
 */
typedef struct
{
    CacheType cache_type;          // 缓存类型
    PreloadDirection direction;    // 预加载方向
    fdb_time_t oldest_ts;          // 最旧记录时间戳
    fdb_time_t newest_ts;          // 最新记录时间戳
    uint8_t page_count;            // 预加载页数
    uint8_t reserved[3];
} PreloadRequest;

/**
 * @brief 记录缓存管理器
 */
typedef struct
{
    SamplingRecordCache sampling;
    DeliveryRecordCache delivery;
    RetainRecordCache retain;
    PowerRecordCache power;        // 电源记录缓存
    DoorRecordCache door;          // 门禁记录缓存

    uint8_t initialized;           // 是否已初始化
    uint8_t reserved[3];
    SemaphoreHandle_t mutex;       // 互斥锁

    // 预加载任务句柄（用于异步预加载）
    TaskHandle_t preload_task_sampling;
    TaskHandle_t preload_task_delivery;
    TaskHandle_t preload_task_retain;
    TaskHandle_t preload_task_power;
    TaskHandle_t preload_task_door;

    // MQTT发送状态跟踪（每种类型的上次发送时间戳）
    uint32_t mqtt_last_sent_ts[5];
} RecordCacheManager;

//==============================================================================
// 全局变量声明
//==============================================================================

extern RecordCacheManager g_cache_mgr;

//==============================================================================
// 函数声明
//==============================================================================

/**
 * @brief 初始化缓存管理器
 * @note 从TSDB加载最新140条记录到各个缓存
 * @return 1-成功 0-失败
 */
uint8_t cache_manager_init(void);

/**
 * @brief 反初始化缓存管理器
 */
void cache_manager_deinit(void);

//------------------------------------------------------------------------------
// 添加记录接口（滑动窗口更新）
//------------------------------------------------------------------------------

/**
 * @brief 添加采样记录到缓存
 * @param start 采样开始记录
 * @param complete 采样完成记录
 * @note 自动维护FIFO滑动窗口（满时移除最旧记录）
 */
void cache_add_sampling(const SamplingStartRecord *start,
                        const SamplingCompleteRecord *complete);

/**
 * @brief 添加送样记录到缓存
 */
void cache_add_delivery(const DeliveryStartRecord *start,
                        const DeliveryCompleteRecord *complete);

/**
 * @brief 添加留样记录到缓存
 */
void cache_add_retain(const RetainLogRecord *record);

/**
 * @brief 添加电源记录到缓存
 * @param event_type 事件类型（POWER_EVT_FAILURE_DETECTED等）
 * @param timestamp 事件时间戳
 */
void cache_add_power(uint16_t event_type, uint32_t timestamp);

/**
 * @brief 添加门禁记录到缓存
 * @param event_type 事件类型（DOOR_EVT_UNLOCKED/DOOR_EVT_LOCKED）
 * @param timestamp 事件时间戳
 */
void cache_add_door(uint16_t event_type, uint32_t timestamp);

//------------------------------------------------------------------------------
// 查询接口
//------------------------------------------------------------------------------

/**
 * @brief 查询采样记录
 * @param page 页码（从0开始）
 * @param per_page 每页条数
 * @param out_starts 输出缓冲区（采样开始记录）
 * @param out_completes 输出缓冲区（采样完成记录）
 * @param out_count 输出实际数量
 * @return 1-缓存命中 0-缓存未命中（需查TSDB）
 */
uint8_t cache_query_sampling(uint8_t page, uint8_t per_page,
                              SamplingStartRecord *out_starts,
                              SamplingCompleteRecord *out_completes,
                              uint8_t *out_count);

/**
 * @brief 查询送样记录
 */
uint8_t cache_query_delivery(uint8_t page, uint8_t per_page,
                              DeliveryStartRecord *out_starts,
                              DeliveryCompleteRecord *out_completes,
                              uint8_t *out_count);

/**
 * @brief 查询留样记录
 */
uint8_t cache_query_retain(uint8_t page, uint8_t per_page,
                            RetainLogRecord *out_records,
                            uint8_t *out_count);

/**
 * @brief 查询电源记录
 */
uint8_t cache_query_power(uint8_t page, uint8_t per_page,
                           PowerEventCache_t *out_events,
                           uint8_t *out_count);

/**
 * @brief 查询门禁记录
 */
uint8_t cache_query_door(uint8_t page, uint8_t per_page,
                          DoorEventCache_t *out_events,
                          uint8_t *out_count);

//------------------------------------------------------------------------------
// 动态加载接口（超出缓存范围时调用）
//------------------------------------------------------------------------------

/**
 * @brief 动态加载更多采样记录
 * @return 新加载的记录数
 */
uint8_t cache_load_more_sampling(void);

/**
 * @brief 动态加载更多送样记录
 */
uint8_t cache_load_more_delivery(void);

/**
 * @brief 动态加载更多留样记录
 */
uint8_t cache_load_more_retain(void);

/**
 * @brief 动态加载更多电源记录
 */
uint8_t cache_load_more_power(void);

/**
 * @brief 动态加载更多门禁记录
 */
uint8_t cache_load_more_door(void);

/**
 * @brief 获取缓存总记录数
 */
uint16_t cache_get_count(CacheType type);

//------------------------------------------------------------------------------
// 滑动窗口操作
//------------------------------------------------------------------------------

/**
 * @brief 向前滑动窗口（加载更新的记录到后端）
 * @param type 缓存类型
 * @param new_records 新记录缓冲区
 * @param new_count 新记录数量
 */
void cache_slide_window_forward(CacheType type,
                                 void *new_records,
                                 uint16_t new_count);

/**
 * @brief 向后滑动窗口（加载更早的记录到前端）
 * @param type 缓存类型
 * @param new_records 新记录缓冲区
 * @param new_count 新记录数量
 */
void cache_slide_window_backward(CacheType type,
                                  void *new_records,
                                  uint16_t new_count);

/**
 * @brief 重建缓存窗口（围绕指定页码）
 * @param type 缓存类型
 * @param page 页码中心
 * @note 加载该页前后各70条记录
 */
void cache_rebuild_window_around_page(CacheType type, uint8_t page);

//------------------------------------------------------------------------------
// 智能预加载
//------------------------------------------------------------------------------

/**
 * @brief 决策是否需要预加载
 * @param requested_page 请求的页码
 * @param cached_start_page 缓存起始页码
 * @param cached_end_page 缓存结束页码
 * @param last_direction 上次翻页方向
 * @return 预加载方向
 */
PreloadDirection decide_preload(uint8_t requested_page,
                                 uint8_t cached_start_page,
                                 uint8_t cached_end_page,
                                 int8_t last_direction);

/**
 * @brief 异步预加载任务
 * @param param PreloadRequest指针
 */
void preload_task(void *param);

//------------------------------------------------------------------------------
// 工具函数
//------------------------------------------------------------------------------

/**
 * @brief 获取缓存统计信息
 * @param type 缓存类型
 * @param out_count 当前记录数
 * @param out_oldest_ts 最旧时间戳
 * @param out_newest_ts 最新时间戳
 */
void cache_get_stats(CacheType type,
                     uint16_t *out_count,
                     fdb_time_t *out_oldest_ts,
                     fdb_time_t *out_newest_ts);

/**
 * @brief 清空指定类型缓存
 */
void cache_clear(CacheType type);

/**
 * @brief 清空所有缓存
 */
void cache_clear_all(void);

#ifdef __cplusplus
}
#endif

#endif // __RECORD_CACHE_H__

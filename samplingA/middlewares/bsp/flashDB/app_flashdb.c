#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "app_flashdb.h"
#include "spi_flash.h"
#include "fal.h"
#include "freertos_app.h" /* for wdt_counter_reload and mutex */
#include "fdb_low_lvl.h"
#include "sampling_time.h"
#include "rtc.h"
#include "record_cache.h"

/* 外部全局变量声明 */
extern SampleConfig g_SampleConfig;
extern SampleDeliveryIntervalConfig g_DeliveryConfig;
extern RetainSampleModeConfig g_RetainSampleConfig;
extern CommSettingConfig g_CommSettingConfig;
extern SystemSettingConfig g_SystemSettingConfig;
extern CalibrationParams_t g_CalibrationParams;
extern RetainBottleState g_RetainBottleState;

/* FreeRTOS */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* 1970-01-01到2000-01-01的秒数：30年+7个闰日 */
#define UNIX_OFFSET_2000 ((30UL * 365UL + 7UL) * 24UL * 3600UL)

/* 喂狗函数声明（定义在fal_flash_AT32_port.c） */
extern void feed_dog(void);

#ifndef FDB_LOG_TAG
#define FDB_LOG_TAG "[tsl]"
#endif

/* ================= 内部对象 ================= */
struct fdb_kvdb g_kvdb_obj;
fdb_kvdb_t g_kvdb = &g_kvdb_obj;

static struct fdb_tsdb g_tsdb_obj;
static fdb_tsdb_t g_tsdb = &g_tsdb_obj;

/* TSDB 时间戳单调器 */
static fdb_time_t g_tsdb_prev_time = 0;
/* 时间戳合理性阈值：30分钟（正常校时最多10分钟，留余量） */
#define TSDB_TIME_TOLERANCE_SEC 1800
/* 就绪标志 */
static volatile uint8_t g_tsdb_ready = 0;

/* ================= 时间戳回调（使用RTC硬件，基准1970年） ================= */
static fdb_time_t tsdb_time_cb(void)
{
  /* ★ 使用rtc_counter_get()获取Unix时间戳（1970年基准） */
  extern uint32_t rtc_counter_get(void);
  fdb_time_t now_ts = (fdb_time_t)rtc_counter_get();

  /* 异常检测：如果历史时间戳超前当前RTC超过阈值，认为被污染，重置 */
  if (g_tsdb_prev_time > now_ts + TSDB_TIME_TOLERANCE_SEC)
  {
    printf("TSDB警告：时间戳异常(prev=%lu, rtc=%lu)，已重置\r\n",
           (unsigned long)g_tsdb_prev_time, (unsigned long)now_ts);
    g_tsdb_prev_time = now_ts;
  }

  /* 单调性保证：防止正常校时导致的时间回退（≤30分钟范围内） */
  if (now_ts <= g_tsdb_prev_time)
  {
    now_ts = g_tsdb_prev_time + 1;
  }
  g_tsdb_prev_time = now_ts;

  return now_ts;
}

/* ================= KVDB 封装 ================= */
uint8_t cfg_kv_init(void)
{
  if (fal_init() <= 0)
    return 0u;
  static struct fdb_default_kv_node nodes[] = {
      {(char *)"sample_cfg", NULL, 0},
      {(char *)"delivery_cfg", NULL, 0},
      {(char *)"retain_cfg", NULL, 0},
      {(char *)"comm_cfg", NULL, 0},
      {(char *)"system_cfg", NULL, 0},
      {(char *)"calib_params", NULL, 0},
  };
  static struct fdb_default_kv def = {nodes, sizeof(nodes) / sizeof(nodes[0])};
  return (fdb_kvdb_init(g_kvdb, "kvdb", "fdb_kvdb", &def, NULL) == FDB_NO_ERR) ? 1u : 0u;
}

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

/* 固定键名（放此模块内部） */
static const char *KV_SAMPLE = "sample_cfg";
static const char *KV_DELIVERY = "delivery_cfg";
static const char *KV_RETAIN = "retain_cfg";
static const char *KV_COMM = "comm_cfg";
static const char *KV_SYSTEM = "system_cfg";
static const char *KV_CALIB = "calib_params";
static const char *KV_RETAIN_STATE = "retain_state";
static const char *KV_POWER_RECORD = "power_record";
static const char *KV_MQTT_STATE = "mqtt_state";
static const char *KV_LOCATION = "lbs_location";

/* 断电记录全局变量 */
static PowerRecord_t g_power_record = {0};
SemaphoreHandle_t g_kvdb_mutex = NULL;  /* 移除static，允许外部访问 */
static SemaphoreHandle_t g_tsdb_mutex = NULL; /* TSDB互斥锁 */

/* 与应用结构体尺寸绑定，接口保持应用层函数名以减少改动 */
uint8_t cfg_save_sample(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;  /* 锁超时，不执行写入 */
    }
    result = kv_set_blob(KV_SAMPLE, p, sizeof(SampleConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_sample(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_SAMPLE, p, sizeof(SampleConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_delivery(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;  /* 锁超时，不执行写入 */
    }
    result = kv_set_blob(KV_DELIVERY, p, sizeof(SampleDeliveryIntervalConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_delivery(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_DELIVERY, p, sizeof(SampleDeliveryIntervalConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_retain(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        printf("[KVDB-Flash] cfg_save_retain: 跳过(锁超时)\r\n");
        return 0;  /* 锁超时，不执行写入 */
    }
    result = kv_set_blob(KV_RETAIN, p, sizeof(RetainSampleModeConfig));
    kvdb_unlock();
    printf("[KVDB-Flash] cfg_save_retain: %s (size=%u)\r\n",
           result ? "OK" : "FAIL", (unsigned)sizeof(RetainSampleModeConfig));
    return result;
}
uint8_t cfg_load_retain(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_RETAIN, p, sizeof(RetainSampleModeConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_comm(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_COMM, p, sizeof(CommSettingConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_comm(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_COMM, p, sizeof(CommSettingConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_system(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_SYSTEM, p, sizeof(SystemSettingConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_system(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_SYSTEM, p, sizeof(SystemSettingConfig));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_calib(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_CALIB, p, sizeof(CalibrationParams_t));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_calib(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_CALIB, p, sizeof(CalibrationParams_t));
    kvdb_unlock();
    return result;
}
uint8_t cfg_save_retain_state(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        printf("[KVDB-Flash] cfg_save_retain_state: 跳过(锁超时)\r\n");
        return 0;  /* 锁超时，不执行写入 */
    }
    result = kv_set_blob(KV_RETAIN_STATE, p, sizeof(RetainBottleState));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_retain_state(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_RETAIN_STATE, p, sizeof(RetainBottleState));
    kvdb_unlock();
    return result;
}

/**
 * @brief 带超时的留样配置保存（非阻塞）
 * @param p 配置数据指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 0=成功, 1=获取锁超时, 2=保存失败
 */
uint8_t cfg_save_retain_timeout(const void *p, uint32_t timeout_ms) {
    if (g_kvdb_mutex == NULL) {
        g_kvdb_mutex = xSemaphoreCreateMutex();
    }
    if (g_kvdb_mutex == NULL) {
        return 2;
    }
    if (xSemaphoreTake(g_kvdb_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 1; // 获取锁超时
    }
    uint8_t result = kv_set_blob(KV_RETAIN, p, sizeof(RetainSampleModeConfig));
    xSemaphoreGive(g_kvdb_mutex);
    return result == 0 ? 2 : 0;
}

/**
 * @brief 带超时的瓶位状态保存（非阻塞）
 * @param p 状态数据指针
 * @param timeout_ms 超时时间（毫秒）
 * @return 0=成功, 1=获取锁超时, 2=保存失败
 */
uint8_t cfg_save_retain_state_timeout(const void *p, uint32_t timeout_ms) {
    if (g_kvdb_mutex == NULL) {
        g_kvdb_mutex = xSemaphoreCreateMutex();
    }
    if (g_kvdb_mutex == NULL) {
        return 2;
    }
    if (xSemaphoreTake(g_kvdb_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return 1; // 获取锁超时
    }
    uint8_t result = kv_set_blob(KV_RETAIN_STATE, p, sizeof(RetainBottleState));
    xSemaphoreGive(g_kvdb_mutex);
    return result == 0 ? 2 : 0;
}

uint8_t cfg_save_power_record(const void *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_POWER_RECORD, p, sizeof(PowerRecord_t));
    kvdb_unlock();
    return result;
}
uint8_t cfg_load_power_record(void *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_POWER_RECORD, p, sizeof(PowerRecord_t));
    kvdb_unlock();
    return result;
}

uint8_t cfg_save_mqtt_state(const MqttSentState_t *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_MQTT_STATE, p, sizeof(MqttSentState_t));
    kvdb_unlock();
    return result;
}

uint8_t cfg_load_mqtt_state(MqttSentState_t *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_MQTT_STATE, p, sizeof(MqttSentState_t));
    kvdb_unlock();
    return result;
}

uint8_t cfg_save_location(const LocationInfo_t *p) {
    uint8_t result;
    kvdb_lock();
    if (kvdb_lock_was_timeout()) {
        return 0;
    }
    result = kv_set_blob(KV_LOCATION, p, sizeof(LocationInfo_t));
    kvdb_unlock();
    return result;
}

uint8_t cfg_load_location(LocationInfo_t *p) {
    uint8_t result;
    kvdb_lock();
    result = kv_get_blob(KV_LOCATION, p, sizeof(LocationInfo_t));
    kvdb_unlock();
    return result;
}

uint8_t cfg_reset_all(void)
{
  fdb_kv_del(g_kvdb, KV_SAMPLE);
  fdb_kv_del(g_kvdb, KV_DELIVERY);
  fdb_kv_del(g_kvdb, KV_RETAIN);
  fdb_kv_del(g_kvdb, KV_COMM);
  fdb_kv_del(g_kvdb, KV_SYSTEM);
  fdb_kv_del(g_kvdb, KV_CALIB);
  fdb_kv_del(g_kvdb, KV_RETAIN_STATE);
  fdb_kv_del(g_kvdb, KV_POWER_RECORD);
  return 1u;
}

/* ===== TSDB 互斥锁函数 ===== */
void tsdb_app_lock(void)
{
    if (g_tsdb_mutex == NULL)
    {
        g_tsdb_mutex = xSemaphoreCreateMutex();
    }
    if (g_tsdb_mutex != NULL)
    {
        xSemaphoreTake(g_tsdb_mutex, portMAX_DELAY);
    }
}

void tsdb_app_unlock(void)
{
    if (g_tsdb_mutex != NULL)
    {
        xSemaphoreGive(g_tsdb_mutex);
    }
}

/* ===== 配置有效性校验函数 ===== */
static void validate_all_configs(void)
{
    uint8_t need_save_sample = 0;
    uint8_t need_save_retain = 0;
    uint8_t need_save_comm = 0;
    SampleConfig default_sample = {
        .BucketAB = 0,
        .SamplingMode = 0,
        .SamplingImproveTime = 30,
        .SampleInterval = 15,
        .TubeHoldTime = 30,
        .SampleVolume = 500,
        .CycleTime = 60,
        .BlowbackTime = 10,
        .BucketDrainTime = 10,
        .AnalysisTime = 30,
        .DischargeVolume = 100,
        .FlowRatio = 1000,
        .FlowStart = 100,
        .FlowStop = 10
    };

    /* 检查采样配置 */
    if (g_SampleConfig.SampleInterval < 1 || g_SampleConfig.SampleInterval > 1440)
    {
        printf("[配置警告] 采样间隔无效(%d分钟)，恢复默认值15分钟\r\n",
               g_SampleConfig.SampleInterval);
        g_SampleConfig.SampleInterval = default_sample.SampleInterval;
        need_save_sample = 1;
    }

    if (g_SampleConfig.SampleVolume < 10 || g_SampleConfig.SampleVolume > 2000)
    {
        printf("[配置警告] 采样量无效(%dml)，恢复默认值500ml\r\n",
               g_SampleConfig.SampleVolume);
        g_SampleConfig.SampleVolume = default_sample.SampleVolume;
        need_save_sample = 1;
    }

    if (g_SampleConfig.CycleTime < g_SampleConfig.SampleInterval || g_SampleConfig.CycleTime > 1440)
    {
        printf("[配置警告] 周期时间无效(%d分钟)，调整为%d分钟\r\n",
               g_SampleConfig.CycleTime,
               (g_SampleConfig.SampleInterval > default_sample.CycleTime) ?
               g_SampleConfig.SampleInterval : default_sample.CycleTime);
        g_SampleConfig.CycleTime = (g_SampleConfig.SampleInterval > default_sample.CycleTime) ?
                                  g_SampleConfig.SampleInterval : default_sample.CycleTime;
        need_save_sample = 1;
    }

    if (g_SampleConfig.SamplingMode > 3)
    {
        printf("[配置警告] 采样模式无效(%d)，恢复默认值0\r\n", g_SampleConfig.SamplingMode);
        g_SampleConfig.SamplingMode = default_sample.SamplingMode;
        need_save_sample = 1;
    }

    /* 检查留样配置 */
    if (g_RetainSampleConfig.bottleNumber < 1 || g_RetainSampleConfig.bottleNumber > 24)
    {
        printf("[配置警告] 瓶号无效(%d)，恢复默认值1\r\n", g_RetainSampleConfig.bottleNumber);
        g_RetainSampleConfig.bottleNumber = 1;
        need_save_retain = 1;
    }

    if (g_RetainSampleConfig.Mode > 3)
    {
        printf("[配置警告] 留样模式无效(%d)，恢复默认值0\r\n", g_RetainSampleConfig.Mode);
        g_RetainSampleConfig.Mode = 0;
        need_save_retain = 1;
    }

    /* 检查通信配置 */
    if (g_CommSettingConfig.DeviceAddr == 0 || g_CommSettingConfig.DeviceAddr > 247)
    {
        printf("[配置警告] 设备地址无效(%d)，恢复默认值1\r\n", g_CommSettingConfig.DeviceAddr);
        g_CommSettingConfig.DeviceAddr = 1;
        need_save_comm = 1;
    }

    if (g_CommSettingConfig.AutoCalibration > COMM_PROTOCOL_SICHUAN)
    {
        printf("[配置警告] 协议选择无效(%d)，恢复默认值%d\r\n",
               g_CommSettingConfig.AutoCalibration, COMM_PROTOCOL_DAYUE);
        g_CommSettingConfig.AutoCalibration = COMM_PROTOCOL_DAYUE;
        need_save_comm = 1;
    }

   
    /* 保存修正后的配置 */
    if (need_save_sample)
    {
        printf("[配置] 保存修正后的采样配置\r\n");
        cfg_save_sample(&g_SampleConfig);
    }

    if (need_save_retain)
    {
        printf("[配置] 保存修正后的留样配置\r\n");
        cfg_save_retain(&g_RetainSampleConfig);
    }

    if (need_save_comm)
    {
        printf("[配置] 保存修正后的通信配置\r\n");
        cfg_save_comm(&g_CommSettingConfig);
    }
}

/* ===== 统一的设置加载入口 ===== */
void settings_init_load(void)
{
  if (!cfg_kv_init())
    return;
  /* 采样 */
  SampleConfig tmpS = g_SampleConfig; /* 默认值来自全局 */
  if (!cfg_load_sample(&tmpS))
  {
    (void)cfg_save_sample(&g_SampleConfig);
  }
  else
  {
    g_SampleConfig = tmpS;
  }

  /* 送样 */
  SampleDeliveryIntervalConfig tmpD = g_DeliveryConfig;
  if (!cfg_load_delivery(&tmpD))
  {
    (void)cfg_save_delivery(&g_DeliveryConfig);
  }
  else
  {
    g_DeliveryConfig = tmpD;
  }
  /* 留样 */
  RetainSampleModeConfig tmpR = g_RetainSampleConfig;
  if (!cfg_load_retain(&tmpR))
  {
    (void)cfg_save_retain(&g_RetainSampleConfig);
  }
  else
  {
    g_RetainSampleConfig = tmpR;
  }
  /* 留样瓶位状态 */
  extern RetainBottleState g_RetainBottleState; /* 由sampling.c定义 */
  RetainBottleState tmpRB = g_RetainBottleState;
  if (!cfg_load_retain_state(&tmpRB))
  {
    (void)cfg_save_retain_state(&g_RetainBottleState);
  }
  else
  {
    g_RetainBottleState = tmpRB;
  }
  /* 通讯/系统/校准 */
  CommSettingConfig tmpC = g_CommSettingConfig;
  if (!cfg_load_comm(&tmpC))
  {
    (void)cfg_save_comm(&g_CommSettingConfig);
  }
  else
  {
    g_CommSettingConfig = tmpC;
  }
  SystemSettingConfig tmpSys = g_SystemSettingConfig;
  if (!cfg_load_system(&tmpSys))
  {
    (void)cfg_save_system(&g_SystemSettingConfig);
  }
  else
  {
    g_SystemSettingConfig = tmpSys;
  }
  CalibrationParams_t tmpCal = g_CalibrationParams;
  if (!cfg_load_calib(&tmpCal))
  {
    (void)cfg_save_calib(&g_CalibrationParams);
  }
  else
  {
    g_CalibrationParams = tmpCal;
  }

  /* 断电记录初始化 */
  power_record_init();

  /* 配置有效性校验 */
  validate_all_configs();
}

/* ================= TSDB 初始化 ================= */
static uint8_t tsdb_init_internal(void)
{
  /* 注意：请在RTC已校准后调用 fdb_start_tasks()，确保时间正确 */
  uint32_t sec = 4096; /* 分区扇区大小 */
  fdb_tsdb_control(g_tsdb, FDB_TSDB_CTRL_SET_SEC_SIZE, &sec);

  // TSDB初始化前喂狗
  feed_dog();

  /* 带超时的初始化循环 */
  uint32_t start_time = xTaskGetTickCount();
  uint32_t timeout_ticks = pdMS_TO_TICKS(2000);  // 2秒超时
  fdb_err_t err;

  while ((xTaskGetTickCount() - start_time) < timeout_ticks)
  {
    wdt_counter_reload();  // 循环喂狗

    /* 先尝试初始化 */
    err = fdb_tsdb_init(g_tsdb, "tsdb", "fdb_tsdb", tsdb_time_cb, 1024, NULL);

    /* 如果成功，直接返回 */
    if (err == FDB_NO_ERR)
    {
      break;
    }

    /* 如果是其他错误（非初始化失败或读取错误），不需要重试 */
    if (err != FDB_INIT_FAILED && err != FDB_READ_ERR)
    {
      break;
    }

    /* 对于初始化失败或读取错误，短暂等待后重试 */
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  /* 检查是否超时或失败 */
  if (err != FDB_NO_ERR)
  {
    /* 尝试格式化重建 */
    tsdb_format_full();

    /* 格式化后再次尝试初始化 */
    wdt_counter_reload();
    err = fdb_tsdb_init(g_tsdb, "tsdb", "fdb_tsdb", tsdb_time_cb, 1024, NULL);

    if (err != FDB_NO_ERR)
    {
      g_tsdb_ready = 0;
      return 0u;
    }
  }

  uint8_t rollover_en = 1;
  fdb_tsdb_control(g_tsdb, FDB_TSDB_CTRL_SET_ROLLOVER, &rollover_en);

  fdb_time_t last_time = 0;
  fdb_tsdb_control(g_tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, &last_time);

  /* 初始化时检查历史时间戳合理性 */
  extern uint32_t rtc_counter_get(void);
  fdb_time_t now_rtc = (fdb_time_t)rtc_counter_get();

  if (last_time > now_rtc + TSDB_TIME_TOLERANCE_SEC)
  {
    g_tsdb_prev_time = now_rtc;
  }
  else
  {
    g_tsdb_prev_time = last_time;
  }

  return 1u;
}

void fdb_start_tasks(void)
{
  uint8_t ok = tsdb_init_internal();

  if (ok)
  {
    g_tsdb_ready = 1;
  }
}

/* TSDB状态检查函数 */
uint8_t tsdb_is_ready(void)
{
  return g_tsdb_ready;
}

void tsdb_debug_info(void)
{
  printf("TSDB_DEBUG: ready=%u\r\n", g_tsdb_ready);
  if (g_tsdb_ready)
  {
    fdb_time_t last_time = 0;
    fdb_tsdb_control(g_tsdb, FDB_TSDB_CTRL_GET_LAST_TIME, &last_time);
    printf("TSDB_DEBUG: last_time=%lu\r\n", (unsigned long)last_time);

    // 检查分区状态
    const struct fal_partition *tsdb = fal_partition_find("fdb_tsdb");
    if (tsdb)
    {
      printf("TSDB_DEBUG: partition found, size=%lu\r\n", (unsigned long)tsdb->len);
    }
    else
    {
      printf("TSDB_DEBUG: partition NOT found!\r\n");
    }

    // 检查当前时间
    fdb_time_t current_time = tsdb_time_cb();
    printf("TSDB_DEBUG: current_time=%lu\r\n", (unsigned long)current_time);
  }
  else
  {
    printf("TSDB_DEBUG: TSDB not initialized\r\n");
  }
}

/* ================= 事件直写与回调读取（方案1：遵循FlashDB标准） ================= */

/**
 * @brief 写入TSDB事件（遵循FlashDB标准设计）
 * @param event_type 事件类型（2字节）
 * @param body 事件体数据指针
 * @param body_len 事件体长度
 * @return 1=成功 0=失败
 *
 * 存储格式：[2字节event_type][body数据]
 * 时间戳由FlashDB自动管理（tsl->time）
 */
/* 2026年1月1日 00:00:00 UTC 的Unix时间戳 */
#define TSDB_MIN_VALID_TIMESTAMP 1735689600UL

uint8_t tsdb_event_append(uint16_t event_type, const void *body, size_t body_len)
{
  uint8_t result = 0u;

  tsdb_app_lock();  /* 应用层加锁 */

  if (!g_tsdb_ready)
  {
    printf("TSDB_W: Not ready\r\n");
    tsdb_app_unlock();
    return 0u;
  }

  /* ★ 时间合理性检查：拒绝写入早于2026年的记录 */
  extern uint32_t rtc_counter_get(void);
  uint32_t current_ts = rtc_counter_get();
  if (current_ts < TSDB_MIN_VALID_TIMESTAMP)
  {
    printf("TSDB_W: RTC时间异常(ts=%lu < 2026年)，拒绝写入\r\n", (unsigned long)current_ts);
    tsdb_app_unlock();
    return 0u;
  }

  if (!body && body_len > 0)
  {
    printf("TSDB_W: Invalid params\r\n");
    tsdb_app_unlock();
    return 0u;
  }

  // 组合数据：2字节event_type + body
  uint8_t buf[256]; // 预留足够空间
  size_t total_len = 2 + body_len;

  if (total_len > sizeof(buf))
  {
    printf("TSDB_W: Data too large: %u bytes\r\n", (unsigned int)total_len);
    tsdb_app_unlock();
    return 0u;
  }

  // 存储event_type（小端序）
  buf[0] = (uint8_t)(event_type & 0xFF);
  buf[1] = (uint8_t)((event_type >> 8) & 0xFF);

  // 复制body数据
  if (body_len > 0)
  {
    memcpy(buf + 2, body, body_len);
  }

  // 写入FlashDB（时间戳自动由tsdb_time_cb生成）
  struct fdb_blob blob;
  fdb_err_t ret = fdb_tsl_append(g_tsdb, fdb_blob_make(&blob, buf, total_len));

  if (ret != FDB_NO_ERR)
  {
    printf("TSDB_W: Failed, ret=%d\r\n", ret);
    result = 0u;
  }
  else
  {
    printf("TSDB_W: type=%u len=%u OK, ts=%lu\r\n", event_type, (unsigned int)body_len, (unsigned long)g_tsdb_prev_time);
    result = 1u;
  }

  tsdb_app_unlock();  /* 应用层解锁 */
  return result;
}

/* 迭代回调上下文 */
typedef struct
{
  tsdb_event_iter_cb user_cb;
  void *user_arg;
} _iter_ctx_t;

/* 反向迭代回调上下文（支持提前终止） */
typedef struct
{
  tsdb_event_iter_cb user_cb;
  void *user_arg;
  fdb_time_t end_ts;     // 结束时间（只遍历此时间之前的记录）
  fdb_time_t last_ts;    // 最后遍历到的时间戳
  uint32_t collected;    // 已收集的目标记录数
  uint32_t target_count; // 目标收集数量（达到后停止）
  uint8_t stop_flag;     // 停止标志
} _reverse_iter_ctx_t;

/* 全局迭代计数器（用于调试） */
uint32_t g_tsdb_iter_total = 0;

/**
 * @brief FlashDB TSL迭代回调适配器
 * @param tsl FlashDB的TSL记录指针
 * @param arg 用户上下文
 * @return false=继续遍历
 */
static bool _tsl_iter_cb(fdb_tsl_t tsl, void *arg)
{
  _iter_ctx_t *ctx = (_iter_ctx_t *)arg;
  uint8_t buf[256];
  struct fdb_blob blob;

  // ? 定期喂狗并让出CPU（每10条记录）
  extern uint32_t g_tsdb_iter_total;
  g_tsdb_iter_total++;

  // 关键：需要调用fdb_blob_read真正读取数据到buf中
  fdb_blob_read((fdb_db_t)g_tsdb, fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, buf, sizeof(buf))));

  size_t n = blob.saved.len;

  if (n < 2)
  {
    // 数据太短，至少需要2字节event_type
    return false;
  }

  // 解析event_type（小端序）
  uint16_t event_type = buf[0] | (buf[1] << 8);

  // 每100条记录喂狗、让出CPU（避免占用CPU过久）
  if ((g_tsdb_iter_total % 100) == 0)
  {
    feed_dog();
    extern void vTaskDelay(uint32_t);
    vTaskDelay(1); // 让出CPU
  }

  // 提取body指针和长度
  const void *body = (n > 2) ? (buf + 2) : NULL;
  size_t body_len = (n > 2) ? (n - 2) : 0;

  // 构造事件信息（使用FlashDB的时间戳）
  TsdbEventInfo info;
  info.event_type = event_type;
  info.ts = tsl->time; // 关键：使用FlashDB维护的时间戳
  info.body_len = body_len;

  // 回调用户
  if (ctx->user_cb)
  {
    ctx->user_cb(&info, body, ctx->user_arg);
  }

  return false; // 继续遍历
}

/**
 * @brief 按时间范围遍历TSDB事件
 * @param from 起始时间戳
 * @param to 结束时间戳
 * @param cb 回调函数
 * @param user 用户参数
 */
void tsdb_iter_range(fdb_time_t from, fdb_time_t to, tsdb_event_iter_cb cb, void *user)
{
  if (!cb || !g_tsdb_ready)
  {
    return;
  }

  // 查询前喂狗（查询可能需要很长时间）
  feed_dog();

  _iter_ctx_t ctx;
  ctx.user_cb = cb;
  ctx.user_arg = user;

  // 重置迭代计数器
  extern uint32_t g_tsdb_iter_total;
  g_tsdb_iter_total = 0;

  /* 按时间范围遍历 */
  fdb_tsl_iter_by_time(g_tsdb, from, to, _tsl_iter_cb, &ctx);

  // 查询后喂狗
  feed_dog();
}

/**
 * @brief 反向遍历回调适配器（支持提前终止）
 */
static bool _tsl_reverse_iter_cb(fdb_tsl_t tsl, void *arg)
{
  _reverse_iter_ctx_t *ctx = (_reverse_iter_ctx_t *)arg;

  static uint32_t iter_seq = 0;
  iter_seq++;

  // 检查停止标志
  if (ctx->stop_flag)
  {
    iter_seq = 0; // 重置计数
    return true; // 返回true停止遍历
  }

  // 检查时间范围（只处理end_ts之前的记录）
  if (tsl->time > ctx->end_ts)
  {
        return false; // 跳过此记录，继续遍历
  }

  
  // 读取记录数据
  uint8_t buf[256];
  struct fdb_blob blob;
  fdb_blob_read((fdb_db_t)g_tsdb, fdb_tsl_to_blob(tsl, fdb_blob_make(&blob, buf, sizeof(buf))));

  size_t n = blob.saved.len;
  if (n < 2)
  {
    return false; // 数据太短，跳过
  }

  // 解析event_type
  uint16_t event_type = buf[0] | (buf[1] << 8);

  // 构造事件信息
  TsdbEventInfo info;
  info.event_type = event_type;
  info.ts = tsl->time;
  info.body_len = (n > 2) ? (n - 2) : 0;

  const void *body = (n > 2) ? (buf + 2) : NULL;

  // 回调用户（用户回调内部会判断是否是目标类型并收集）
  if (ctx->user_cb)
  {
    /* 在回调前记录已收集数量 - LoadContext.count在偏移0处，类型为uint16_t */
    uint16_t *p_record_count = (uint16_t*)((uint8_t*)ctx->user_arg + 0);
    uint16_t old_count = *p_record_count;

        ctx->user_cb(&info, body, ctx->user_arg);

    /* 检查是否已收集新记录，更新collected计数和last_ts */
    uint16_t new_count = *p_record_count;
    if (new_count > old_count) {
      uint32_t collected_this_time = new_count - old_count;
      ctx->collected += collected_this_time;

      // 只有在实际收集到记录时才更新last_ts
      ctx->last_ts = tsl->time;
    }
  }

  /* 检查是否达到目标数量 */
  if (ctx->target_count > 0 && ctx->collected >= ctx->target_count) {
    ctx->stop_flag = 1;
        return true;  // 停止遍历
  }

  
  // 每50条记录喂狗
  static uint32_t iter_count = 0;
  if ((++iter_count % 50) == 0)
  {
    feed_dog();
  }

  return false; // 继续遍历
}

/**
 * @brief 反向遍历TSDB（从最新到最旧），支持提前终止
 */
uint32_t tsdb_iter_reverse_until(fdb_time_t to, tsdb_event_iter_cb cb, void *user, fdb_time_t *out_last_ts)
{
  uint32_t result = 0;

  if (!cb || !g_tsdb_ready)
  {
    return 0;
  }

  tsdb_app_lock();  /* 读取也需要加锁 */

  feed_dog();

  _reverse_iter_ctx_t ctx;
  ctx.user_cb = cb;
  ctx.user_arg = user;
  ctx.end_ts = to;
  ctx.last_ts = 0;
  ctx.collected = 0;
  /* 直接从user参数偏移位置获取target_count（LogQueryContext结构体） */
  /* LogQueryContext中target_count在第28字节的位置 */
  ctx.target_count = *(uint32_t*)((uint8_t*)user + 28);
  ctx.stop_flag = 0;

  fdb_tsl_iter_reverse(g_tsdb, _tsl_reverse_iter_cb, &ctx);

  if (out_last_ts)
  {
    *out_last_ts = ctx.last_ts;
  }

  result = ctx.collected;

  feed_dog();

  tsdb_app_unlock();
  return result;
}

/* ================= 产线工具：TSDB分区预格式化 ================= */

/**
 * @brief TSDB 格式化：整片擦除TSDB分区并重新写入headers
 * 功能：
 * 1. 找到TSDB分区
 * 2. 逐扇区擦除整个TSDB分区（支持8MB）
 * 3. 调用tsdb_write_headers重新写入格式化信息
 */
void tsdb_format_full(void)
{
    /* 1) 查找TSDB分区 */
    const struct fal_partition *tsdb = fal_partition_find("fdb_tsdb");
    if (!tsdb) {
        printf("TSDB格式化失败：找不到TSDB分区\r\n");
        return;
    }

    printf("开始格式化TSDB分区（大小=%lu KB）...\r\n", (unsigned long)(tsdb->len/1024));

    /* 2) 整片擦除TSDB分区（逐扇区，支持8MB） */
    {
        const uint32_t SECTOR = 4096;
        for (uint32_t off = 0; off < tsdb->len; off += SECTOR) {
            spiflash_sector_erase(off / SECTOR); /* 传扇区号 */
            wdt_counter_reload();
            /* ★ 喂狗：设置Task5和Task10事件位，防止长时间擦除导致软看门狗超时 */
            if (event_handle != NULL) {
                xEventGroupSetBits(event_handle, TASK5_EVENT_BIT | TASK10_EVENT_BIT);
            }
            if (((off / SECTOR) & 0x3F) == 0) {
                printf("擦除进度 %lu/%lu KB\r\n",
                       (unsigned long)(off/1024),
                       (unsigned long)(tsdb->len/1024));
            }
        }
        printf("TSDB擦除完成.\r\n");
    }

    /* 3) 调用全局实现：DMA 写 header 并校验 */
    tsdb_write_headers();
    printf("TSDB格式化完成. 建议重启设备或重新刷写固件.\r\n");
}

void tsdb_write_headers(void)
{
  const struct fal_partition *tsdb = fal_partition_find("fdb_tsdb");
  if (!tsdb)
  {
    printf("tsdb part missing\r\n");
    return;
  }
  const uint32_t SECTOR = 4096;
  const uint32_t nsec = (uint32_t)(tsdb->len / SECTOR);

  /* DMA 写读自检（第2扇区偏移32字节） */
  {
    uint8_t w[16], r[16];
    memset(w, 0x5A, sizeof(w));
    memset(r, 0x00, sizeof(r));
    uint32_t addr = SECTOR + 32;
    spiflash_write(w, addr, (uint32_t)sizeof(w));
    spiflash_read(r, addr, (uint32_t)sizeof(r));
    if (memcmp(w, r, sizeof(w)) != 0)
    {
      printf("DMA WR/RD verify fail\r\n");
      return;
    }
    printf("DMA R/W test OK\r\n");
  }

  /* 构造 status=EMPTY 与 magic */
  uint8_t status[FDB_STORE_STATUS_TABLE_SIZE];
  memset(status, 0xFF, sizeof(status));
  status[0] = 0x00;                    /* EMPTY 状态首字节写 0x00 */
  const uint32_t magic = 0x304C5354UL; /* 'T''S''L''0' */
  uint8_t mbuf[4];
  memcpy(mbuf, &magic, 4);

  printf("Start TSDB header formatting (DMA)...\r\n");
  for (uint32_t i = 0; i < nsec; ++i)
  {
    uint32_t base = i * SECTOR;
    /* 写 status 表 */
    spiflash_write(status, base + 0, (uint32_t)sizeof(status));
    wdt_counter_reload();
    /* 写 magic 紧随其后 */
    spiflash_write(mbuf, base + (uint32_t)sizeof(status), 4);
    wdt_counter_reload();
    /* ★ 喂狗：设置Task5和Task10事件位，防止长时间写header导致软看门狗超时 */
    if (event_handle != NULL) {
        xEventGroupSetBits(event_handle, TASK5_EVENT_BIT | TASK10_EVENT_BIT);
    }
    /* 校验（首扇区 + 每64扇区） */
    if ((i == 0) || ((i & 0x3F) == 0))
    {
      uint8_t chk0 = 0, mrb[4];
      uint32_t mr = 0;
      spiflash_read(&chk0, base + 0, 1);
      spiflash_read(mrb, base + (uint32_t)sizeof(status), 4);
      memcpy(&mr, mrb, 4);
      if (chk0 != 0x00 || mr != magic)
      {
        printf("verify fail @%lu st0=0x%02X m=0x%08lX\r\n", (unsigned long)i, chk0, (unsigned long)mr);
        return;
      }
      printf("header %lu/%lu\r\n", (unsigned long)i, (unsigned long)nsec);
    }
  }
  printf("TSDB header formatted.\r\n");
}

/* ===== 断电记录功能实现 ===== */

/* KVDB互斥锁保护 - 带超时防止死锁 */
#define KVDB_LOCK_TIMEOUT_MS  5000  /* 5秒超时 */
static volatile uint8_t s_kvdb_lock_timeout_flag = 0;  /* 锁超时标志 */

void kvdb_lock(void)
{
  if (g_kvdb_mutex == NULL)
  {
    g_kvdb_mutex = xSemaphoreCreateMutex();
  }
  if (g_kvdb_mutex != NULL)
  {
    s_kvdb_lock_timeout_flag = 0;
    if (xSemaphoreTake(g_kvdb_mutex, pdMS_TO_TICKS(KVDB_LOCK_TIMEOUT_MS)) != pdTRUE)
    {
      /* 获取锁超时，打印警告但不阻塞 */
      printf("[KVDB] 警告：获取锁超时(%dms)，可能存在锁竞争\r\n", KVDB_LOCK_TIMEOUT_MS);
      s_kvdb_lock_timeout_flag = 1;
    }
  }
}

/* 检查上次kvdb_lock是否超时 */
uint8_t kvdb_lock_was_timeout(void)
{
  return s_kvdb_lock_timeout_flag;
}

void kvdb_unlock(void)
{
  if (g_kvdb_mutex != NULL)
  {
    xSemaphoreGive(g_kvdb_mutex);
  }
}

/* 断电记录初始化 */
void power_record_init(void)
{
  // 创建互斥锁
  if (g_kvdb_mutex == NULL)
  {
    g_kvdb_mutex = xSemaphoreCreateMutex();
  }

  // 从KVDB加载断电记录
  PowerRecord_t temp_record;
  if (cfg_load_power_record(&temp_record))
  {
    g_power_record = temp_record;

    /* 兼容旧固件：历史保存可能是“2000年基准秒数”，需要迁移到Unix秒 */
    if (g_power_record.last_heartbeat_time > 0 && g_power_record.last_heartbeat_time < UNIX_OFFSET_2000) {
      g_power_record.last_heartbeat_time += UNIX_OFFSET_2000;
    }
    if (g_power_record.last_shutdown_time > 0 && g_power_record.last_shutdown_time < UNIX_OFFSET_2000) {
      g_power_record.last_shutdown_time += UNIX_OFFSET_2000;
    }
  }
  else
  {
    // 初始化默认值
    memset(&g_power_record, 0, sizeof(g_power_record));
  g_power_record.last_heartbeat_time = rtc_counter_get();
    cfg_save_power_record(&g_power_record);
  }
}

/* 更新心跳时间戳 */
void power_update_heartbeat(void)
{
  uint32_t current_time = rtc_counter_get();

  // 防止时间倒退
  if (current_time > g_power_record.last_heartbeat_time)
  {
    // 只更新内存中的值，不立即写入Flash
    g_power_record.last_heartbeat_time = current_time;

    //        printf("[POWER] 心跳更新：%lu\r\n", (unsigned long)current_time);
  }
}

/* 内部版本：保存断电记录（不加锁，调用前必须已获取锁） */
static uint8_t cfg_save_power_record_nolock(const void *p)
{
  extern uint8_t kv_set_blob(const char *key, const void *buf, size_t size);
  extern const char *KV_POWER_RECORD;
  uint8_t result = kv_set_blob(KV_POWER_RECORD, p, sizeof(PowerRecord_t));
  return result;
}

/* 定期保存心跳时间戳（由其他任务定期调用） */
void power_heartbeat_periodic_save(void)
{
  static uint32_t last_save_time = 0;
  uint32_t current_time = rtc_counter_get();

  // 每30秒保存一次
  if (current_time - last_save_time >= 30)
  {
    // 使用非阻塞方式，避免与屏幕分发器冲突
    if (g_kvdb_mutex != NULL && xSemaphoreTake(g_kvdb_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
      cfg_save_power_record_nolock(&g_power_record);
      xSemaphoreGive(g_kvdb_mutex);
      // printf("[POWER] 心跳时间已定期保存\r\n");
    }
    else
    {
      // 获取锁失败，跳过本次保存
      // printf("[POWER] KVDB锁繁忙，心跳保存跳过\r\n");
    }

    last_save_time = current_time;
  }
}

/* 断电检测和记录 */
void power_failure_detection_and_record(void)
{
  uint32_t current_time = rtc_counter_get();
  uint32_t last_heartbeat = g_power_record.last_heartbeat_time;

  // 检查是否发生了断电（时间差超过60秒认为断电）
  if (current_time > last_heartbeat)
  {
    uint32_t time_diff = current_time - last_heartbeat;

    if (time_diff > 60)
    {
      // 发生了断电
      uint32_t power_off_duration = time_diff;
      uint32_t estimated_power_off_time = current_time - power_off_duration;

      // 更新统计信息
      kvdb_lock();
      g_power_record.power_count++;
      g_power_record.total_power_off_time += power_off_duration;
      g_power_record.last_power_off_duration = power_off_duration;
      cfg_save_power_record(&g_power_record);
      kvdb_unlock();

      printf("[POWER] 检测到断电事件：\r\n");
      printf("[POWER]   推算断电时间：%lu\r\n", (unsigned long)estimated_power_off_time);
      printf("[POWER]   恢复时间：%lu\r\n", (unsigned long)current_time);
      printf("[POWER]   断电持续时间：%lu秒\r\n", (unsigned long)power_off_duration);
      printf("[POWER]   总断电次数：%lu\r\n", (unsigned long)g_power_record.power_count);

      // 记录TSDB事件
      if (tsdb_is_ready())
      {
        PowerFailureEvent_t event_data;
        event_data.power_off_time = estimated_power_off_time;
        event_data.power_on_time = current_time;
        event_data.power_off_duration = power_off_duration;
        event_data.power_count = g_power_record.power_count;

        uint8_t ret = tsdb_event_append(POWER_EVT_FAILURE_DETECTED, &event_data, sizeof(event_data));
        cache_add_power(POWER_EVT_FAILURE_DETECTED, estimated_power_off_time);  // 更新缓存
        printf("[POWER] TSDB断电事件已记录（type=0x%04X, ret=%u）\r\n", POWER_EVT_FAILURE_DETECTED, ret);
      }
      else
      {
        printf("[POWER] TSDB未就绪，断电事件未记录\r\n");
      }

      // 记录恢复完成事件
      if (tsdb_is_ready())
      {
        struct
        {
          uint32_t recovery_time;
          uint32_t power_count;
        } recovery_data;

        recovery_data.recovery_time = current_time;
        recovery_data.power_count = g_power_record.power_count;

        uint8_t ret = tsdb_event_append(POWER_EVT_RECOVERY_COMPLETE, &recovery_data, sizeof(recovery_data));
        cache_add_power(POWER_EVT_RECOVERY_COMPLETE, current_time);  // 更新缓存
        printf("[POWER] TSDB恢复事件已记录（type=0x%04X, ret=%u）\r\n", POWER_EVT_RECOVERY_COMPLETE, ret);
      }
    }
    else
    {
      printf("[POWER] 正常启动，时间差=%lu秒（未检测到断电）\r\n", (unsigned long)time_diff);
    }
  }
  else
  {
    printf("[POWER] 时间异常，当前时间=%lu，上次心跳=%lu\r\n",
           (unsigned long)current_time, (unsigned long)last_heartbeat);
  }

  // 更新心跳时间戳为当前时间
  power_update_heartbeat();
}

/* 记录正常关机 */
void power_record_normal_shutdown(void)
{
  uint32_t current_time = rtc_counter_get();

  kvdb_lock();
  g_power_record.last_shutdown_time = current_time;
  cfg_save_power_record(&g_power_record);
  kvdb_unlock();

  printf("[POWER] 正常关机时间已记录：%lu\r\n", (unsigned long)current_time);
}

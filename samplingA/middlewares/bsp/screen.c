#include "freertos_app.h"
#include "screen.h"
#include "sampling_time.h"
#include "work.h"
#include "retain_judge.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "app_flashdb.h"
#include "rtc.h"
#include "screen_cache.h"
#include "record_cache.h"

/* 屏幕分发器控制变量 */
static volatile uint8_t s_screen_dispatcher_stopped = 0;
/* 屏幕分发器控制变量 */
static volatile uint8_t s_tsdb_reset_requested = 0; // TSDB重置请求标志

/* 时间设置缓存结构 */
typedef struct
{
    uint16_t year;  // 年
    uint8_t month;  // 月 (1-12)
    uint8_t day;    // 日 (1-31)
    uint8_t hour;   // 时 (0-23)
    uint8_t minute; // 分 (0-59)
    uint8_t second; // 秒 (0-59)
    uint8_t valid;  // 缓存是否有效标志
} TimeSettingsCache_t;

// static TimeSettingsCache_t s_time_cache = {0};  // 时间设置缓存

/* 函数声明 */
void _recompute_delivery_schedule(void);
uint8_t handle_card_id_update(UartMessage *msg);
void send_time_read_command(void);
void parse_and_cache_time_response(UartMessage *msg);
void send_fixed_delivery_read_command(void);
void parse_fixed_delivery_response(UartMessage *msg);

/* 辅助函数：数值限制 */
static inline uint16_t _clamp_u16(uint16_t value, uint16_t min, uint16_t max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;
    return value;
}

/* 辅助函数：采样TSDB记录（缓存写入） */
static inline void _samp_tsdb(uint8_t code, uint8_t bucket, uint16_t volume, uint16_t time)
{
    /* 封装数据结构：code, bucket, volume, time */
    struct
    {
        uint8_t code;
        uint8_t bucket;
        uint16_t volume;
        uint16_t time;
    } data = {code, bucket, volume, time};

    /* 写入TSDB缓存，事件类型使用code的高位编码 */
    uint16_t event_type = 0x00F0 | code; // 0x00F0-0x00FF范围
    uint8_t ret = tsdb_cache_append(event_type, &data, sizeof(data));
    printf("[TSDB缓存] 采样 代码=0x%02X, 桶=%u, 体积=%u, 时间=%u, 缓存=%u\r\n",
           code, bucket, volume, time, ret);
}

/* 辅助函数：送样TSDB记录（缓存写入） */
static inline void _delivery_tsdb(uint8_t code, uint8_t bucket, uint16_t volume, uint16_t time)
{
    /* 封装数据结构：code, bucket, volume, time */
    struct
    {
        uint8_t code;
        uint8_t bucket;
        uint16_t volume;
        uint16_t time;
    } data = {code, bucket, volume, time};

    /* 写入TSDB缓存，事件类型使用code的高位编码 */
    uint16_t event_type = 0x00E0 | code; // 0x00E0-0x00EF范围（送样）
    uint8_t ret = tsdb_cache_append(event_type, &data, sizeof(data));
    printf("[TSDB缓存] 送样 代码=0x%02X, 桶=%u, 体积=%u, 时间=%u, 缓存=%u\r\n",
           code, bucket, volume, time, ret);
}

/* 辅助函数：留样TSDB记录（缓存写入） */
static inline void _retain_tsdb(uint8_t code, uint8_t bucket, uint16_t volume, uint16_t bottle)
{
    /* 封装数据结构：code, bucket, volume, bottle */
    struct
    {
        uint8_t code;
        uint8_t bucket;
        uint16_t volume;
        uint16_t bottle; // 留样瓶号
    } data = {code, bucket, volume, bottle};

    /* 写入TSDB缓存，事件类型使用code的高位编码 */
    uint16_t event_type = 0x00D0 | code; // 0x00D0-0x00DF范围（留样）
    uint8_t ret = tsdb_cache_append(event_type, &data, sizeof(data));
    printf("[TSDB缓存] 留样 代码=0x%02X, 桶=%u, 体积=%u, 瓶号=%u, 缓存=%u\r\n",
           code, bucket, volume, bottle, ret);
}

extern int scheduler_is_emergency_active(void);

/* 函数前置声明 */
void sampling_emergency_stop(void);

/* 辅助函数：更新硬件RTC时钟
 * 先读取当前RTC时间，然后批量更新所有时间字段
 */
static void update_hardware_rtc(void)
{
    // 1. 先读取当前硬件RTC的实时时间
    rtc_time_get(); // 更新全局calendar变量

    // 2. 用g_SystemSettingConfig的值覆盖所有时间字段
    calendar_type rtc_cal = calendar; // 复制当前时间
    rtc_cal.year = g_SystemSettingConfig.Year;
    rtc_cal.month = g_SystemSettingConfig.Month;
    rtc_cal.date = g_SystemSettingConfig.Day;
    rtc_cal.hour = g_SystemSettingConfig.Hour;
    rtc_cal.min = g_SystemSettingConfig.Minute;
    rtc_cal.sec = g_SystemSettingConfig.Second;

    // 3. 写入硬件RTC
    uint8_t ret = rtc_time_set(&rtc_cal);
    if (ret == 0)
    {
        printf("[RTC] 硬件RTC已更新: %04d-%02d-%02d %02d:%02d:%02d\r\n",
               rtc_cal.year, rtc_cal.month, rtc_cal.date,
               rtc_cal.hour, rtc_cal.min, rtc_cal.sec);
    }
    else
    {
        printf("[RTC] 错误：更新硬件RTC失败（年份必须在1970-2099之间）\r\n");
    }
}

/*_clamp_u16: 数值限制函数，确保一个16位无符号数值在指定范围内
_samp_tsdb: 采样时序数据库记录函数，用于记录采样相关事件
_delivery_tsdb: 送样时序数据库记录函数，用于记录送样相关事件
_retain_tsdb: 留样时序数据库记录函数，用于记录留样相关事件
scheduler_is_emergency_active: 检查系统是否处于紧急状态的函数*/

static void _recompute_delivery_end(void);

/* 手动操作紧急停止标志位 */
volatile uint8_t g_manual_operation_abort_flag = 0;

/* 非阻塞瓶位移动状态检查标志 */
static uint8_t s_bottle_move_pending = 0;       // 是否有待处理的非阻塞移动
static uint8_t s_bottle_move_target_action = 0; // 移动成功后要保存的目标瓶号

/* 延迟KVDB保存标志（避免阻塞） */
static uint8_t s_kvdb_save_pending = 0;       // 是否有待保存的KVDB数据
static uint8_t s_kvdb_save_bottle_number = 0; // 待保存的瓶号

/**
 * @brief 检查非阻塞瓶位移动状态并更新内存变量
 * @note 在 screen_message_dispatcher 中周期性调用
 * @note 不直接调用KVDB保存，而是设置延迟保存标志
 */
static void bottle_move_check_and_update(void)
{
    if (!s_bottle_move_pending)
    {
        return;
    }

    uint8_t result = bottle_move_to_check();

    if (result == 0)
    {
        // 仍在进行中，继续等待
        return;
    }

    // 移动完成（成功或失败）
    s_bottle_move_pending = 0;

    if (result == 1)
    {
        // 移动成功，只更新内存变量（不直接保存KVDB，避免阻塞）
        extern RetainSampleModeConfig g_RetainSampleConfig;
        extern RetainBottleState g_RetainBottleState;
        extern uint8_t g_current_bottle_number;

        uint8_t action = s_bottle_move_target_action;

        g_RetainSampleConfig.bottleNumber = action;
        g_current_bottle_number = action;
        g_RetainBottleState.currentBottle = action;

        // 如果之前因为24号瓶留样完成而禁用了留样，现在需要重新启用
        if (g_RetainSampleConfig.EnableSample == 0 && action != 24)
        {
            g_RetainSampleConfig.EnableSample = 1;
            printf("[系统] 离开24号瓶，已重新启用留样功能\r\n");
        }

        // 设置延迟保存标志，由 bottle_move_kvdb_try_save 处理
        s_kvdb_save_pending = 1;
        s_kvdb_save_bottle_number = action;
        printf("[留样瓶] 移动成功到%d号瓶，等待KVDB保存\r\n", action);

        // 更新屏幕显示变量
        update_bottle_display();
    }
    else
    {
        // 移动失败
        printf("[留样瓶] 手动转动到%d号瓶失败\r\n", s_bottle_move_target_action);
    }
}

/**
 * @brief 尝试保存KVDB数据（带超时，非阻塞）
 * @note 在 screen_message_dispatcher 末尾调用
 */
static void bottle_move_kvdb_try_save(void)
{
    static uint8_t s_retry_count = 0; // 重试计数

    if (!s_kvdb_save_pending)
    {
        return;
    }

    extern RetainSampleModeConfig g_RetainSampleConfig;
    extern RetainBottleState g_RetainBottleState;

    // 使用100ms超时尝试保存
    uint8_t ret1 = cfg_save_retain_timeout(&g_RetainSampleConfig, 100);
    uint8_t ret2 = cfg_save_retain_state_timeout(&g_RetainBottleState, 100);

    if (ret1 == 0 && ret2 == 0)
    {
        // 保存成功
        s_kvdb_save_pending = 0;
        s_retry_count = 0;
        printf("[KVDB] 手动转动到%d号瓶，已保存\r\n", s_kvdb_save_bottle_number);
    }
    else if (ret1 == 1 || ret2 == 1)
    {
        // 获取锁超时，下次再试
        s_retry_count++;
        if (s_retry_count % 10 == 1) // 每10次打印一次
        {
            printf("[KVDB] 等待锁超时，重试第%d次 (ret1=%d, ret2=%d)\r\n", s_retry_count, ret1, ret2);
        }
        // 如果重试超过50次（约5秒），标记缓存脏数据
        if (s_retry_count >= 50)
        {
            printf("[KVDB缓存] 重试超限，标记缓存...\r\n");
            kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
            kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);
            s_kvdb_save_pending = 0;
            s_retry_count = 0;
            printf("[KVDB缓存] 手动转动到%d号瓶，已标记\r\n", s_kvdb_save_bottle_number);
        }
    }
    else
    {
        // 保存失败
        s_kvdb_save_pending = 0;
        s_retry_count = 0;
        printf("[KVDB] 保存失败，ret1=%d, ret2=%d\r\n", ret1, ret2);
    }
}

//==============================================================================
// 页面状态管理
//==============================================================================

/* 页面状态枚举 */
typedef enum
{
    SCREEN_PAGE_HOME = 0,           // 主页
    SCREEN_PAGE_SETTINGS,           // 设置页
    SCREEN_PAGE_CHANNEL_SETTINGS,   // 通道设置页（5秒发送ADC数据）
    SCREEN_PAGE_ADVANCED_SETTINGS,  // 高级设置页（5秒发送流量数据）
    SCREEN_PAGE_OTHER               // 其他页
} ScreenPageState;

/* 全局页面状态（初始为主页） */
static ScreenPageState g_screen_page_state = SCREEN_PAGE_HOME;

//==============================================================================
// 页面状态管理函数
//==============================================================================

/**
 * @brief 查询当前是否在主页
 * @return 1=主页, 0=其他页
 */
uint8_t screen_is_on_home_page(void)
{
    return (g_screen_page_state == SCREEN_PAGE_HOME) ? 1 : 0;
}

/**
 * @brief 查询当前是否在通道设置页
 * @return 1=通道设置页, 0=其他页
 */
uint8_t screen_is_on_channel_settings_page(void)
{
    return (g_screen_page_state == SCREEN_PAGE_CHANNEL_SETTINGS) ? 1 : 0;
}

/**
 * @brief 查询当前是否在高级设置页
 * @return 1=高级设置页, 0=其他页
 */
uint8_t screen_is_on_advanced_settings_page(void)
{
    return (g_screen_page_state == SCREEN_PAGE_ADVANCED_SETTINGS) ? 1 : 0;
}

/**
 * @brief 设置页面状态
 * @param state 页面状态
 */
static void screen_set_page_state(ScreenPageState state)
{
    g_screen_page_state = state;
}

// 编写写入采样设置页的函数
void write_sampling_settings_page(void)
{
    uint8_t buf[32] = {0x5A, 0xA5, 0x1D, 0x82, 0x50, 0x10, 0x00};
    buf[7] = g_SampleConfig.SamplingMode;
    buf[8] = g_SampleConfig.SampleInterval >> 8;
    buf[9] = g_SampleConfig.SampleInterval & 0xFF;
    buf[10] = g_SampleConfig.SampleVolume >> 8;
    buf[11] = g_SampleConfig.SampleVolume & 0xFF;
    buf[12] = g_SampleConfig.BlowbackTime >> 8;
    buf[13] = g_SampleConfig.BlowbackTime & 0xFF;
    buf[14] = g_SampleConfig.SamplingImproveTime >> 8;
    buf[15] = g_SampleConfig.SamplingImproveTime & 0xFF;
    buf[16] = g_SampleConfig.TubeHoldTime >> 8;
    buf[17] = g_SampleConfig.TubeHoldTime & 0xFF;
    buf[18] = g_SampleConfig.CycleTime >> 8;
    buf[19] = g_SampleConfig.CycleTime & 0xFF;
    buf[20] = g_SampleConfig.BucketDrainTime >> 8;
    buf[21] = g_SampleConfig.BucketDrainTime & 0xFF;
    buf[22] = g_SampleConfig.AnalysisTime >> 8;
    buf[23] = g_SampleConfig.AnalysisTime & 0xFF;
    buf[24] = g_SampleConfig.DischargeVolume >> 8;
    buf[25] = g_SampleConfig.DischargeVolume & 0xFF;
    buf[26] = g_SampleConfig.FlowRatio >> 8;
    buf[27] = g_SampleConfig.FlowRatio & 0xFF;
    buf[28] = g_SampleConfig.FlowStart >> 8;
    buf[29] = g_SampleConfig.FlowStart & 0xFF;
    buf[30] = g_SampleConfig.FlowStop >> 8;
    buf[31] = g_SampleConfig.FlowStop & 0xFF;
    screen_send_notify(USART_SCREEN, buf, 32, 3);
}

void write_delivery_settings_page(void)
{
    uint8_t buf[20] = {0x5A, 0xA5, 0x11, 0x82, 0x50, 0x40, 0x00};
    buf[7] = g_DeliveryConfig.StartHour;
    buf[8] = 0;
    buf[9] = g_DeliveryConfig.StartMin;
    buf[10] = g_DeliveryConfig.Duration >> 8;
    buf[11] = g_DeliveryConfig.Duration & 0xFF;
    buf[12] = g_DeliveryConfig.Interval >> 8;
    buf[13] = g_DeliveryConfig.Interval & 0xFF;
    buf[14] = 0;
    buf[15] = g_DeliveryConfig.EndHour;
    buf[16] = 0;
    buf[17] = g_DeliveryConfig.EndMin;
    buf[18] = 0;
    buf[19] = g_DeliveryConfig.Enable;
    screen_send_notify(USART_SCREEN, buf, 20, 3);
}

void write_retain_settings_page(void)
{
    uint8_t buf[26] = {0x5A, 0xA5, 0x19, 0x82, 0x50, 0x60, 0x00}; // 数组大小26（0-25索引）
    buf[7] = g_RetainSampleConfig.Mode;
    buf[8] = g_RetainSampleConfig.SampleVolume >> 8; // 单次留样量
    buf[9] = g_RetainSampleConfig.SampleVolume & 0xFF;
    buf[10] = 0; // 平行样高字节
    buf[11] = g_RetainSampleConfig.ParallelCount;
    buf[12] = 0; // 混样次数高字节
    buf[13] = g_RetainSampleConfig.MixCount;
    buf[14] = g_RetainSampleConfig.BlowbackTime >> 8; // 留样反吹
    buf[15] = g_RetainSampleConfig.BlowbackTime & 0xFF;
    buf[16] = 0;                                        // 是否留样高字节
    buf[17] = g_RetainSampleConfig.EnableSample;        // 是否留样
    buf[18] = 0;                                        // 是否加酸高字节
    buf[19] = g_RetainSampleConfig.EnableAcid;          // 是否加酸（修复：原来是buf[17]）
    buf[20] = 0;                                        // 是否排空高字节
    buf[21] = g_RetainSampleConfig.EnableVacuum;        // 是否排空（修复：原来是buf[19]）
    buf[22] = g_RetainSampleConfig.TubeHoldTime >> 8;   // 留样管存（修复：原来是buf[20]）
    buf[23] = g_RetainSampleConfig.TubeHoldTime & 0xFF; // （修复：原来是buf[21]）
    buf[24] = g_RetainSampleConfig.BackdrawTime >> 8;   // 留样回抽高字节（修复：原来是buf[21]）
    buf[25] = g_RetainSampleConfig.BackdrawTime & 0xFF; // 留样回抽低字节（修复：原来是buf[22]）

    // 调试打印
    printf("[屏幕] write_retain_settings: TubeHoldTime=%u (buf[22-23]=0x%02X%02X)\r\n",
           g_RetainSampleConfig.TubeHoldTime, buf[22], buf[23]);

    screen_send_notify(USART_SCREEN, buf, 26, 3); // 发送26字节
}

// 通道一固定COD uint16_t，通道六固定流量uint16_t，一位小数，其他uint16_t 两位小数
void write_retain_channel_settings_page(void)
{
    uint8_t buf[52] = {0x5A, 0xA5, 0x31, 0x82, 0x50, 0x6A};
    buf[6] = 0;
    buf[7] = g_RetainSampleConfig.channelLimits[0].FactorType;                  // 因子类型 固定COD
    buf[8] = (uint16_t)(g_RetainSampleConfig.channelLimits[0].LowerLimit) >> 8; // 超标下限
    buf[9] = (uint16_t)(g_RetainSampleConfig.channelLimits[0].LowerLimit) & 0xFF;
    buf[10] = (uint16_t)(g_RetainSampleConfig.channelLimits[0].UpperLimit) >> 8; // 超标上限
    buf[11] = (uint16_t)(g_RetainSampleConfig.channelLimits[0].UpperLimit) & 0xFF;
    buf[12] = 0;                                            // 是否应用
    buf[13] = g_RetainSampleConfig.channelLimits[0].Enable; // 是否应用
    // g_RetainSampleConfig.channelLimits[1].LowerLimit，g_RetainSampleConfig.channelLimits[1].UpperLimit浮点数
    // 两个浮点数*100然后取整
    buf[14] = 0;                                                                          // 因子类型
    buf[15] = g_RetainSampleConfig.channelLimits[1].FactorType;                           // 因子类型
    buf[16] = (uint16_t)(g_RetainSampleConfig.channelLimits[1].LowerLimit * 10.0f) >> 8; // 超标下限
    buf[17] = (uint16_t)(g_RetainSampleConfig.channelLimits[1].LowerLimit * 10.0f) & 0xFF;
    buf[18] = (uint16_t)(g_RetainSampleConfig.channelLimits[1].UpperLimit * 10.0f) >> 8; // 超标上限
    buf[19] = (uint16_t)(g_RetainSampleConfig.channelLimits[1].UpperLimit * 10.0f) & 0xFF;
    buf[20] = 0;                                            // 是否应用
    buf[21] = g_RetainSampleConfig.channelLimits[1].Enable; // 是否应用

    buf[22] = 0;                                                                          // 因子类型
    buf[23] = g_RetainSampleConfig.channelLimits[2].FactorType;                           // 因子类型
    buf[24] = (uint16_t)(g_RetainSampleConfig.channelLimits[2].LowerLimit * 10.0f) >> 8; // 超标下限
    buf[25] = (uint16_t)(g_RetainSampleConfig.channelLimits[2].LowerLimit * 10.0f) & 0xFF;
    buf[26] = (uint16_t)(g_RetainSampleConfig.channelLimits[2].UpperLimit * 10.0f) >> 8; // 超标上限
    buf[27] = (uint16_t)(g_RetainSampleConfig.channelLimits[2].UpperLimit * 10.0f) & 0xFF;
    buf[28] = 0;                                            // 是否应用
    buf[29] = g_RetainSampleConfig.channelLimits[2].Enable; // 是否应用

    buf[30] = 0;                                                                          // 因子类型
    buf[31] = g_RetainSampleConfig.channelLimits[3].FactorType;                           // 因子类型
    buf[32] = (uint16_t)(g_RetainSampleConfig.channelLimits[3].LowerLimit * 10.0f) >> 8; // 超标下限
    buf[33] = (uint16_t)(g_RetainSampleConfig.channelLimits[3].LowerLimit * 10.0f) & 0xFF;
    buf[34] = (uint16_t)(g_RetainSampleConfig.channelLimits[3].UpperLimit * 10.0f) >> 8; // 超标上限
    buf[35] = (uint16_t)(g_RetainSampleConfig.channelLimits[3].UpperLimit * 10.0f) & 0xFF;
    buf[36] = 0;                                            // 是否应用
    buf[37] = g_RetainSampleConfig.channelLimits[3].Enable; // 是否应用

    buf[38] = 0;                                                                          // 因子类型
    buf[39] = g_RetainSampleConfig.channelLimits[4].FactorType;                           // 因子类型
    buf[40] = (uint16_t)(g_RetainSampleConfig.channelLimits[4].LowerLimit * 10.0f) >> 8; // 超标下限
    buf[41] = (uint16_t)(g_RetainSampleConfig.channelLimits[4].LowerLimit * 10.0f) & 0xFF;
    buf[42] = (uint16_t)(g_RetainSampleConfig.channelLimits[4].UpperLimit * 10.0f) >> 8; // 超标上限
    buf[43] = (uint16_t)(g_RetainSampleConfig.channelLimits[4].UpperLimit * 10.0f) & 0xFF;
    buf[44] = 0;                                            // 是否应用
    buf[45] = g_RetainSampleConfig.channelLimits[4].Enable; // 是否应用

    buf[46] = (uint16_t)(g_RetainSampleConfig.channelLimits[5].LowerLimit * 10.0f) >> 8; // 超标下限
    buf[47] = (uint16_t)(g_RetainSampleConfig.channelLimits[5].LowerLimit * 10.0f) & 0xFF;
    buf[48] = (uint16_t)(g_RetainSampleConfig.channelLimits[5].UpperLimit * 10.0f) >> 8; // 超标上限
    buf[49] = (uint16_t)(g_RetainSampleConfig.channelLimits[5].UpperLimit * 10.0f) & 0xFF;
    buf[50] = 0;                                            // 是否应用
    buf[51] = g_RetainSampleConfig.channelLimits[5].Enable; // 是否应用
    screen_send_notify(USART_SCREEN, buf, 52, 3);
}

void write_retain_channel_data_page(void)
{   // 模拟量数值  只写 收到（ 5a    a5    6    83    0    0    1    81    32）AD写一次
    uint8_t buf[20] = {0x5A, 0xA5, 0x11, 0x82, 0x50, 0x82};
    buf[6] = (uint16_t)(g_RetainSampleConfig.channelData[0]) >> 8;             // COD值高位
    buf[7] = (uint16_t)(g_RetainSampleConfig.channelData[0]) & 0xFF;           // cod值低位
    buf[8] = (uint16_t)(g_RetainSampleConfig.channelData[1] * 100.0f) >> 8;    // 高位
    buf[9] = (uint16_t)(g_RetainSampleConfig.channelData[1] * 100.0f) & 0xFF;  // 低位
    buf[10] = (uint16_t)(g_RetainSampleConfig.channelData[2] * 100.0f) >> 8;   // 高位
    buf[11] = (uint16_t)(g_RetainSampleConfig.channelData[2] * 100.0f) & 0xFF; // 低位
    buf[12] = (uint16_t)(g_RetainSampleConfig.channelData[3] * 100.0f) >> 8;   // 高位
    buf[13] = (uint16_t)(g_RetainSampleConfig.channelData[3] * 100.0f) & 0xFF; // 低位
    buf[14] = (uint16_t)(g_RetainSampleConfig.channelData[4] * 100.0f) >> 8;   // 高位
    buf[15] = (uint16_t)(g_RetainSampleConfig.channelData[4] * 100.0f) & 0xFF; // 低位
    buf[16] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) >> 8;    // 通道6(流量)高位
    buf[17] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) & 0xFF;  // 通道6(流量)低位
    buf[18] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) >> 8;    // 流量高位（索引7=PA3）
    buf[19] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) & 0xFF;  // 流量低位（索引7=PA3）
    screen_send_notify(USART_SCREEN, buf, 20, 3);
}

// 六个通道通过adc转换的电流值 两位小数  .channelCurrent
void write_retain_channel_current_page(void)
{   // 模拟量数值  只写 收到（ 5a    a5    6    83    0    0    1    81    33）AD写一次
    uint8_t buf[18] = {0x5A, 0xA5, 0x0F, 0x82, 0x50, 0xA8};
    buf[6] = (uint16_t)(g_RetainSampleConfig.channelCurrent[0] * 100.0f) >> 8;    // COD值高位
    buf[7] = (uint16_t)(g_RetainSampleConfig.channelCurrent[0] * 100.0f) & 0xFF;  // cod值低位
    buf[8] = (uint16_t)(g_RetainSampleConfig.channelCurrent[1] * 100.0f) >> 8;    // 高位
    buf[9] = (uint16_t)(g_RetainSampleConfig.channelCurrent[1] * 100.0f) & 0xFF;  // 低位
    buf[10] = (uint16_t)(g_RetainSampleConfig.channelCurrent[2] * 100.0f) >> 8;   // 高位
    buf[11] = (uint16_t)(g_RetainSampleConfig.channelCurrent[2] * 100.0f) & 0xFF; // 低位
    buf[12] = (uint16_t)(g_RetainSampleConfig.channelCurrent[3] * 100.0f) >> 8;   // 高位
    buf[13] = (uint16_t)(g_RetainSampleConfig.channelCurrent[3] * 100.0f) & 0xFF; // 低位
    buf[14] = (uint16_t)(g_RetainSampleConfig.channelCurrent[4] * 100.0f) >> 8;   // 高位
    buf[15] = (uint16_t)(g_RetainSampleConfig.channelCurrent[4] * 100.0f) & 0xFF; // 低位
    buf[16] = (uint16_t)(g_RetainSampleConfig.channelCurrent[7] * 100.0f) >> 8;    // 通道6(流量)高位
    buf[17] = (uint16_t)(g_RetainSampleConfig.channelCurrent[7] * 100.0f) & 0xFF;  // 通道6(流量)低位
    screen_send_notify(USART_SCREEN, buf, 18, 3);
}

static uint16_t screen_cal_ad_to_display(uint16_t cal_ad)
{
    if (cal_ad == 0)
    {
        return 0;
    }
    if (cal_ad <= 30u)
    {
        return (uint16_t)(cal_ad * 10u);
    }
    return cal_ad;
}

// 写入  5A A5 05 82 50 8A 03 33   变量地址508A 最后两个字节是数值  通道一输入AD  对应g_RetainSampleModeConfig.channelCals[0].InputAD
// 收到  5A A5 06 83 50 8B 01 00 03   变量地址508B 最后两个字节是数值  通道一0点AD   对应g_RetainSampleModeConfig.channelCals[0].ZeroAD
// 收到  5A A5 06 83 50 8C 01 00 03   变量地址508C 最后两个字节是数值  通道一校准AD  对应g_RetainSampleModeConfig.channelCals[0].CalAD
// 收到  5A A5 06 83 50 8D 01 00 03   变量地址508D 最后两个字节是数值  通道一校准值  对应g_RetainSampleModeConfig.channelCals[0].CalValue
// 校准值是对应20mA的真实值，输入AD显示AD值，0点AD填742，校准AD填3711
// AD=185.55*g_RetainSampleConfig.Current
// g_RetainSampleConfig.channelData怎样计算的
// g_RetainSampleConfig.Current=ADC/185.55
void write_retain_channel_channelCals_page(void)
{
    uint8_t buf[54] = {0x5A, 0xA5, 0x33, 0x82, 0x50, 0x8A};
    uint16_t cal_ad;
    buf[6] = (uint16_t)(g_RetainSampleConfig.channelCurrent[0] * 100.0f) >> 8;
    buf[7] = (uint16_t)(g_RetainSampleConfig.channelCurrent[0] * 100.0f) & 0xFF;
    buf[8] = (uint16_t)(g_RetainSampleConfig.channelData[0]) >> 8;
    buf[9] = (uint16_t)(g_RetainSampleConfig.channelData[0]) & 0xFF;
    cal_ad = screen_cal_ad_to_display(g_RetainSampleConfig.channelCals[0].CalAD);
    buf[10] = cal_ad >> 8;
    buf[11] = cal_ad & 0xFF;
    buf[12] = (uint16_t)(g_RetainSampleConfig.channelCals[0].CalValue) >> 8;
    buf[13] = (uint16_t)(g_RetainSampleConfig.channelCals[0].CalValue) & 0xFF;

    buf[14] = (uint16_t)(g_RetainSampleConfig.channelCurrent[1] * 100.0f) >> 8;
    buf[15] = (uint16_t)(g_RetainSampleConfig.channelCurrent[1] * 100.0f) & 0xFF;
    buf[16] = (uint16_t)(g_RetainSampleConfig.channelData[1] * 10.0f) >> 8;
    buf[17] = (uint16_t)(g_RetainSampleConfig.channelData[1] * 10.0f) & 0xFF;
    cal_ad = screen_cal_ad_to_display(g_RetainSampleConfig.channelCals[1].CalAD);
    buf[18] = cal_ad >> 8;
    buf[19] = cal_ad & 0xFF;
    buf[20] = (uint16_t)(g_RetainSampleConfig.channelCals[1].CalValue * 10.0f) >> 8;
    buf[21] = (uint16_t)(g_RetainSampleConfig.channelCals[1].CalValue * 10.0f) & 0xFF;

    buf[22] = (uint16_t)(g_RetainSampleConfig.channelCurrent[2] * 100.0f) >> 8;
    buf[23] = (uint16_t)(g_RetainSampleConfig.channelCurrent[2] * 100.0f) & 0xFF;
    buf[24] = (uint16_t)(g_RetainSampleConfig.channelData[2] * 10.0f) >> 8;
    buf[25] = (uint16_t)(g_RetainSampleConfig.channelData[2] * 10.0f) & 0xFF;
    cal_ad = screen_cal_ad_to_display(g_RetainSampleConfig.channelCals[2].CalAD);
    buf[26] = cal_ad >> 8;
    buf[27] = cal_ad & 0xFF;
    buf[28] = (uint16_t)(g_RetainSampleConfig.channelCals[2].CalValue * 10.0f) >> 8;
    buf[29] = (uint16_t)(g_RetainSampleConfig.channelCals[2].CalValue * 10.0f) & 0xFF;

    buf[30] = (uint16_t)(g_RetainSampleConfig.channelCurrent[3] * 100.0f) >> 8;
    buf[31] = (uint16_t)(g_RetainSampleConfig.channelCurrent[3] * 100.0f) & 0xFF;
    buf[32] = (uint16_t)(g_RetainSampleConfig.channelData[3] * 10.0f) >> 8;
    buf[33] = (uint16_t)(g_RetainSampleConfig.channelData[3] * 10.0f) & 0xFF;
    cal_ad = screen_cal_ad_to_display(g_RetainSampleConfig.channelCals[3].CalAD);
    buf[34] = cal_ad >> 8;
    buf[35] = cal_ad & 0xFF;
    buf[36] = (uint16_t)(g_RetainSampleConfig.channelCals[3].CalValue * 10.0f) >> 8;
    buf[37] = (uint16_t)(g_RetainSampleConfig.channelCals[3].CalValue * 10.0f) & 0xFF;

    buf[38] = (uint16_t)(g_RetainSampleConfig.channelCurrent[4] * 100.0f) >> 8;
    buf[39] = (uint16_t)(g_RetainSampleConfig.channelCurrent[4] * 100.0f) & 0xFF;
    buf[40] = (uint16_t)(g_RetainSampleConfig.channelData[4] * 10.0f) >> 8;
    buf[41] = (uint16_t)(g_RetainSampleConfig.channelData[4] * 10.0f) & 0xFF;
    cal_ad = screen_cal_ad_to_display(g_RetainSampleConfig.channelCals[4].CalAD);
    buf[42] = cal_ad >> 8;
    buf[43] = cal_ad & 0xFF;
    buf[44] = (uint16_t)(g_RetainSampleConfig.channelCals[4].CalValue * 10.0f) >> 8;
    buf[45] = (uint16_t)(g_RetainSampleConfig.channelCals[4].CalValue * 10.0f) & 0xFF;

    buf[46] = (uint16_t)(g_RetainSampleConfig.channelCurrent[7] * 100.0f) >> 8;
    buf[47] = (uint16_t)(g_RetainSampleConfig.channelCurrent[7] * 100.0f) & 0xFF;
    buf[48] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) >> 8;
    buf[49] = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f) & 0xFF;
    buf[50] = g_CommSettingConfig.FlowADLower >> 8;
    buf[51] = g_CommSettingConfig.FlowADLower & 0xFF;
    buf[52] = (uint16_t)(g_CommSettingConfig.FlowMeterBase * 10.0f) >> 8;
    buf[53] = (uint16_t)(g_CommSettingConfig.FlowMeterBase * 10.0f) & 0xFF;
    screen_send_notify(USART_SCREEN, buf, 54, 3);
}

// 收到  5A A5 06 83 50 B0 01 00 03   变量地址50B0 最后两个字节是数值  通讯协议  对应g_CommSettingConfig.Protocol
// 收到  5A A5 06 83 50 B1 01 00 03   变量地址50B1 最后两个字节是数值  设备地址  对应g_CommSettingConfig.DeviceAddr
// 收到  5A A5 06 83 50 B2 01 00 00   变量地址50B2 最后两个字节是数值  协议选择  对应g_CommSettingConfig.AutoCalibration
void write_retain_channel_comm_page(void)
{
    uint8_t buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x50, 0xB0};
    buf[6] = 0;
    buf[7] = g_CommSettingConfig.Protocol;
    buf[8] = 0;
    buf[9] = g_CommSettingConfig.DeviceAddr;
    screen_send_notify(USART_SCREEN, buf, 10, 3);

    uint8_t buf2[8] = {0x5A, 0xA5, 0x05, 0x82, 0x50, 0xB2};
    buf2[6] = 0;
    buf2[7] = g_CommSettingConfig.AutoCalibration;
    screen_send_notify(USART_SCREEN, buf2, 8, 3);
}

// 收到  5A A5 06 83 50 BF 01 00 03   变量地址50BF 最后两个字节是数值  是否自动运行 对应g_SystemSettingConfig.AutoRunMode
// 收到  5A A5 06 83 50 D4 01 00 03   变量地址50D4 最后两个字节是数值  是否水站模式 对应g_SystemSettingConfig.WaterStationMode
void write_retain_channel_AutoRunMode_page(void)
{
    uint8_t buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x50, 0xBF};
    buf[6] = 0;
    buf[7] = g_SystemSettingConfig.AutoRunMode;
    screen_send_notify(USART_SCREEN, buf, 8, 3);
}

void write_retain_channel_WaterStationMode_page(void)
{
    uint8_t buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x50, 0xD4};
    buf[6] = 0;
    buf[7] = g_SystemSettingConfig.WaterStationMode;
    screen_send_notify(USART_SCREEN, buf, 8, 3);
}

/**
 * @brief 写入软件版本信息到屏幕
 * @note 写入三个变量地址:
 *       - 50E0: 软件序列号
 *       - 50E8: 核心板版本
 *       - 54F0: 液晶屏版本
 */
void write_software_version_page(void)
{
    uint8_t buf[30];
    uint8_t len;

    // 写入软件序列号到地址50E0
    len = strlen(g_SystemSettingConfig.SoftwareSerial);
    if (len > 0 && len <= 24) {
        buf[0] = 0x5A;
        buf[1] = 0xA5;
        buf[2] = 3 + len;  // 长度 = 82 + 地址2字节 + 数据
        buf[3] = 0x82;
        buf[4] = 0x50;
        buf[5] = 0xE0;
        memcpy(&buf[6], g_SystemSettingConfig.SoftwareSerial, len);
        screen_send_notify(USART_SCREEN, buf, 6 + len, 3);
        vTaskDelay(5);
    }

    // 写入核心板版本到地址50E8
    len = strlen(g_SystemSettingConfig.SoftwareCoreVer);
    if (len > 0 && len <= 16) {
        buf[0] = 0x5A;
        buf[1] = 0xA5;
        buf[2] = 3 + len;
        buf[3] = 0x82;
        buf[4] = 0x50;
        buf[5] = 0xE8;
        memcpy(&buf[6], g_SystemSettingConfig.SoftwareCoreVer, len);
        screen_send_notify(USART_SCREEN, buf, 6 + len, 3);
        vTaskDelay(5);
    }

    // 写入液晶屏版本到地址54F0
    len = strlen(g_SystemSettingConfig.SoftwareLcdVer);
    if (len > 0 && len <= 16) {
        buf[0] = 0x5A;
        buf[1] = 0xA5;
        buf[2] = 3 + len;
        buf[3] = 0x82;
        buf[4] = 0x54;
        buf[5] = 0xF0;
        memcpy(&buf[6], g_SystemSettingConfig.SoftwareLcdVer, len);
        screen_send_notify(USART_SCREEN, buf, 6 + len, 3);
    }
}

void write_bottle_states_page(void)
{
    uint8_t buf[54] = {0x5A, 0xA5, 0x33, 0x82, 0x51, 0x20, 0x00, 0x01};

    // 填充24个瓶的状态：buf[7], buf[9], buf[11], ..., buf[53]
    for (uint8_t i = 1; i <= 24; i++)
    {
        uint8_t status = retain_get_bottle_status(i);
        buf[7 + (i - 1) * 2] = status; // 0=空瓶, 2=满瓶
    }

    screen_send_notify(USART_SCREEN, buf, 54, 3);
}

void write_retain_channel_calibration_page(void)
{
    uint8_t buf[42] = {0x5A, 0xA5, 0x27, 0x82, 0x51, 0x10};
    buf[6] = g_CalibrationParams.samplingCalib.time1 >> 8;
    buf[7] = g_CalibrationParams.samplingCalib.time1 & 0xFF;
    buf[8] = g_CalibrationParams.samplingCalib.realValue1 >> 8;
    buf[9] = g_CalibrationParams.samplingCalib.realValue1 & 0xFF;
    buf[10] = g_CalibrationParams.samplingCalib.time2 >> 8;
    buf[11] = g_CalibrationParams.samplingCalib.time2 & 0xFF;
    buf[12] = g_CalibrationParams.samplingCalib.realValue2 >> 8;
    buf[13] = g_CalibrationParams.samplingCalib.realValue2 & 0xFF;
    buf[14] = g_CalibrationParams.samplingCalib.time3 >> 8;
    buf[15] = g_CalibrationParams.samplingCalib.time3 & 0xFF;
    buf[16] = g_CalibrationParams.samplingCalib.realValue3 >> 8;
    buf[17] = g_CalibrationParams.samplingCalib.realValue3 & 0xFF;
    buf[18] = g_CalibrationParams.retainSampleCalib.time1 >> 8;
    buf[19] = g_CalibrationParams.retainSampleCalib.time1 & 0xFF;
    buf[20] = g_CalibrationParams.retainSampleCalib.realValue1 >> 8;
    buf[21] = g_CalibrationParams.retainSampleCalib.realValue1 & 0xFF;
    buf[22] = g_CalibrationParams.retainSampleCalib.time2 >> 8;
    buf[23] = g_CalibrationParams.retainSampleCalib.time2 & 0xFF;
    buf[24] = g_CalibrationParams.retainSampleCalib.realValue2 >> 8;
    buf[25] = g_CalibrationParams.retainSampleCalib.realValue2 & 0xFF;
    buf[26] = g_CalibrationParams.retainSampleCalib.time3 >> 8;
    buf[27] = g_CalibrationParams.retainSampleCalib.time3 & 0xFF;
    buf[28] = g_CalibrationParams.retainSampleCalib.realValue3 >> 8;
    buf[29] = g_CalibrationParams.retainSampleCalib.realValue3 & 0xFF;
    buf[30] = g_CalibrationParams.acidAdditionCalib.time1 >> 8;
    buf[31] = g_CalibrationParams.acidAdditionCalib.time1 & 0xFF;
    buf[32] = g_CalibrationParams.acidAdditionCalib.realValue1 >> 8;
    buf[33] = g_CalibrationParams.acidAdditionCalib.realValue1 & 0xFF;
    buf[34] = g_CalibrationParams.acidAdditionCalib.time2 >> 8;
    buf[35] = g_CalibrationParams.acidAdditionCalib.time2 & 0xFF;
    buf[36] = g_CalibrationParams.acidAdditionCalib.realValue2 >> 8;
    buf[37] = g_CalibrationParams.acidAdditionCalib.realValue2 & 0xFF;
    buf[38] = g_CalibrationParams.acidAdditionCalib.time3 >> 8;
    buf[39] = g_CalibrationParams.acidAdditionCalib.time3 & 0xFF;
    buf[40] = g_CalibrationParams.acidAdditionCalib.realValue3 >> 8;
    buf[41] = g_CalibrationParams.acidAdditionCalib.realValue3 & 0xFF;
    screen_send_notify(USART_SCREEN, buf, 42, 3);
}

// 发送给屏幕数据后等待接收ACK ACK=
uint8_t screen_send_notify(usart_type *usart_x, const uint8_t *buf, uint8_t len, uint8_t retryNum)
{
    /* 互斥保证单飞 - 带超时500ms，避免无限阻塞 */
    if (xSemaphoreTake(g_screen_mtx, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        return 0;
    }
    g_screen_waiter = xTaskGetCurrentTaskHandle();
    uint8_t ok = 0;

    uint8_t totalRetry = retryNum;
    while (retryNum > 0)
    {
        /* 清旧通知，避免误触发 */
        ulTaskNotifyTake(pdTRUE, 0);
        vSendData(usart_x, buf, len);
        /* 等待ACK通知，超时则重试 */
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) > 0)
        {
            ok = 1;
            break;
        }
        retryNum--;
    }

    g_screen_waiter = NULL;
    xSemaphoreGive(g_screen_mtx);

    if (!ok)
    {
        printf("[屏幕] 通信失败，重试%d次无响应\r\n", totalRetry);
    }
    return ok;
}

/* 从ISR唤醒等待确认的发送任务 */
void screen_ack_notify_from_isr(BaseType_t *pxHigherPriorityTaskWoken)
{
    TaskHandle_t waiter = g_screen_waiter;
    if (waiter != NULL)
    {
        vTaskNotifyGiveFromISR(waiter, pxHigherPriorityTaskWoken);
    }
}

/* 标记KVDB脏数据：按item编号范围映射到对应配置类型 */
/* 实际Flash写入由task10定期刷写完成，避免屏幕分发器阻塞 */
static inline void _cfg_save_by_item(uint16_t item)
{
    /* 使用缓存标记，延迟写入 */
    if ((item >= 0x10 && item <= 0x35))
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SAMPLE);
        printf("[KVDB缓存] 采样设置已标记\r\n");
    }
    else if ((item >= 0x40 && item <= 0x46))
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_DELIVERY);
        printf("[KVDB缓存] 送样设置已标记\r\n");
    }
    else if ((item >= 0x60 && item <= 0x69) || (item >= 0x6A && item <= 0x81) || (item >= 0x8A && item <= 0xA1))
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
        printf("[KVDB缓存] 留样设置已标记\r\n");
    }
    else if ((item >= 0xB0 && item <= 0xB5))
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_COMM);
        printf("[KVDB缓存] 通信设置已标记\r\n");
    }
    else if ((item >= 0xB6 && item <= 0xBB) || item == 0xBF || (item >= 0xC0 && item <= 0xD4))
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SYSTEM);
        printf("[KVDB缓存] 系统设置已标记\r\n");
    }
    else
    {
        /* 其他未映射项：无需保存 */
    }
}

static inline void log_cfg_u8(uint8_t item, uint8_t oldv, uint8_t newv)
{
    // cfg_save_*函数内部已经有锁保护，直接调用即可
    _cfg_save_by_item(item);
}

static inline void log_cfg_u16(uint8_t item, uint16_t oldv, uint16_t newv)
{
    // cfg_save_*函数内部已经有锁保护，直接调用即可
    _cfg_save_by_item(item);
}

static inline void log_cfg_f32(uint8_t item, float oldv, float newv)
{
    // cfg_save_*函数内部已经有锁保护，直接调用即可
    _cfg_save_by_item(item);
}

static inline void log_cfg_u32(uint8_t item, uint32_t oldv, uint32_t newv)
{
    // cfg_save_*函数内部已经有锁保护，直接调用即可
    _cfg_save_by_item(item);
}

static void encode_datetime(uint8_t *dst, uint32_t ts)
{
    if (!dst)
    {
        return;
    }

    RtcDateTimeComponents dt = {0};

    // 判断时间戳基准：如果大于约30年的秒数，认为是1970年基准的Unix时间戳
    // Unix时间戳2025年约为55年（55*365*24*3600），而2000年基准的时间戳2025年约为25年
    if (ts > 30UL * 365UL * 24UL * 3600UL)
    {
        // 1970年基准的Unix时间戳，需要转换为2000年基准
        // 1970年到2000年的时间差：30年 + 7个闰日 = 30*365 + 7 天
        uint32_t seconds_1970_to_2000 = (30UL * 365UL + 7UL) * 24UL * 3600UL;
        rtc_seconds_to_datetime(ts - seconds_1970_to_2000, &dt);
    }
    else
    {
        // 已经是2000年基准的时间戳，直接使用
        rtc_seconds_to_datetime(ts, &dt);
    }

    dst[0] = (uint8_t)(dt.year - 2000u);
    dst[1] = dt.month;
    dst[2] = dt.day;
    dst[3] = dt.hour;
    dst[4] = dt.minute;
    dst[5] = dt.second;
}

uint8_t SamplingMode_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x50, 0x10, 0x00, 0x02};  // 设置采样模式
uint8_t SetAB_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x26, 0x00, 0x01};         // 当前AB桶显示（A=1 B=2）
uint8_t SamplingState_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x27, 0x00, 0x01}; // 当前采样状态，0-50个状态，对应

uint8_t ChangeHomePage_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x15}; // 切换到主页面

#define LOG_PAGE_ITEM_COUNT 7u
#define LOG_TIME_WINDOW_SECONDS (3u * 24u * 3600u) // 3天查询窗口
#define SAMPLING_LOG_BUFFER_SIZE 256u
#define DELIVERY_LOG_BUFFER_SIZE 256u
#define RETAIN_LOG_BUFFER_SIZE 256u
#define POWER_LOG_BUFFER_SIZE 256u
#define DOOR_LOG_BUFFER_SIZE 256u
#define SAMPLING_RECORD_BYTES 12u
#define DELIVERY_RECORD_BYTES 12u
#define RETAIN_RECORD_BYTES 14u
#define POWER_RECORD_BYTES 8u
#define DOOR_RECORD_BYTES 8u
#define SAMPLING_FRAME_DATA_LEN (2u + LOG_PAGE_ITEM_COUNT * SAMPLING_RECORD_BYTES)
#define DELIVERY_FRAME_DATA_LEN (2u + LOG_PAGE_ITEM_COUNT * DELIVERY_RECORD_BYTES)
#define RETAIN_FRAME_DATA_LEN (2u + LOG_PAGE_ITEM_COUNT * RETAIN_RECORD_BYTES)
#define POWER_FRAME_DATA_LEN (2u + LOG_PAGE_ITEM_COUNT * POWER_RECORD_BYTES)
#define DOOR_FRAME_DATA_LEN (2u + LOG_PAGE_ITEM_COUNT * DOOR_RECORD_BYTES)
#define SAMPLING_FRAME_TOTAL_LEN (3u + SAMPLING_FRAME_DATA_LEN)
#define DELIVERY_FRAME_TOTAL_LEN (3u + DELIVERY_FRAME_DATA_LEN)
#define RETAIN_FRAME_TOTAL_LEN (3u + RETAIN_FRAME_DATA_LEN)
#define POWER_FRAME_TOTAL_LEN (3u + POWER_FRAME_DATA_LEN)
#define DOOR_FRAME_TOTAL_LEN (3u + DOOR_FRAME_DATA_LEN)

typedef enum
{
    LOG_QUERY_SAMPLING = 0,
    LOG_QUERY_DELIVERY = 1,
    LOG_QUERY_RETAIN = 2,
    LOG_QUERY_POWER = 3,
    LOG_QUERY_DOOR = 4,
} LogQueryType;

/* ============ 日志查询会话（使用record_cache） ============ */
typedef struct
{
    uint8_t current_page;    // 当前页码
    uint16_t snapshot_total; // 进入查询时的记录总数快照
    uint8_t valid;           // 查询会话是否有效
} LogQuerySession;

static LogQuerySession g_log_session[5] = {0}; // 5种日志类型

//==============================================================================
// 日志查询上下文管理函数（使用record_cache）
//==============================================================================

/**
 * @brief 计算总页数
 */
static size_t log_session_total_pages(LogQueryType type)
{
    uint16_t count = cache_get_count((CacheType)type);
    size_t pages = (count + LOG_PAGE_ITEM_COUNT - 1u) / LOG_PAGE_ITEM_COUNT;
    return (pages == 0u) ? 1u : pages;
}

/**
 * @brief 重置日志查询会话
 */
static void log_session_reset(LogQueryType type)
{
    g_log_session[type].current_page = 0;
    g_log_session[type].snapshot_total = cache_get_count((CacheType)type);
    g_log_session[type].valid = 1;
}

static void send_sampling_page(uint8_t page)
{
    uint8_t frame[SAMPLING_FRAME_TOTAL_LEN] = {0};
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t)SAMPLING_FRAME_DATA_LEN;
    frame[3] = 0x82;
    frame[4] = 0x51;
    frame[5] = 0x82;

    // 从record_cache查询数据
    SamplingStartRecord starts[LOG_PAGE_ITEM_COUNT];
    SamplingCompleteRecord completes[LOG_PAGE_ITEM_COUNT];
    uint8_t count = 0;
    cache_query_sampling(page, LOG_PAGE_ITEM_COUNT, starts, completes, &count);

    for (size_t i = 0; i < LOG_PAGE_ITEM_COUNT; ++i)
    {
        uint8_t *slot = frame + 6 + i * SAMPLING_RECORD_BYTES;
        memset(slot, 0, SAMPLING_RECORD_BYTES);
        if (i >= count)
        {
            continue;
        }
        uint16_t mode = (uint16_t)starts[i].sampling_mode;
        uint16_t bucket_code = (starts[i].bucket_id == 0u) ? 0x0001u : 0x0002u;
        uint16_t volume = completes[i].actual_volume;
        uint32_t ts = completes[i].end_time;

        slot[0] = (uint8_t)(mode >> 8);
        slot[1] = (uint8_t)(mode & 0xFFu);
        slot[2] = (uint8_t)(bucket_code >> 8);
        slot[3] = (uint8_t)(bucket_code & 0xFFu);
        slot[4] = (uint8_t)(volume >> 8);
        slot[5] = (uint8_t)(volume & 0xFFu);
        encode_datetime(slot + 6, ts);
    }

    screen_send_notify(USART_SCREEN, frame, SAMPLING_FRAME_TOTAL_LEN, 3);
}

static void send_delivery_page(uint8_t page)
{
    uint8_t frame[DELIVERY_FRAME_TOTAL_LEN] = {0};
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t)DELIVERY_FRAME_DATA_LEN;
    frame[3] = 0x82;
    frame[4] = 0x51;
    frame[5] = 0x82;

    DeliveryStartRecord starts[LOG_PAGE_ITEM_COUNT];
    DeliveryCompleteRecord completes[LOG_PAGE_ITEM_COUNT];
    uint8_t count = 0;
    cache_query_delivery(page, LOG_PAGE_ITEM_COUNT, starts, completes, &count);

    for (size_t i = 0; i < LOG_PAGE_ITEM_COUNT; ++i)
    {
        uint8_t *slot = frame + 6 + i * DELIVERY_RECORD_BYTES;
        memset(slot, 0, DELIVERY_RECORD_BYTES);
        if (i >= count)
        {
            continue;
        }
        uint16_t mode = (uint16_t)g_SampleConfig.SamplingMode; // 触发来源来自全局配置
        uint16_t bucket_code = (starts[i].bucket_id == 0u) ? 0x0001u : 0x0002u;
        uint16_t volume = completes[i].delivery_volume;
        uint32_t ts = completes[i].end_time;

        slot[0] = (uint8_t)(mode >> 8);
        slot[1] = (uint8_t)(mode & 0xFFu);
        slot[2] = (uint8_t)(bucket_code >> 8);
        slot[3] = (uint8_t)(bucket_code & 0xFFu);
        slot[4] = (uint8_t)(volume >> 8);
        slot[5] = (uint8_t)(volume & 0xFFu);
        encode_datetime(slot + 6, ts);
    }

    screen_send_notify(USART_SCREEN, frame, DELIVERY_FRAME_TOTAL_LEN, 3);
}

static void send_retain_page(uint8_t page)
{
    uint8_t frame[RETAIN_FRAME_TOTAL_LEN] = {0};
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t)RETAIN_FRAME_DATA_LEN;
    frame[3] = 0x82;
    frame[4] = 0x51;
    frame[5] = 0x82;

    RetainLogRecord records[LOG_PAGE_ITEM_COUNT];
    uint8_t count = 0;
    cache_query_retain(page, LOG_PAGE_ITEM_COUNT, records, &count);

    for (size_t i = 0; i < LOG_PAGE_ITEM_COUNT; ++i)
    {
        uint8_t *slot = frame + 6 + i * RETAIN_RECORD_BYTES;
        memset(slot, 0, RETAIN_RECORD_BYTES);
        if (i >= count)
        {
            continue;
        }
        uint16_t mode = (uint16_t)records[i].retain_mode;
        uint16_t bottle = records[i].bottle_number;
        uint16_t volume = records[i].retain_volume;
        uint32_t ts = records[i].end_time;

        slot[0] = (uint8_t)(mode >> 8);
        slot[1] = (uint8_t)(mode & 0xFFu);
        slot[2] = (uint8_t)(bottle >> 8);
        slot[3] = (uint8_t)(bottle & 0xFFu);
        slot[4] = (uint8_t)(volume >> 8);
        slot[5] = (uint8_t)(volume & 0xFFu);
        encode_datetime(slot + 6, ts);
        slot[12] = 0x00;
        slot[13] = 0x01;
    }

    screen_send_notify(USART_SCREEN, frame, RETAIN_FRAME_TOTAL_LEN, 3);
}

static void send_power_page(uint8_t page)
{
    uint8_t frame[POWER_FRAME_TOTAL_LEN] = {0};
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t)POWER_FRAME_DATA_LEN;
    frame[3] = 0x82;
    frame[4] = 0x51;
    frame[5] = 0x82;

    PowerEventCache_t events[LOG_PAGE_ITEM_COUNT];
    uint8_t count = 0;
    cache_query_power(page, LOG_PAGE_ITEM_COUNT, events, &count);

    for (size_t i = 0; i < LOG_PAGE_ITEM_COUNT; ++i)
    {
        uint8_t *slot = frame + 6 + i * POWER_RECORD_BYTES;
        memset(slot, 0, POWER_RECORD_BYTES);
        if (i >= count)
        {
            continue;
        }
        uint16_t mode = events[i].event_type;
        uint32_t ts = events[i].timestamp;

        slot[0] = (uint8_t)(mode >> 8);
        slot[1] = (uint8_t)(mode & 0xFFu);
        encode_datetime(slot + 2, ts);
    }

    screen_send_notify(USART_SCREEN, frame, POWER_FRAME_TOTAL_LEN, 3);
}

static void send_door_page(uint8_t page)
{
    uint8_t frame[DOOR_FRAME_TOTAL_LEN] = {0};
    frame[0] = 0x5A;
    frame[1] = 0xA5;
    frame[2] = (uint8_t)DOOR_FRAME_DATA_LEN;
    frame[3] = 0x82;
    frame[4] = 0x51;
    frame[5] = 0x82;

    DoorEventCache_t events[LOG_PAGE_ITEM_COUNT];
    uint8_t count = 0;
    cache_query_door(page, LOG_PAGE_ITEM_COUNT, events, &count);

    for (size_t i = 0; i < LOG_PAGE_ITEM_COUNT; ++i)
    {
        uint8_t *slot = frame + 6 + i * DOOR_RECORD_BYTES;
        memset(slot, 0, DOOR_RECORD_BYTES);
        if (i >= count)
        {
            continue;
        }
        uint16_t mode = events[i].event_type;
        uint32_t ts = events[i].timestamp;

        slot[0] = (uint8_t)(mode >> 8);
        slot[1] = (uint8_t)(mode & 0xFFu);
        encode_datetime(slot + 2, ts);
    }

    screen_send_notify(USART_SCREEN, frame, DOOR_FRAME_TOTAL_LEN, 3);
}

//
static void notify_log_page(LogQueryType type, uint8_t page)
{
    size_t pages = log_session_total_pages(type);
    uint16_t count = cache_get_count((CacheType)type);

    printf("[日志显示] 类型=%d, 当前页=%u/%u, 记录数=%u\r\n",
           type, (unsigned int)(page + 1), (unsigned int)pages,
           (unsigned int)count);

    switch (type)
    {
    case LOG_QUERY_SAMPLING:
        send_sampling_page(page);
        break;
    case LOG_QUERY_DELIVERY:
        send_delivery_page(page);
        break;
    case LOG_QUERY_RETAIN:
        send_retain_page(page);
        break;
    case LOG_QUERY_POWER:
        send_power_page(page);
        break;
    case LOG_QUERY_DOOR:
        send_door_page(page);
        break;
    }
}

/**
 * @brief 处理日志查询请求（首次进入）
 * @param type 日志类型
 */
static void handle_log_query_request(LogQueryType type)
{
    // 重置会话
    log_session_reset(type);

    printf("[日志查询] 类型=%d, 缓存记录数=%u\r\n",
           type, cache_get_count((CacheType)type));

    // 发送第一页数据
    notify_log_page(type, 0);
}

/**
 * @brief 处理日志翻页
 * @param type 日志类型
 * @param command 翻页命令（0x01=上一页，0x02=下一页）
 */
static void handle_log_page_navigation(LogQueryType type, uint8_t command)
{
    if (!g_log_session[type].valid)
        return;

    size_t total_pages = log_session_total_pages(type);
    uint8_t current_page = g_log_session[type].current_page;

    if (command == 0x02u)
    {   /* 下一页 */
        if (current_page + 1 < total_pages)
        {
            g_log_session[type].current_page++;
        }
        else
        {
            printf("[翻页] 已到达最后一页\r\n");
        }
    }
    else if (command == 0x01u)
    {   /* 上一页 */
        if (current_page > 0u)
        {
            g_log_session[type].current_page--;
        }
    }

    // 发送当前页数据
    notify_log_page(type, g_log_session[type].current_page);
}

// 屏幕消息分发器 - 统一处理所有屏幕消息，避免竞争  //改usart2波特率
void screen_message_dispatcher(void)
{
    // 检查分发器是否被停止
    if (s_screen_dispatcher_stopped)
    {
        return;
    }

    // 检查非阻塞瓶位移动状态
    bottle_move_check_and_update();

    UartMessage msg;
    // 非阻塞检查消息队列
    if (xQueueReceive(queue_screen_handle, &msg, 0) == pdPASS)
    {
        // 最小消息长度检查：协议头(5A A5) + 长度 + 命令 + 地址高 + 地址低 = 6字节
        if (msg.len < 6) {
            return;
        }

        // 检查主页通知消息: 5A A5 06 83 00 00 01 80 00
        if (msg.len >= 9 &&
                msg.data[0] == 0x5A && msg.data[1] == 0xA5 &&
                msg.data[2] == 0x06 && msg.data[3] == 0x83 &&
                msg.data[4] == 0x00 && msg.data[5] == 0x00 &&
                msg.data[6] == 0x01 && msg.data[7] == 0x80 &&
                msg.data[8] == 0x00)
        {
            // 回到主页
            screen_set_page_state(SCREEN_PAGE_HOME);
            return;
        }

        // 检查进入设置页通知消息: 5A A5 06 83 00 00 01 81 44
        if (msg.len >= 9 &&
                msg.data[0] == 0x5A && msg.data[1] == 0xA5 &&
                msg.data[2] == 0x06 && msg.data[3] == 0x83 &&
                msg.data[4] == 0x00 && msg.data[5] == 0x00 &&
                msg.data[6] == 0x01 && msg.data[7] == 0x81 &&
                msg.data[8] == 0x44)
        {
            // 进入设置页，反写软件版本信息
            printf("[屏幕] 进入设置页，反写软件版本\r\n");
            write_software_version_page();
            return;
        }

        // 检查进入通道设置页通知消息: 5A A5 06 83 00 00 01 81 33
        if (msg.len >= 9 &&
                msg.data[0] == 0x5A && msg.data[1] == 0xA5 &&
                msg.data[2] == 0x06 && msg.data[3] == 0x83 &&
                msg.data[4] == 0x00 && msg.data[5] == 0x00 &&
                msg.data[6] == 0x01 && msg.data[7] == 0x81 &&
                msg.data[8] == 0x33)
        {
            // 进入通道设置页，开始5秒周期发送ADC数据
            printf("[屏幕] 进入通道设置页，启动5秒周期发送\r\n");
            screen_set_page_state(SCREEN_PAGE_CHANNEL_SETTINGS);
            return;
        }

        // 软件版本信息处理: 50E0(序列号), 50E8(核心板), 54F0(液晶屏)
        // 报文格式: 5A A5 xx 83 50 E0/E8 或 5A A5 xx 83 54 F0
        if (msg.data[0] == 0x5A && msg.data[1] == 0xA5 && msg.data[3] == 0x83)
        {
            uint8_t addr_h = msg.data[4];
            uint8_t addr_l = msg.data[5];
            if ((addr_h == 0x50 && (addr_l == 0xE0 || addr_l == 0xE8)) ||
                (addr_h == 0x54 && addr_l == 0xF0))
            {
                handle_software_version(&msg);
                return;
            }
        }

        // 检查消息头是否有效 (5A A5 06 83, 5A A5 08 83, 5A A5 10 83 或 5A A5 36 83)
        // 0x06用于普通6字节命令，0x08用于8字节ID卡号命令，0x10用于时间响应命令，0x36用于定时送样响应
        if (msg.data[0] == 0x5A && msg.data[1] == 0xA5 &&
                (msg.data[2] == 0x06 || msg.data[2] == 0x08 || msg.data[2] == 0x10 || msg.data[2] == 0x36) &&
                msg.data[3] == 0x83)
        {

            uint8_t cmd_type = msg.data[4];
            uint8_t sub_cmd = msg.data[5];

            // 定时送样响应数据：5A A5 36 83 20 00 19 ...
            if (msg.data[2] == 0x36 && cmd_type == 0x20 && sub_cmd == 0x00)
            {
                parse_fixed_delivery_response(&msg);
                return;
            }

            // 特殊处理：时间响应数据 (50 B6 06)
            if (msg.len >= 7 && cmd_type == 0x50 && sub_cmd == 0xB6 && msg.data[6] == 0x06)
            {
                parse_and_cache_time_response(&msg);
                return;
            }

            // 页面状态检测：进入设置类消息时切换状态
            if (cmd_type == 0x50 || cmd_type == 0x51)
            {
                if (g_screen_page_state == SCREEN_PAGE_HOME ||  g_screen_page_state == SCREEN_PAGE_OTHER){
									screen_set_page_state(SCREEN_PAGE_SETTINGS);
								}
            }

            // 特殊处理：ID卡号设置命令 (data[2] == 0x08)
            if (msg.data[2] == 0x08 && cmd_type == 0x50)
            {
                // 检查是否是ID卡号设置命令 (0xC0-0xD6范围内的子命令)
                if ((sub_cmd >= 0xC0 && sub_cmd <= 0xCE) ||
                        (sub_cmd >= 0xD0 && sub_cmd <= 0xD6) ||
                        sub_cmd == 0xC4 || sub_cmd == 0xC6 || sub_cmd == 0xC8 ||
                        sub_cmd == 0xCA || sub_cmd == 0xCC)
                {
                    printf("[分发器] 检测到ID卡号设置命令 (0x%02X)\n", sub_cmd);
                    handle_card_id_update(&msg);
                    return;
                }
            }

            // 登录消息: 5A A5 06 83 50 02 01
            if (msg.len >= 7 && cmd_type == 0x50 && sub_cmd == 0x02 && msg.data[6] == 0x01)
            {
                handle_login(&msg);
            }
            // TSDB记录重置: 5A A5 06 83 50 04 01 1E 03
            else if (msg.len >= 9 && cmd_type == 0x50 && sub_cmd == 0x04 && msg.data[6] == 0x01
                     && msg.data[7] == 0x1E && msg.data[8] == 0x03)
            {
                // 只有在复位状态下才能执行
                if (g_State.State == 0)
                {
                    printf("[屏幕] 收到TSDB重置请求，执行清空\r\n");

                    // 清空记录缓存
                    cache_clear_all();
                    printf("[屏幕] 记录缓存已清空\r\n");

                    // 格式化TSDB数据库
                    tsdb_format_full();
                    printf("[屏幕] TSDB清空完成\r\n");

                    // 跳转到主页
                    screen_set_page_state(SCREEN_PAGE_HOME);
                    Screen_begin();
                }
                else
                {
                    printf("[屏幕] TSDB重置请求被忽略（系统未处于复位状态）\r\n");
                }
            }
            // 系统复位请求：5A A5 06 83 20 20 01 00 01
            else if (msg.len >= 9 && cmd_type == 0x20 && sub_cmd == 0x20 && msg.data[6] == 0x01 && msg.data[8] == 0x01)
            {
                printf("[屏幕] 收到系统复位请求\r\n");
                ScreenCommand cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = SCMD_SYSTEM_RESET; // 交由task10做二次确认
                if (xQueueSend(queue_screen_cmd, &cmd, pdMS_TO_TICKS(10)) == pdTRUE)
                {
                    printf("[屏幕] 复位请求已派发到task10\r\n");
                }
                else
                {
                    printf("[屏幕] 警告：命令队列满，复位请求丢弃\r\n");
                }
            }
            // 定时送样时间设置触发：5A A5 06 83 20 19 01 00 12
            else if (cmd_type == 0x20 && sub_cmd == 0x19)
            {
                printf("[屏幕] 收到定时送样设置触发\r\n");
                send_fixed_delivery_read_command();
            }
            // 单点控制消息: 5A A5 06 83 52
            else if (cmd_type == 0x52)
            {
                handle_single_command(&msg);
            }
            // 设置类消息: 5A A5 06 83 50
            else if (cmd_type == 0x50)
            {
                // 采样设置: 0x10-0x1C
                if (sub_cmd >= 0x10 && sub_cmd <= 0x1C)
                {
                    handle_sampling_settings(&msg);
                }
                // 送样设置: 0x40-0x46
                else if (sub_cmd >= 0x40 && sub_cmd <= 0x46)
                {
                    handle_delivery_settings(&msg);
                }
                // 留样设置: 0x60-0x69
                else if (sub_cmd >= 0x60 && sub_cmd <= 0x69)
                {
                    handle_storage_settings(&msg);
                }
                // 留样通道设置: 0x6A-0x81
                else if (sub_cmd >= 0x6A && sub_cmd <= 0x81)
                {
                    handle_storage_channel(&msg);
                }
                // 通道AD设置: 0x8A-0xA1
                else if (sub_cmd >= 0x8A && sub_cmd <= 0xA1)
                {
                    handle_channel_ad(&msg);
                }
                // 通讯设置: 0xB0-0xBB
                else if (sub_cmd >= 0xB0 && sub_cmd <= 0xBB)
                {
                    handle_comm_settings(&msg);
                }
                // 门禁设置: 0xBF-0xD4
                else if (sub_cmd >= 0xBF && sub_cmd <= 0xD4)
                {
                    handle_access_settings(&msg);
                }
            }
            // 精度/校准设置: 5A A5 06 83 51
            else if (cmd_type == 0x51)
            {
                handle_calibration_settings(&msg);
            }
            // 确认消息处理
            else if (msg.len >= 9 && cmd_type == 0x00 && sub_cmd == 0x00 && msg.data[6] == 0x01)
            {
                uint8_t action = msg.data[7];
                uint8_t value = msg.data[8];

                if (action == 0x81 && value == 0x81)
                {
                    // 进入数据备份页面（不执行任何清空操作）
                    printf("[屏幕] 进入数据备份页面\r\n");
                }
                else if (action == 0x00 && value == 0x02)
                {
                    printf("屏幕命令：系统启动\n"); // 启动按钮
                    if (g_State.State == 0)
                    {
                        g_State.State = 1;
                        NVIC_SystemReset();
                    }
                    //                    system_start_sequence(START_MODE_MANUAL);  // 手动启动模式
                    //                    // 使用正确的主页跳转命令
                    //                    uint8_t home_page_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x0B};
                    //                    screen_send_notify(USART_SCREEN, home_page_buf, sizeof(home_page_buf), 3);
                    //                    screen_set_page_state(SCREEN_PAGE_HOME);  // 回到主页
                }
                else if (action == 0x81 && value == 0x53)
                {   // 留样瓶状态
                    write_bottle_states_page();
                }
                else if (action == 0x14 && value == 0x01)
                {
                    printf("留样瓶复位\n"); // 留样瓶复位 - 派发到task10异步执行

                    // 只派发命令，变量更新和KVDB保存在task10中复位成功后执行
                    ScreenCommand cmd;
                    memset(&cmd, 0, sizeof(cmd));
                    cmd.type = SCMD_BOTTLE_RESET;
                    if (xQueueSend(queue_screen_cmd, &cmd, pdMS_TO_TICKS(10)) == pdTRUE)
                    {
                        printf("[屏幕] 瓶盘复位命令已派发到task10\r\n");
                    }
                    else
                    {
                        printf("[屏幕] 警告：命令队列满，复位命令丢弃\r\n");
                    }
                }
                else if (action == 0x37 && value == 0x01)
                {
                    printf("屏幕命令：系统启动\n"); // 启动按钮
                    // 启动程序system_start_sequence();
                    handle_login(&msg);
                }
                else if (action == 0x23 && value == 0x01)
                {   /* 手动采样执行 */
                    printf("屏幕命令：手动采样开始（桶=%u, 体积=%u ml）\n", g_SingleSampleTest.sampleBucket, g_SingleSampleTest.sampleVolume);
                    /* 启动手动采样任务 */
                    test_sampling_execute();
                }
                else if (action == 0x21 && value == 0x01)
                {   /* 手动送样执行 */
                    if (g_SingleSampleTest.deliveryMode == 0)
                    {
                        test_instant_delivery_execute();
                    }
                    else
                        test_delivery_execute();
                }
                else if (action == 0x25 && value == 0x01)
                {   /* 手动留样执行 */
                    if (g_SingleSampleTest.retainMode == 0)
                    {
                        test_instant_retention_execute();
                    }
                    else
                        test_retention_execute();
                }
                else if (action == 0x12 && value == 0x01)
                {   /* 时间设置按钮 */
                    printf("[屏幕] 时间设置按钮按下\n");
                    send_time_read_command(); // 发送时间读取命令
                }
                else if (action == 0x03 && value == 0x00)
                {   /* 紧急停止 */
                    //                    printf("屏幕命令：紧急停止！\n");
                    sampling_emergency_stop();
                }
                else if (action == 0x81)
                {   /* 首次进入各类日志查询 */
                    /* value: 0x71 采样; 0x72 送样; 0x73 留样; 0x76 断电记录; 0x77 门禁记录; 0x32 ADC数值; 0x41 高级设置 */
                    if (value == 0x71)
                    {
                        handle_log_query_request(LOG_QUERY_SAMPLING);
                    }
                    else if (value == 0x72)
                    {
                        handle_log_query_request(LOG_QUERY_DELIVERY);
                    }
                    else if (value == 0x73)
                    {
                        handle_log_query_request(LOG_QUERY_RETAIN);
                    }
                    else if (value == 0x76)
                    {
                        handle_log_query_request(LOG_QUERY_POWER);
                    }
                    else if (value == 0x77)
                    {
                        handle_log_query_request(LOG_QUERY_DOOR);
                    }
                    else if (value == 0x32)
                    {
                        // 写入ADC通道数值  写入5082-COD数值 5083-5087是设定的ADC数值 5088=流量值
                    }
                    else if (value == 0x41)
                    {
                        // 进入高级设置页，设置页面状态
                        screen_set_page_state(SCREEN_PAGE_ADVANCED_SETTINGS);
                        printf("[屏幕] 进入高级设置页\r\n");
                    }
                }
                else if (action == 0x71)
                {   /* 采样日志翻页 */
                    handle_log_page_navigation(LOG_QUERY_SAMPLING, value);
                }
                else if (action == 0x72)
                {   /* 送样日志翻页 */
                    handle_log_page_navigation(LOG_QUERY_DELIVERY, value);
                }
                else if (action == 0x73)
                {   /* 留样日志翻页 */
                    handle_log_page_navigation(LOG_QUERY_RETAIN, value);
                }
                else if (action == 0x76)
                {   /* 断电记录翻页 */
                    handle_log_page_navigation(LOG_QUERY_POWER, value);
                }
                else if (action == 0x77)
                {   /* 门禁记录翻页 */
                    handle_log_page_navigation(LOG_QUERY_DOOR, value);
                }
            }
        }
    }

    // 尝试延迟保存KVDB数据（非阻塞）
    bottle_move_kvdb_try_save();
}

// 登录状态枚举
typedef enum
{
    LOGIN_IDLE = 0,      // 空闲状态
    LOGIN_WAIT_PASSWORD, // 等待密码
    LOGIN_WAIT_CONFIRM   // 等待确认
} LoginState;

// 登录状态结构体
typedef struct
{
    LoginState state;
    uint16_t password;
    uint8_t login_success;
} LoginInfo;

static LoginInfo g_login_info = {LOGIN_IDLE, 0, 0};

// 页面跳转命令
static uint8_t PageAdmin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x6F};    // 管理员成功页面
static uint8_t PageOperator_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x97}; // 操作员成功页面
static uint8_t PageSampler_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x99};  // 取样员成功页面
static uint8_t PageFail_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x1B};     // 登录失败页面

// 登录处理函数 - 直接处理分发器传来的消息
void handle_login(UartMessage *msg)
{
    static uint32_t login_start_time = 0;

    printf("[登录] 收到消息，状态=%d\r\n", g_login_info.state);

    // 超时保护
    if (g_login_info.state != LOGIN_IDLE)
    {
        if (login_start_time == 0)
        {
            login_start_time = xTaskGetTickCount();
        }
        else if (xTaskGetTickCount() - login_start_time > pdMS_TO_TICKS(30000))
        {
            g_login_info.state = LOGIN_IDLE;
            g_login_info.password = 0;
            login_start_time = 0;
            printf("[登录] 超时，重置为空闲状态\r\n");
            return;
        }
    }
    else
    {
        login_start_time = 0;
    }

    // 密码消息：5A A5 06 83 50 02 01 XX XX
    if (msg->data[4] == 0x50 && msg->data[5] == 0x02 && msg->data[6] == 0x01)
    {
        g_login_info.password = (msg->data[7] << 8) | msg->data[8];
        g_login_info.state = LOGIN_WAIT_CONFIRM;
        printf("[登录] 收到密码：0x%04X，状态->等待确认\r\n", g_login_info.password);
    }
    // 确认消息：5A A5 06 83 00 00 01 37 01
    else if (msg->data[4] == 0x00 && msg->data[5] == 0x00 &&
             msg->data[6] == 0x01 && msg->data[7] == 0x37 &&
             msg->data[8] == 0x01)
    {

        printf("[登录] 收到确认，状态=%d, 密码=0x%04X\r\n",
               g_login_info.state, g_login_info.password);

        if (g_login_info.state != LOGIN_WAIT_CONFIRM)
        {
            printf("[登录] 错误：不在等待确认状态！\r\n");
            return;
        }

        if (g_login_info.password == 0x091A)
        {
            printf("[登录] 管理员密码正确，跳转到管理员页面\r\n");
            vSendData(USART_SCREEN, PageAdmin_buf, 10);
            vTaskDelay(pdMS_TO_TICKS(50));
            vSendData(USART_SCREEN, PageAdmin_buf, 10);
        }
        else if (g_login_info.password == 0x1A0A)
        {
            printf("[登录] 操作员密码正确\r\n");
            vSendData(USART_SCREEN, PageOperator_buf, 10);
            vTaskDelay(pdMS_TO_TICKS(50));
            vSendData(USART_SCREEN, PageOperator_buf, 10);
        }
        else if (g_login_info.password == 0x0000)
        {
            printf("[登录] 取样员密码正确\r\n");
            vSendData(USART_SCREEN, PageSampler_buf, 10);
            vTaskDelay(pdMS_TO_TICKS(50));
            vSendData(USART_SCREEN, PageSampler_buf, 10);
        }
        else
        {
            printf("[登录] 密码错误：0x%04X\r\n", g_login_info.password);
            vSendData(USART_SCREEN, PageFail_buf, 10);
            vTaskDelay(pdMS_TO_TICKS(50));
            vSendData(USART_SCREEN, PageFail_buf, 10);
        }

        g_login_info.state = LOGIN_IDLE;
        g_login_info.password = 0;
        login_start_time = 0;
    }
}

// 单点控制处理函数 - 直接处理分发器传来的消息
void handle_single_command(UartMessage *msg)
{
    // 消息格式: 5A A5 06 83 52 XX 01 00 XX
    uint8_t device_id = msg->data[5]; // 设备ID
    uint8_t action = msg->data[8];    // 动作
    printf("[单点控制] 设备=0x%02X, 动作=0x%02X\n", device_id, action);

    switch (device_id)
    {
    case 0x00: // 采样蠕动泵
        if (action == 0x02)
            MotorRun(1, 0, g_SystemSettingConfig.Motorspeed);
        else if (action == 0x01)
            MotorRun(1, 1, g_SystemSettingConfig.Motorspeed);
        else if (action == 0x00)
            MotorStop(1);
        break;

    case 0x01: // 送样蠕动泵
        if (action == 0x02)
            MotorRun(2, 0, g_SystemSettingConfig.Motorspeed);
        else if (action == 0x01)
            MotorRun(2, 1, g_SystemSettingConfig.Motorspeed);
        else if (action == 0x00)
            MotorStop(2);
        break;

    case 0x02: // 进水三通阀
        if (action == 0x01)
            InletThreeWayValveA;
        else if (action == 0x02)
            InletThreeWayValveB;
        break;

    case 0x03: // 出水三通阀

        if (action == 0x00)
            OutletThreeWayValveClose();
        else if (action == 0x01)
            OutletThreeWayValveA();
        else if (action == 0x02)
            OutletThreeWayValveB();
        break;

    case 0x04: // 留样三通阀
        if (action == 0x00)
            SampleThreeWayValveSample;
        else if (action == 0x01)
            SampleThreeWayValveSTAY;
        break;

    case 0x05: // 瞬时三通阀
        if (action == 0x00)
            InstantThreeWayValveDirect;
        else if (action == 0x01)
            InstantThreeWayValveInstant;
        break;

    case 0x06: // 加酸蠕动泵
        if (action == 0x00)
            AcidPumpStop;
        else if (action == 0x01)
            AcidPumpRun;
        break;

    case 0x07: // 外接水泵
        if (action == 0x00)
            ExternalPumpStop;
        else if (action == 0x01)
            ExternalPumpRun;
        break;

    case 0x08: // A桶混合
        if (action == 0x00)
            MixAStop;
        else if (action == 0x01)
            MixARun;
        break;

    case 0x09: // B桶混合
        if (action == 0x00)
            MixBStop;
        else if (action == 0x01)
            MixBRun;
        break;

    /*SingleSampleTest_t g_SingleSampleTest = {
    .sampleBucket = 1,
    .sampleVolume = 500,
    .deliveryMode = 2,
    .deliveryVolume = 500,
    .retainMode = 2,
    .retainVolume = 500,
    .bottleNumber = 1

        uint8_t sampleBucket; // 采样AB桶选择，1=A桶，2=B桶
    uint16_t sampleVolume; // 采样量，单位毫升
    uint8_t deliveryMode; // 送样方式  瞬时送样=0 A桶送样=1 B桶送样=2
    uint16_t deliveryVolume; // 送样量
    uint8_t retainMode; // 留样方式  瞬时留样=0 A桶留样=1 B桶留样=2 自选留样=3
    uint16_t retainVolume; // 留样量
    uint8_t bottleNumber; // 瓶号  1-24
    };*/

    case 0x12: // 手动采样AB桶选择
        if (action == 0x01)
            g_SingleSampleTest.sampleBucket = 1; // A桶
        else if (action == 0x02)
            g_SingleSampleTest.sampleBucket = 2; // B桶
        break;

    case 0x13: // 手动采样量  最后两位是数量
        g_SingleSampleTest.sampleVolume = (msg->data[7] << 8) | msg->data[8];
        printf("手动采样量设置：%u ml\n", g_SingleSampleTest.sampleVolume);
        break;

    case 0x14: // 手动送样AB桶选择
        if (action == 0x00)
            g_SingleSampleTest.deliveryMode = 0; // 瞬时
        else if (action == 0x01)
            g_SingleSampleTest.deliveryMode = 1; // A桶
        else if (action == 0x02)
            g_SingleSampleTest.deliveryMode = 2; // B桶
        break;

    case 0x15: // 手动送样量  最后两位是数量
        g_SingleSampleTest.deliveryVolume = (msg->data[7] << 8) | msg->data[8];
        break;

    case 0x16: // 手动留样AB桶选择
        if (action == 0x00)
            g_SingleSampleTest.retainMode = 0; // 瞬时
        else if (action == 0x01)
            g_SingleSampleTest.retainMode = 1; // A桶
        else if (action == 0x02)
            g_SingleSampleTest.retainMode = 2; // B桶
        break;

    case 0x17: // 手动留样量  最后两位是数量
        g_SingleSampleTest.retainVolume = (msg->data[7] << 8) | msg->data[8];
        break;

    case 0x18: // 留样瓶选择
        g_SingleSampleTest.bottleNumber = action;
        break;

    case 0x19:                                        // 转动到对应瓶号  转动到位瓶号  1-24
        g_SingleSampleTest.turnbottleNumber = action; //  留样瓶号=action  使用留样程序转动到相对应位置

        // 非阻塞方式执行瓶位移动，超时150秒
        {
            uint8_t start_result = bottle_move_to_start(action, 40, 150000);
            if (start_result == 0)
            {
                // 启动成功，设置待处理标志
                s_bottle_move_pending = 1;
                s_bottle_move_target_action = action;
                printf("[留样瓶] 非阻塞移动已启动，目标瓶号=%d\r\n", action);
            }
            else if (start_result == 3)
            {
                // 已在目标位置，直接更新KVDB
                extern RetainSampleModeConfig g_RetainSampleConfig;
                extern RetainBottleState g_RetainBottleState;
                extern uint8_t g_current_bottle_number;

                g_RetainSampleConfig.bottleNumber = action;
                g_current_bottle_number = action;
                g_RetainBottleState.currentBottle = action;

                if (g_RetainSampleConfig.EnableSample == 0 && action != 24)
                {
                    g_RetainSampleConfig.EnableSample = 1;
                    printf("[系统] 离开24号瓶，已重新启用留样功能\r\n");
                }

                kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
                kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);
                printf("[KVDB缓存] 已在%d号瓶位置，已标记\r\n", action);

                // 更新屏幕显示变量
                update_bottle_display();
            }
            else if (start_result == 1)
            {
                printf("[留样瓶] 瓶位移动正在进行中，请稍后再试\r\n");
            }
            else
            {
                printf("[留样瓶] 启动移动失败，错误码=%d\r\n", start_result);
            }
        }
        break;

    case 0x1A:                                         // 排空对应瓶号
        g_SingleSampleTest.emptybottleNumber = action; // 使用留样程序转动到相对应位置
        emptybottle(g_SingleSampleTest.emptybottleNumber, 40, 5000);

        // 排空完成后也需要更新KVDB
        extern RetainBottleState g_RetainBottleState;
        g_RetainBottleState.usedMask &= ~(1UL << (action - 1)); // 清除该瓶的使用标记
        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);
        printf("[KVDB缓存] %d号瓶已排空，已标记\r\n", action);
        break;

    case 0x0A: // A桶排水
        if (action == 0x00)
            DrainAStop;
        else if (action == 0x01)
            DrainARun;
        break;

    case 0x0B: // B桶排水
        if (action == 0x00)
            DrainBStop;
        else if (action == 0x01)
            DrainBRun;
        break;

    case 0x0C: // 门禁
        if (action == 0x00)
        {
            DoorRun;
            vTaskDelay(2000);
            DoorStop;
        }
        break;

    case 0x0D: // 触发输出
        if (action == 0x00){
					 gpio_bits_set(GPIOC, GPIO_PINS_5);
						gpio_bits_reset(GPIOB, GPIO_PINS_0 );
				}
//            TriggerStop;
        else if (action == 0x01){
				gpio_bits_reset(GPIOC, GPIO_PINS_5);
						gpio_bits_set(GPIOB, GPIO_PINS_0 );
				
				}
//            TriggerRun;
        break;

    case 0x0E: // 清洗阀
        if (action == 0x00)
            CleanStop;
        else if (action == 0x01)
            CleanRun;
        break;
    }
}
void handle_sampling_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[采样设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    uint8_t changed = 0;

    switch (sub_cmd)
    {
    case 0x10:
    {   // 采样模式（最后1字节）
        uint8_t old = g_SampleConfig.SamplingMode;
        uint8_t newv = msg->data[8];
        printf("旧值=%d,新值=%d\n", old, newv);
        if (old != newv)
        {
            g_SampleConfig.SamplingMode = newv;
            log_cfg_u8(0x10u, old, newv);
            changed = 1;
        }
        break;
    }

    case 0x11:
    {   // 采样间隔（最后2字节）
        uint16_t old = g_SampleConfig.SampleInterval;
        uint16_t newv = value;
        // 防止除零：采样间隔最小为1分钟
        if (newv < 1) {
            newv = 1;
        }
        if (old != newv)
        {
            g_SampleConfig.SampleInterval = newv;
            log_cfg_u16(0x11u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x12:
    {   // 单次采样量（最后2字节）
        uint16_t old = g_SampleConfig.SampleVolume;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.SampleVolume = newv;
            log_cfg_u16(0x12u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x13:
    {   // 采样反吹时间（最后2字节）
        uint16_t old = g_SampleConfig.BlowbackTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.BlowbackTime = newv;
            log_cfg_u16(0x13u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x14:
    {   // 采样提升时间（最后2字节）
        uint16_t old = g_SampleConfig.SamplingImproveTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.SamplingImproveTime = newv;
            log_cfg_u16(0x14u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x15:
    {   // 采样管存放时间（最后2字节）
        uint16_t old = g_SampleConfig.TubeHoldTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.TubeHoldTime = newv;
            log_cfg_u16(0x15u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x16:
    {   // 周期时间（最后2字节）
        uint16_t old = g_SampleConfig.CycleTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.CycleTime = newv;
            log_cfg_u16(0x16u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x17:
    {   // 采样桶排空时间（最后2字节）
        uint16_t old = g_SampleConfig.BucketDrainTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.BucketDrainTime = newv;
            log_cfg_u16(0x17u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x18:
    {   // 仪器分析时间（最后2字节）
        uint16_t old = g_SampleConfig.AnalysisTime;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.AnalysisTime = newv;
            log_cfg_u16(0x18u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x19:
    {   // 排放量（最后2字节）
        uint16_t old = g_SampleConfig.DischargeVolume;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.DischargeVolume = newv;
            log_cfg_u16(0x19u, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x1A:
    {   // 流量比例（最后2字节）
        uint16_t old = g_SampleConfig.FlowRatio;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.FlowRatio = newv;
            log_cfg_u16(0x1Au, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x1B:
    {   // 流量触发（最后2字节）
        uint16_t old = g_SampleConfig.FlowStart;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.FlowStart = newv;
            log_cfg_u16(0x1Bu, old, newv);
            changed = 1;
        }
        break;
    }
    case 0x1C:
    {   // 流量停止值（最后2字节）
        uint16_t old = g_SampleConfig.FlowStop;
        uint16_t newv = value;
        if (old != newv)
        {
            g_SampleConfig.FlowStop = newv;
            log_cfg_u16(0x1Cu, old, newv);
            changed = 1;
        }
        break;
    }
    default:
        break;
    }

    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SAMPLE);
        printf("[KVDB缓存] 采样设置已标记\r\n");

        // 立即刷新屏幕设置页面
        write_sampling_settings_page();
    }
}

// 送样设置处理函数 (0x40-0x46)
// 收到  5A A5 06 83 50 40 01 00 03   变量地址5040 最后两个字节是数值  送样开始时间-小时 对应g_DeliveryConfig.StartHour
// 收到  5A A5 06 83 50 41 01 00 03   变量地址5041 最后两个字节是数值  送样开始时间-分钟 对应g_DeliveryConfig.StartMin
// 收到  5A A5 06 83 50 42 01 00 03   变量地址5042 最后两个字节是数值  送样时长 对应g_DeliveryConfig.Duration
// 收到  5A A5 06 83 50 43 01 00 03   变量地址5043 最后两个字节是数值  送样回抽 对应g_DeliveryConfig.Interval
// 收到  5A A5 06 83 50 44 01 00 03   变量地址5044 最后两个字节是数值  送样结束时间-小时 对应g_DeliveryConfig.EndHour
// 收到  5A A5 06 83 50 45 01 00 03   变量地址5045 最后两个字节是数值  送样结束时间-分钟 对应g_DeliveryConfig.EndMin
// 收到  5A A5 06 83 50 46 01 00 03   变量地址5046 最后两个字节是数值  定时启动 对应g_DeliveryConfig.Enable
void handle_delivery_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[送样设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    uint8_t changed = 0;
    switch (sub_cmd)
    {
    case 0x40:
    {   // 开始小时 0-23
        uint8_t old = g_DeliveryConfig.StartHour;
        uint8_t v = (value <= 23) ? (uint8_t)value : old;
        if (v != old)
        {
            g_DeliveryConfig.StartHour = v;
            log_cfg_u8(0x40u, old, v);
            _recompute_delivery_schedule(); // 新函数：重新计算送样调度
            changed = 1;
        }
        break;
    }
    case 0x41:
    {   // 开始分钟 0-59
        uint8_t old = g_DeliveryConfig.StartMin;
        uint8_t v = (value <= 59) ? (uint8_t)value : old;
        if (v != old)
        {
            g_DeliveryConfig.StartMin = v;
            log_cfg_u8(0x41u, old, v);
            _recompute_delivery_schedule(); // 新函数：重新计算送样调度
            changed = 1;
        }
        break;
    }
    case 0x42:
    {   // 送样时长(s)
        uint16_t old = g_DeliveryConfig.Duration;
        uint16_t v = value;
        if (v != old)
        {
            g_DeliveryConfig.Duration = v;
            log_cfg_u16(0x42u, old, v);
            _recompute_delivery_schedule(); // 新函数：重新计算送样调度
            // 自动计算并写入 5044=小时  5045=分钟
            changed = 1;
        }
        break;
    }
    case 0x43:
    {   // 回抽间隔(s)
        uint16_t old = g_DeliveryConfig.Interval;
        uint16_t v = value;
        if (v != old)
        {
            g_DeliveryConfig.Interval = v;
            log_cfg_u16(0x43u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x44:
    {   // 结束小时（策略A：忽略屏幕设置，自动计算）
        uint8_t before = g_DeliveryConfig.EndHour;
        _recompute_delivery_end();
        (void)before; // 已在重算中按需写入并保存
        break;
    }
    case 0x45:
    {   // 结束分钟（策略A：忽略屏幕设置，自动计算）
        uint8_t before = g_DeliveryConfig.EndMin;
        _recompute_delivery_end();
        (void)before;
        break;
    }
    case 0x46:
    {   // 定时启动（Enable）0/1
        uint8_t old = g_DeliveryConfig.Enable;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_DeliveryConfig.Enable = v;
            log_cfg_u8(0x46u, old, v);
            changed = 1;
        }
        break;
    }
    default:
        break;
    }

    // 标记配置待刷写并刷新页面
    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_DELIVERY);
        printf("[KVDB缓存] 送样设置已标记\r\n");
        write_delivery_settings_page();
    }
}

/**
 * @brief 重新计算送样调度时间表
 * 根据StartHour和StartMin重新计算送样时间点，并通知调度器重新初始化
 */
void _recompute_delivery_schedule(void)
{
    // 1. 验证配置参数有效性
    if (g_DeliveryConfig.StartHour > 23 || g_DeliveryConfig.StartMin > 59)
    {
        printf("[送样调度] 配置参数无效，跳过重新计算\r\n");
        return;
    }

    printf("[送样调度] 重新计算送样调度时间表...\r\n");
    printf("[送样调度] 新的送样时间: %02d:%02d\r\n",
           g_DeliveryConfig.StartHour, g_DeliveryConfig.StartMin);

    // 2. 重新计算送样结束时间（复用原有逻辑）
    _recompute_delivery_end();

    // 3. 如果调度器正在运行，需要重新初始化调度器
    // 这里我们调用调度器的重新初始化函数
    extern void tp_scheduler_reinit_if_running(void);
    extern void fixed_time_scheduler_reinit_if_running(void);
    tp_scheduler_reinit_if_running();
    fixed_time_scheduler_reinit_if_running();

    printf("[送样调度] 送样调度时间表重新计算完成\r\n");
}

// 送样结束时间自动计算器
static void _recompute_delivery_end(void)
{
    uint32_t start_sec = (uint32_t)g_DeliveryConfig.StartHour * 3600u + (uint32_t)g_DeliveryConfig.StartMin * 60u;
    uint32_t end_sec = (start_sec + (uint32_t)g_DeliveryConfig.Duration) % (24u * 3600u);
    uint8_t new_h = (uint8_t)(end_sec / 3600u);
    uint8_t new_m = (uint8_t)((end_sec % 3600u) / 60u);
    if (g_DeliveryConfig.EndHour != new_h)
    {
        uint8_t old = g_DeliveryConfig.EndHour;
        g_DeliveryConfig.EndHour = new_h;
        log_cfg_u8(0x44u, old, new_h);
    }
    if (g_DeliveryConfig.EndMin != new_m)
    {
        uint8_t old = g_DeliveryConfig.EndMin;
        g_DeliveryConfig.EndMin = new_m;
        log_cfg_u8(0x45u, old, new_m);
    }
}

// 校准设置处理函数 (cmd_type=0x51)
// 设计子命令映射：
//  0x10-0x15  采样量校准 samplingCalib: time1, realValue1, time2, realValue2, time3, realValue3
//  0x16-0x1B  留样量校准 retainSampleCalib: 同上顺序
//  0x1C-0x21  加酸量校准 acidAdditionCalib: 同上顺序
//  0x40-0x47  温度校准 tempCalib: inputAD, zeroPointAD, calibAD, calibValue, setTemp, upperDev, lowerDev, zeroTemp
void handle_calibration_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[校准设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    uint8_t changed = 0;
    switch (sub_cmd)
    {
    /* 采样量校准 0x10-0x15 */
    case 0x10:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.time1;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.time1 = value;
            log_cfg_u16(0x10u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x11:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.realValue1;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.realValue1 = value;
            log_cfg_u16(0x11u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x12:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.time2;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.time2 = value;
            log_cfg_u16(0x12u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x13:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.realValue2;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.realValue2 = value;
            log_cfg_u16(0x13u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x14:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.time3;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.time3 = value;
            log_cfg_u16(0x14u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x15:
    {
        uint16_t old = g_CalibrationParams.samplingCalib.realValue3;
        if (value != old)
        {
            g_CalibrationParams.samplingCalib.realValue3 = value;
            log_cfg_u16(0x15u, old, value);
            changed = 1;
        }
        break;
    }

    /* 留样量校准 0x16-0x1B */
    case 0x16:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.time1;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.time1 = value;
            log_cfg_u16(0x16u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x17:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.realValue1;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.realValue1 = value;
            log_cfg_u16(0x17u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x18:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.time2;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.time2 = value;
            log_cfg_u16(0x18u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x19:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.realValue2;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.realValue2 = value;
            log_cfg_u16(0x19u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x1A:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.time3;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.time3 = value;
            log_cfg_u16(0x1Au, old, value);
            changed = 1;
        }
        break;
    }
    case 0x1B:
    {
        uint16_t old = g_CalibrationParams.retainSampleCalib.realValue3;
        if (value != old)
        {
            g_CalibrationParams.retainSampleCalib.realValue3 = value;
            log_cfg_u16(0x1Bu, old, value);
            changed = 1;
        }
        break;
    }

    /* 加酸量校准 0x1C-0x21 */
    case 0x1C:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.time1;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.time1 = value;
            log_cfg_u16(0x1Cu, old, value);
            changed = 1;
        }
        break;
    }
    case 0x1D:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.realValue1;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.realValue1 = value;
            log_cfg_u16(0x1Du, old, value);
            changed = 1;
        }
        break;
    }
    case 0x1E:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.time2;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.time2 = value;
            log_cfg_u16(0x1Eu, old, value);
            changed = 1;
        }
        break;
    }
    case 0x1F:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.realValue2;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.realValue2 = value;
            log_cfg_u16(0x1Fu, old, value);
            changed = 1;
        }
        break;
    }
    case 0x20:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.time3;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.time3 = value;
            log_cfg_u16(0x20u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x21:
    {
        uint16_t old = g_CalibrationParams.acidAdditionCalib.realValue3;
        if (value != old)
        {
            g_CalibrationParams.acidAdditionCalib.realValue3 = value;
            log_cfg_u16(0x21u, old, value);
            changed = 1;
        }
        break;
    }

    /* 温度校准 0x40-0x47 */
    case 0x40:
    {
        uint16_t old = g_CalibrationParams.tempCalib.inputAD;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.inputAD = value;
            log_cfg_u16(0x40u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x41:
    {
        uint16_t old = g_CalibrationParams.tempCalib.zeroPointAD;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.zeroPointAD = value;
            log_cfg_u16(0x41u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x42:
    {
        uint16_t old = g_CalibrationParams.tempCalib.calibAD;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.calibAD = value;
            log_cfg_u16(0x42u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x43:
    {
        uint16_t old = g_CalibrationParams.tempCalib.calibValue;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.calibValue = value;
            log_cfg_u16(0x43u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x44:
    {
        uint16_t old = g_CalibrationParams.tempCalib.setTemp;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.setTemp = value;
            log_cfg_u16(0x44u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x45:
    {
        uint16_t old = g_CalibrationParams.tempCalib.upperDev;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.upperDev = value;
            log_cfg_u16(0x45u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x46:
    {
        uint16_t old = g_CalibrationParams.tempCalib.lowerDev;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.lowerDev = value;
            log_cfg_u16(0x46u, old, value);
            changed = 1;
        }
        break;
    }
    case 0x47:
    {
        uint16_t old = g_CalibrationParams.tempCalib.zeroTemp;
        if (value != old)
        {
            g_CalibrationParams.tempCalib.zeroTemp = value;
            log_cfg_u16(0x47u, old, value);
            changed = 1;
        }
        break;
    }

    default:
        break;
    }
    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_CALIB);
        printf("[KVDB缓存] 校准参数已标记\r\n");
    }
}

// 留样设置处理函数 (0x60-0x69)
// 收到  5A A5 06 83 50 60 01 00 03   变量地址5060 最后两个字节是数值  留样模式  对应 g_RetainSampleConfig.Mode
// 收到  5A A5 06 83 50 61 01 00 03   变量地址5061 最后两个字节是数值  单次留样量  对应 g_RetainSampleConfig.SampleVolume
// 收到  5A A5 06 83 50 62 01 00 03   变量地址5062 最后两个字节是数值  平行样  对应 g_RetainSampleConfig.ParallelCount
// 收到  5A A5 06 83 50 63 01 00 03   变量地址5063 最后两个字节是数值  混样次数  对应 g_RetainSampleConfig.MixCount
// 收到  5A A5 06 83 50 64 01 00 03   变量地址5064 最后两个字节是数值  留样反吹  对应 g_RetainSampleConfig.BlowbackTime
// 收到  5A A5 06 83 50 65 01 00 03   变量地址5065 最后两个字节是数值  是否留样  0/1  对应 g_RetainSampleConfig.EnableSample
// 收到  5A A5 06 83 50 66 01 00 03   变量地址5066 最后两个字节是数值  是否加酸  0/1  对应 g_RetainSampleConfig.EnableAcid
// 收到  5A A5 06 83 50 67 01 00 03   变量地址5067 最后两个字节是数值  是否排空  0/1  对应 g_RetainSampleConfig.EnableVacuum
// 收到  5A A5 06 83 50 68 01 00 03   变量地址5068 最后两个字节是数值  留样管存  对应 g_RetainSampleConfig.TubeHoldTime
// 收到  5A A5 06 83 50 69 01 00 03   变量地址5069 最后两个字节是数值  留样回抽  对应 g_RetainSampleConfig.BackdrawTime

void handle_storage_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[存储设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    uint8_t changed = 0;
    switch (sub_cmd)
    {
    case 0x60:
    {   // 留样模式
        uint8_t old = g_RetainSampleConfig.Mode;
        uint8_t v = (uint8_t)(value & 0xFF); // 使用解析后的value,不是msg->data[7]
        if (v != old)
        {
            g_RetainSampleConfig.Mode = v;
            printf("[屏幕] 留样模式变更：%u -> %u\r\n", old, v);
            kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
            printf("[KVDB缓存] 留样设置已标记\r\n");
            changed = 1;
        }
        else
        {
            printf("[屏幕] 留样模式未变更：%u\r\n", v);
        }
        break;
    }
    case 0x61:
    {   // 单次留样量(ml)
        uint16_t old = g_RetainSampleConfig.SampleVolume;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.SampleVolume = v;
            printf("[屏幕] 单次留样量变更：%u -> %u ml\r\n", old, v);
            kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
            changed = 1;
        }
        break;
    }
    case 0x62:
    {   // 平行样数量
        uint8_t old = g_RetainSampleConfig.ParallelCount;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_RetainSampleConfig.ParallelCount = v;
            log_cfg_u8(0x62u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x63:
    {   // 混样次数
        uint8_t old = g_RetainSampleConfig.MixCount;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_RetainSampleConfig.MixCount = v;
            log_cfg_u8(0x63u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x64:
    {   // 留样反吹(s)
        uint16_t old = g_RetainSampleConfig.BlowbackTime;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.BlowbackTime = v;
            log_cfg_u16(0x64u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x65:
    {   // 是否留样 0/1
        uint8_t old = g_RetainSampleConfig.EnableSample;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_RetainSampleConfig.EnableSample = v;
            log_cfg_u8(0x65u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x66:
    {   // 是否加酸 0/1
        uint8_t old = g_RetainSampleConfig.EnableAcid;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_RetainSampleConfig.EnableAcid = v;
            log_cfg_u8(0x66u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x67:
    {   // 是否瓶排空 0/1   0不排空
        uint8_t old = g_RetainSampleConfig.EnableVacuum;
        uint8_t v = (uint8_t)(value & 0xFF);
        if (v != old)
        {
            g_RetainSampleConfig.EnableVacuum = v;
            log_cfg_u8(0x67u, old, v);
            changed = 1;
        }
        break;
    }
    case 0x68:
    {   // 留样管存放时间(s)
        uint16_t old = g_RetainSampleConfig.TubeHoldTime;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.TubeHoldTime = v;
            log_cfg_u16(0x68u, old, v);
            changed = 1;
            printf("留样 KVDB完成：旧值=%u 新值=%u\r\n", old, v);
        }
        break;
    }
    case 0x69:
    {   // 留样回抽(s)
        uint16_t old = g_RetainSampleConfig.BackdrawTime;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.BackdrawTime = v;
            log_cfg_u16(0x69u, old, v);
            changed = 1;
        }
        break;
    }
    default:
        break;
    }
    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
        printf("[KVDB缓存] 留样设置已标记\r\n");
    }
}

// 留样通道设置处理函数 (0x6A-0x81)
// 收到  5A A5 06 83 50 6A 01 00 03   变量地址506A 最后两个字节是数值  通道一因子  对应 g_RetainSampleConfig.channelLimits[0].FactorType
// 收到  5A A5 06 83 50 6B 01 00 03   变量地址506B 最后两个字节是数值  通道一超标下限  对应 g_RetainSampleConfig.channelLimits[0].LowerLimit
// 收到  5A A5 06 83 50 6C 01 00 03   变量地址506C 最后两个字节是数值  通道一超标上限  对应 g_RetainSampleConfig.channelLimits[0].UpperLimit
// 收到  5A A5 06 83 50 6D 01 00 03   变量地址506D 最后两个字节是数值  通道一是否应用  对应 g_RetainSampleConfig.channelLimits[0].Enable

// 收到  5A A5 06 83 50 6E 01 00 03   变量地址506E 最后两个字节是数值  通道二因子  对应 g_RetainSampleConfig.channelLimits[1].FactorType
// 收到  5A A5 06 83 50 6F 01 00 03   变量地址506F 最后两个字节是数值  通道二超标下限  对应 g_RetainSampleConfig.channelLimits[1].LowerLimit
// 收到  5A A5 06 83 50 70 01 00 03   变量地址5070 最后两个字节是数值  通道二超标上限  对应 g_RetainSampleConfig.channelLimits[1].UpperLimit
// 收到  5A A5 06 83 50 71 01 00 03   变量地址5071 最后两个字节是数值  通道二是否应用  对应 g_RetainSampleConfig.channelLimits[1].Enable

// 收到  5A A5 06 83 50 72 01 00 03   变量地址5072 最后两个字节是数值  通道三因子        对应 g_RetainSampleConfig.channelLimits[2].FactorType
// 收到  5A A5 06 83 50 73 01 00 03   变量地址5073 最后两个字节是数值  通道三超标下限  对应 g_RetainSampleConfig.channelLimits[2].LowerLimit
// 收到  5A A5 06 83 50 74 01 00 03   变量地址5074 最后两个字节是数值  通道三超标上限  对应 g_RetainSampleConfig.channelLimits[2].UpperLimit
// 收到  5A A5 06 83 50 75 01 00 03   变量地址5075 最后两个字节是数值  通道三是否应用  对应 g_RetainSampleConfig.channelLimits[2].Enable

// 收到  5A A5 06 83 50 76 01 00 03   变量地址5076 最后两个字节是数值  通道四因子        对应 g_RetainSampleConfig.channelLimits[3].FactorType
// 收到  5A A5 06 83 50 77 01 00 03   变量地址5077 最后两个字节是数值  通道四超标下限  对应 g_RetainSampleConfig.channelLimits[3].LowerLimit
// 收到  5A A5 06 83 50 78 01 00 03   变量地址5078 最后两个字节是数值  通道四超标上限  对应 g_RetainSampleConfig.channelLimits[3].UpperLimit
// 收到  5A A5 06 83 50 79 01 00 03   变量地址5079 最后两个字节是数值  通道四是否应用  对应 g_RetainSampleConfig.channelLimits[3].Enable

// 收到  5A A5 06 83 50 7A 01 00 03   变量地址507A 最后两个字节是数值  通道五因子        对应 g_RetainSampleConfig.channelLimits[4].FactorType
// 收到  5A A5 06 83 50 7B 01 00 03   变量地址507B 最后两个字节是数值  通道五超标下限  对应 g_RetainSampleConfig.channelLimits[4].LowerLimit
// 收到  5A A5 06 83 50 7C 01 00 03   变量地址507C 最后两个字节是数值  通道五超标上限  对应 g_RetainSampleConfig.channelLimits[4].UpperLimit
// 收到  5A A5 06 83 50 7D 01 00 03   变量地址507D 最后两个字节是数值  通道五是否应用  对应 g_RetainSampleConfig.channelLimits[4].Enable

// 收到  5A A5 06 83 50 7E 01 00 03   变量地址507E 最后两个字节是数值  通道六因子        对应 g_RetainSampleConfig.channelLimits[5].FactorType
// 收到  5A A5 06 83 50 7F 01 00 03   变量地址507F 最后两个字节是数值  通道六超标下限  对应 g_RetainSampleConfig.channelLimits[5].LowerLimit
// 收到  5A A5 06 83 50 80 01 00 03   变量地址5080 最后两个字节是数值  通道六超标上限  对应 g_RetainSampleConfig.channelLimits[5].UpperLimit
// 收到  5A A5 06 83 50 81 01 00 03   变量地址5081 最后两个字节是数值  通道六是否应用  对应 g_RetainSampleConfig.channelLimits[5].Enable

void handle_storage_channel(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];  // 变量地址低字节 (0x6A-0x81)
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[存储通道] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    /* 0x6A-0x81: 每通道4项，共6通道 => 24项  一通道固定cod 六通道固定流量
       映射顺序：FactorType(u8), LowerLimit(float x100), UpperLimit(float x100), Enable(u8) */
    if (sub_cmd < 0x6A || sub_cmd > 0x81)
        return;
    uint8_t idx = (uint8_t)(sub_cmd - 0x6A); // 0..23
    uint8_t ch = idx / 4;                    // 0..5
    uint8_t item = idx % 4;                  // 0..3
    if (ch >= 6)
        return;

    uint8_t changed = 0;
    // 通道1-4，通道0和通道5是固定需要单独写

    if (ch == 0)
    {
        switch (item)
        {
        case 1:
        {
            float old = g_RetainSampleConfig.channelLimits[0].LowerLimit;
            float v = (float)value; // 通道1：原值
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[0].LowerLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 2:
        {
            float old = g_RetainSampleConfig.channelLimits[0].UpperLimit;
            float v = (float)value; // 通道1上限：原值
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[0].UpperLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 3:
        {   // Enable
            uint8_t old = g_RetainSampleConfig.channelLimits[0].Enable;
            uint8_t v = (uint8_t)(value & 0xFF);
            if (v != old)
            {
                g_RetainSampleConfig.channelLimits[0].Enable = v;
                log_cfg_u8(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        }
    }
    else if (ch == 5)
    {
        switch (item)
        {
        case 1:
        {
            float old = g_RetainSampleConfig.channelLimits[5].LowerLimit;
            float v = ((float)value) / 10.0f; // 通道6：缩小10倍
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[5].LowerLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 2:
        {
            float old = g_RetainSampleConfig.channelLimits[5].UpperLimit;
            float v = ((float)value) / 10.0f; // 通道6：缩小10倍
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[5].UpperLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 3:
        {   // Enable
            uint8_t old = g_RetainSampleConfig.channelLimits[5].Enable;
            uint8_t v = (uint8_t)(value & 0xFF);
            if (v != old)
            {
                g_RetainSampleConfig.channelLimits[5].Enable = v;
                log_cfg_u8(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        }
    }
    else
    {
        switch (item)
        {
        case 0:
        {   // FactorType
            uint8_t old = g_RetainSampleConfig.channelLimits[ch].FactorType;
            uint8_t v = msg->data[7];
            if (v != old)
            {
                g_RetainSampleConfig.channelLimits[ch].FactorType = v;
                log_cfg_u8(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 1:
        {   // LowerLimit (float = value/100.0)
            float old = g_RetainSampleConfig.channelLimits[ch].LowerLimit;
            float v = ((float)value) / 10.0f; // 通道2-5：缩小10倍
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[ch].LowerLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 2:
        {   // UpperLimit (float = value/100.0)
            float old = g_RetainSampleConfig.channelLimits[ch].UpperLimit;
            float v = ((float)value) / 10.0f; // 通道2-5：缩小10倍
            if (fabsf(v - old) > 1e-3f)
            {
                g_RetainSampleConfig.channelLimits[ch].UpperLimit = v;
                log_cfg_f32(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        case 3:
        {   // Enable
            uint8_t old = g_RetainSampleConfig.channelLimits[ch].Enable;
            uint8_t v = (uint8_t)(value & 0xFF);
            if (v != old)
            {
                g_RetainSampleConfig.channelLimits[ch].Enable = v;
                log_cfg_u8(sub_cmd, old, v);
                changed = 1;
            }
            break;
        }
        }
    }
    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
        printf("[KVDB缓存] 留样设置已标记\r\n");
    }
}

// 通道AD设置处理函数 (0x8A-0xA1)
// 收到  5A A5 06 83 50 8B 01 00 03   变量地址508B 最后两个字节是数值  通道一0点AD   对应g_RetainSampleModeConfig.channelCals[0].ZeroAD
// 收到  5A A5 06 83 50 8C 01 00 03   变量地址508C 最后两个字节是数值  通道一校准AD  对应g_RetainSampleModeConfig.channelCals[0].CalAD
// 收到  5A A5 06 83 50 8D 01 00 03   变量地址508D 最后两个字节是数值  通道一校准值  对应g_RetainSampleModeConfig.channelCals[0].CalValue
// 收到  5A A5 06 83 50 8F 01 00 03   变量地址508F 最后两个字节是数值  通道二0点AD  对应g_RetainSampleModeConfig.channelCals[1].ZeroAD
// 收到  5A A5 06 83 50 90 01 00 03   变量地址5090 最后两个字节是数值  通道二校准AD  对应g_RetainSampleModeConfig.channelCals[1].CalAD
// 收到  5A A5 06 83 50 91 01 00 03   变量地址5091 最后两个字节是数值  通道二校准值  对应g_RetainSampleModeConfig.channelCals[1].CalValue
// 收到  5A A5 06 83 50 93 01 00 03   变量地址5093 最后两个字节是数值  通道三0点AD  对应g_RetainSampleModeConfig.channelCals[2].ZeroAD
// 收到  5A A5 06 83 50 94 01 00 03   变量地址5094 最后两个字节是数值  通道三校准AD  对应g_RetainSampleModeConfig.channelCals[2].CalAD
// 收到  5A A5 06 83 50 95 01 00 03   变量地址5095 最后两个字节是数值  通道三校准值  对应g_RetainSampleModeConfig.channelCals[2].CalValue
// 收到  5A A5 06 83 50 97 01 00 03   变量地址5097 最后两个字节是数值  通道四0点AD  对应g_RetainSampleModeConfig.channelCals[3].ZeroAD
// 收到  5A A5 06 83 50 98 01 00 03   变量地址5098 最后两个字节是数值  通道四校准AD  对应g_RetainSampleModeConfig.channelCals[3].CalAD
// 收到  5A A5 06 83 50 99 01 00 03   变量地址5099 最后两个字节是数值  通道四校准值  对应g_RetainSampleModeConfig.channelCals[3].CalValue
// 收到  5A A5 06 83 50 9B 01 00 03   变量地址509B 最后两个字节是数值  通道五0点AD  对应g_RetainSampleModeConfig.channelCals[4].ZeroAD
// 收到  5A A5 06 83 50 9C 01 00 03   变量地址509C 最后两个字节是数值  通道五校准AD  对应g_RetainSampleModeConfig.channelCals[4].CalAD
// 收到  5A A5 06 83 50 9D 01 00 03   变量地址509D 最后两个字节是数值  通道五校准值  对应g_RetainSampleModeConfig.channelCals[4].CalValue
// 收到  5A A5 06 83 50 9E 01 00 03   变量地址509E 流量电流显示（MCU发送），不接收数据
// 收到  5A A5 06 83 50 9F 01 00 03   变量地址509F 流量值显示（MCU发送），不接收数据
// 收到  5A A5 06 83 50 A0 01 00 03   变量地址50A0 最后两个字节是数值  流量校准电流(4-20mA点) 对应g_CommSettingConfig.FlowADLower，存储时缩小10倍
// 收到  5A A5 06 83 50 A1 01 00 03   变量地址50A1 最后两个字节是数值  流量校准流量值 对应g_CommSettingConfig.FlowMeterBase，存储时缩小10倍

void handle_channel_ad(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[通道AD] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    /* 0x8A-0xA1: 每通道4项（InputAD, ZeroAD, CalAD, CalValue(x100)）×6通道 */
    if (sub_cmd < 0x8A || sub_cmd > 0xA1)
        return;
    uint8_t idx = (uint8_t)(sub_cmd - 0x8A); // 0..23
    uint8_t ch = idx / 4;                    // 0..5
    uint8_t item = idx % 4;                  // 0..3
    if (ch >= 6)
        return;

    // 通道6改为流量校准：50A0/50A1
    if (ch == 5)
    {
        if (item == 2)
        {   // CalAD -> 流量校准电流（0.1mA单位）
            uint16_t old = g_CommSettingConfig.FlowADLower;
            uint16_t v = value;
            if (v != old)
            {
                g_CommSettingConfig.FlowADLower = v;
                log_cfg_u16(sub_cmd, old, v);
                kvdb_cache_mark_dirty(KVDB_CACHE_COMM);
                printf("[KVDB缓存] 通讯设置已标记\r\n");
            }
        }
        else if (item == 3)
        {   // CalValue -> 流量校准流量（缩小10倍存储）
            float old = g_CommSettingConfig.FlowMeterBase;
            float v = ((float)value) / 10.0f;
            if (fabsf(v - old) > 1e-3f)
            {
                g_CommSettingConfig.FlowMeterBase = v;
                log_cfg_f32(sub_cmd, old, v);
                kvdb_cache_mark_dirty(KVDB_CACHE_COMM);
                printf("[KVDB缓存] 通讯设置已标记\r\n");
            }
        }
        // item 0/1（InputAD/ZeroAD）不再使用
        return;
    }

    uint8_t changed = 0;
    switch (item)
    {
    case 0:
    {   // InputAD
        uint16_t old = g_RetainSampleConfig.channelCals[ch].InputAD;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.channelCals[ch].InputAD = v;
            log_cfg_u16(sub_cmd, old, v);
            changed = 1;
        }
        break;
    }
    case 1:
    {   // ZeroAD
        uint16_t old = g_RetainSampleConfig.channelCals[ch].ZeroAD;
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.channelCals[ch].ZeroAD = v;
            log_cfg_u16(sub_cmd, old, v);
            changed = 1;
        }
        break;
    }
    case 2:
    {   // CalAD
        uint16_t old = g_RetainSampleConfig.channelCals[ch].CalAD;
        /* Cal current uses 0.1mA units from screen input. */
        uint16_t v = value;
        if (v != old)
        {
            g_RetainSampleConfig.channelCals[ch].CalAD = v;
            log_cfg_u16(sub_cmd, old, v);
            changed = 1;
        }
        break;
    }
    case 3:
    {   // CalValue (float = value)
        float old = g_RetainSampleConfig.channelCals[ch].CalValue;
        float v;
        if (ch == 0)
        {
            // 通道1：原值
            v = (float)value;
        }
        else
        {
            // 通道2-5：缩小10倍
            v = ((float)value) / 10.0f;
        }
        if (fabsf(v - old) > 1e-3f)
        {
            g_RetainSampleConfig.channelCals[ch].CalValue = v;
            log_cfg_f32(sub_cmd, old, v);
            changed = 1;
        }
        break;
    }
    }
    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
        printf("[KVDB缓存] 留样设置已标记\r\n");
    }
}
// 精度校准处理函数
// 收到  5A A5 06 83 51 10 01 00 00   采样量时间1校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.time1
// 收到  5A A5 06 83 51 11 01 00 00   采样量真实值1校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.realValue1
// 收到  5A A5 06 83 51 12 01 00 00   采样量时间2校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.time2
// 收到  5A A5 06 83 51 13 01 00 00   采样量真实值2校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.realValue2
// 收到  5A A5 06 83 51 14 01 00 00   采样量时间3校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.time3
// 收到  5A A5 06 83 51 15 01 00 00   采样量真实值3校准 最后两个字节是数值  对应g_CalibrationParams.samplingCalib.realValue3
// 收到  5A A5 06 83 51 16 01 00 00   留样量时间1校准 最后两个字节是数值   对应g_CalibrationParams.retainSampleCalib.time1
// 收到  5A A5 06 83 51 17 01 00 00   留样量真实值1校准 最后两个字节是数值  对应g_CalibrationParams.retainSampleCalib.realValue1
// 收到  5A A5 06 83 51 18 01 00 00   留样量时间2校准 最后两个字节是数值  对应g_CalibrationParams.retainSampleCalib.time2
// 收到  5A A5 06 83 51 19 01 00 00   留样量真实值2校准 最后两个字节是数值  对应g_CalibrationParams.retainSampleCalib.realValue2
// 收到  5A A5 06 83 51 1A 01 00 00   留样量时间3校准 最后两个字节是数值  对应g_CalibrationParams.retainSampleCalib.time3
// 收到  5A A5 06 83 51 1B 01 00 00   留样量真实值3校准 最后两个字节是数值  对应g_CalibrationParams.retainSampleCalib.realValue3
// 收到  5A A5 06 83 51 1C 01 00 00   加酸量时间1校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.time1
// 收到  5A A5 06 83 51 1D 01 00 00   加酸量真实值1校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.realValue1
// 收到  5A A5 06 83 51 1E 01 00 00   加酸量时间2校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.time2
// 收到  5A A5 06 83 51 1F 01 00 00   加酸量真实值2校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.realValue2
// 收到  5A A5 06 83 51 20 01 00 00   加酸量时间3校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.time3
// 收到  5A A5 06 83 51 21 01 00 00   加酸量真实值3校准 最后两个字节是数值  对应g_CalibrationParams.acidAdditionCalib.realValue3
// 收到  5A A5 06 83 51 22 01 00 00   温度校准输入AD 最后两个字节是数值      对应g_CalibrationParams.tempCalib.inputAD
// 收到  5A A5 06 83 51 23 01 00 00   温度校准0点AD 最后两个字节是数值      对应g_CalibrationParams.tempCalib.zeroPointAD
// 收到  5A A5 06 83 51 24 01 00 00   温度校准校准AD 最后两个字节是数值      对应g_CalibrationParams.tempCalib.calibAD
// 收到  5A A5 06 83 51 25 01 00 00   温度校准校准值 最后两个字节是数值 小数/100计算  对应g_CalibrationParams.tempCalib.calibValue
// 收到  5A A5 06 83 51 27 01 00 00   温度设置设置温度 最后两个字节是数值      对应g_CalibrationParams.tempCalib.setTemp
// 收到  5A A5 06 83 51 28 01 00 00   温度设置上偏差 最后两个字节是数值      对应g_CalibrationParams.tempCalib.upperDev
// 收到  5A A5 06 83 51 29 01 00 00   温度设置下偏差 最后两个字节是数值      对应g_CalibrationParams.tempCalib.lowerDev
// 收到  5A A5 06 83 51 26 01 00 00   温度设置0点温度 最后两个字节是数值 小数/100计算  对应g_CalibrationParams.tempCalib.zeroTemp

// 通讯设置处理函数 (0xB0-0xBB)
// 收到  5A A5 06 83 50 B0 01 00 03   变量地址50B0 最后两个字节是数值  通讯协议  对应g_CommSettingConfig.Protocol
// 收到  5A A5 06 83 50 B1 01 00 03   变量地址50B1 最后两个字节是数值  设备地址  对应g_CommSettingConfig.DeviceAddr
// 收到  5A A5 06 83 50 B2 01 00 00   变量地址50B2 最后两个字节是数值  协议选择(0=大岳,1=大湖,2=四川管控) 对应g_CommSettingConfig.AutoCalibration
// 收到  5A A5 06 83 50 B3 01 00 03   变量地址50B3 已废弃（原用于显示流量）
// 收到  5A A5 06 83 50 B4 01 00 03   变量地址50B4 已废弃（流量校准电流改用50A0）
// 收到  5A A5 06 83 50 B5 01 00 03   变量地址50B5 已废弃（流量校准流量改用50A1）
// 收到  5A A5 06 83 50 B6 01 00 03   变量地址50B6 最后两个字节是数值  系统时间-年 对应g_SystemSettingConfig.Year
// 收到  5A A5 06 83 50 B7 01 00 03   变量地址50B7 最后两个字节是数值  系统时间-月 对应g_SystemSettingConfig.Month
// 收到  5A A5 06 83 50 B8 01 00 03   变量地址50B8 最后两个字节是数值  系统时间-日 对应g_SystemSettingConfig.Day
// 收到  5A A5 06 83 50 B9 01 00 03   变量地址50B9 最后两个字节是数值  系统时间-时 对应g_SystemSettingConfig.Hour
// 收到  5A A5 06 83 50 BA 01 00 03   变量地址50BA 最后两个字节是数值  系统时间-分 对应g_SystemSettingConfig.Minute
// 收到  5A A5 06 83 50 BB 01 00 03   变量地址50BB 最后两个字节是数值  系统时间-秒 对应g_SystemSettingConfig.Second
void handle_comm_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint16_t value = (msg->data[7] << 8) | msg->data[8];
    printf("[通讯设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

    uint8_t changed_comm = 0;
    uint8_t changed_sys = 0;
    switch (sub_cmd)
    {
    case 0xB0:
    {   // 协议
        uint8_t old = g_CommSettingConfig.Protocol;
        uint8_t v = msg->data[8];
        if (v != old)
        {
            g_CommSettingConfig.Protocol = v;
            log_cfg_u8(0xB0u, old, v);
            changed_comm = 1;
        }
        break;
    }
    case 0xB1:
    {   // 设备地址
        uint8_t old = g_CommSettingConfig.DeviceAddr;
        uint8_t v = msg->data[8];
        if (v != old)
        {
            g_CommSettingConfig.DeviceAddr = v;
            log_cfg_u8(0xB1u, old, v);
            changed_comm = 1;
        }
        break;
    }
    case 0xB2:
    {   // 协议选择
        uint8_t old = g_CommSettingConfig.AutoCalibration;
        uint8_t v = msg->data[8];
        if (v > COMM_PROTOCOL_SICHUAN)
        {
            printf("[通讯设置] 协议选择无效: %u\r\n", v);
            break;
        }
        if (v != old)
        {
            g_CommSettingConfig.AutoCalibration = v;
            log_cfg_u8(0xB2u, old, v);
            changed_comm = 1;
        }
        break;
    }
    // case 0xB3 已删除：50B3地址仅用于显示流量值，不接收数据
    // 50B4/50B5 已废弃：流量校准改用通道六地址 50A0/50A1
    case 0xB4:
    case 0xB5:
    {
        break;
    }
    default:
        break;
    }
    if (changed_comm)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_COMM);
        printf("[KVDB缓存] 通信设置已标记\r\n");
    }
    if (changed_sys)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SYSTEM);
        printf("[KVDB缓存] 系统设置已标记\r\n");
    }
}

// 门禁设置处理函数 (0xBF-0xD4)
// 收到  5A A5 06 83 50 BF 01 00 03   变量地址50BF 最后两个字节是数值  是否自动运行 对应g_SystemSettingConfig.AutoRunMode
// 收到  5A A5 06 83 50 D4 01 00 03   变量地址50D4 最后两个字节是数值  是否水站模式 对应g_SystemSettingConfig.WaterStationMode
// 收到  5a    a5    8    83    50    ca    2    1    62    88    9    变量地址50C0 最后4个字节是数值  卡1设置   //卡号是23234569，对应g_SystemSettingConfig.CardId[0]
// 收到  5a    a5    8    83    50    c0    2    0    55    ec    92   变量地址50C2 最后4个字节是数值  卡2设置 //卡号是5631122，对应g_SystemSettingConfig.CardId[1]

/**
 * @brief 处理ID卡号更新的独立函数
 * @param msg 接收到的UART消息
 * @return 1=有更新；0=无更新
 */
uint8_t handle_card_id_update(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];
    uint8_t changed = 0;

    // 解析32位卡ID：msg->data[7-10] = [B3 B2 B1 B0]
    uint32_t card_id = ((uint32_t)msg->data[7] << 24) |
                       ((uint32_t)msg->data[8] << 16) |
                       ((uint32_t)msg->data[9] << 8) |
                       ((uint32_t)msg->data[10]);

    printf("[卡号设置] 子命令=0x%02X, 卡ID=%u (0x%08X)\n",
           sub_cmd, card_id, card_id);

    // 根据地址映射到卡号索引
    uint8_t card_index = 0xFF; // 无效索引
    switch (sub_cmd)
    {
    case 0xCA:
        card_index = 0;
        break; // 卡1
    case 0xC0:
        card_index = 1;
        break; // 卡2
    case 0xCC:
        card_index = 2;
        break; // 卡3
    case 0xCE:
        card_index = 3;
        break; // 卡4
    case 0xD0:
        card_index = 4;
        break; // 卡5
    case 0xD2:
        card_index = 5;
        break; // 卡6
    case 0xC4:
        card_index = 6;
        break; // 卡7
    case 0xC6:
        card_index = 7;
        break; // 卡8
    case 0xC8:
        card_index = 8;
        break; // 卡9
    case 0xD6:
        card_index = 9;
        break; // 卡10
    default:
        printf("未知卡地址：0x%02X\n", sub_cmd);
        return 0;
    }

    // 更新卡ID
    if (card_index < 10)
    {
        uint32_t old = g_SystemSettingConfig.CardId[card_index];
        if (card_id != old)
        {
            g_SystemSettingConfig.CardId[card_index] = card_id;
            log_cfg_u32(sub_cmd, old, card_id);
            changed = 1;
            printf("卡[%u]已更新：%u -> %u\n", card_index, old, card_id);

            // 标记待保存到KVDB
            kvdb_cache_mark_dirty(KVDB_CACHE_SYSTEM);
            printf("[KVDB缓存] 卡号已标记\r\n");
        }
    }

    return changed;
}

/**
 * @brief 发送时间读取命令
 * 发送命令：5A A5 04 83 50 B6 06
 */
void send_time_read_command(void)
{
    uint8_t cmd[7] = {0x5A, 0xA5, 0x04, 0x83, 0x50, 0xB6, 0x06};
    vSendData(USART_SCREEN, cmd, 7);
    printf("[时间设置] 发送时间读取命令\n");
}

/**
 * @brief 发送定时送样时间读取命令
 * 发送命令：5A A5 04 83 20 00 19
 */
void send_fixed_delivery_read_command(void)
{
    uint8_t cmd[7] = {0x5A, 0xA5, 0x04, 0x83, 0x20, 0x00, 0x19};
    vSendData(USART_SCREEN, cmd, 7);
    printf("[定时送样] 发送读取命令\n");
}

/**
 * @brief 解析并缓存时间响应数据
 * 响应格式：5A A5 10 83 50 B6 06 00 19 00 0C 00 0E 00 15 00 2A 00 37 (共19字节)
 * 时间数据在偶数位置：buf[8]=年, buf[10]=月, buf[12]=日, buf[14]=时, buf[16]=分, buf[18]=秒
 * @param msg 接收到的UART消息
 */
void parse_and_cache_time_response(UartMessage *msg)
{
    // 检查是否是时间响应命令
    if (msg->len >= 19 &&
            msg->data[0] == 0x5A && msg->data[1] == 0xA5 &&
            msg->data[2] == 0x10 && msg->data[3] == 0x83 &&
            msg->data[4] == 0x50 && msg->data[5] == 0xB6 &&
            msg->data[6] == 0x06)
    {

        // 解析时间数据（时间值在偶数索引位置，每个值前面有个0x00）
        uint16_t year = msg->data[8] + 2000; // 年（0x19=25 + 2000 = 2025）
        uint8_t month = msg->data[10];       // 月（0x0C=12）
        uint8_t day = msg->data[12];         // 日（0x0E=14）
        uint8_t hour = msg->data[14];        // 时（0x15=21）
        uint8_t minute = msg->data[16];      // 分（0x2A=42）
        uint8_t second = msg->data[18];      // 秒（0x37=55）

        printf("[时间设置] 读取到新时间: %04u-%02u-%02u %02u:%02u:%02u\n",
               year, month, day, hour, minute, second);

        // 直接更新系统配置中的时间
        g_SystemSettingConfig.Year = year;
        g_SystemSettingConfig.Month = month;
        g_SystemSettingConfig.Day = day;
        g_SystemSettingConfig.Hour = hour;
        g_SystemSettingConfig.Minute = minute;
        g_SystemSettingConfig.Second = second;

        // 直接更新硬件RTC，不保存到KVDB
        update_hardware_rtc();
    }
}

/**
 * @brief 解析定时送样时间响应数据
 * 响应格式：5A A5 36 83 20 00 19 00 00 00 01 00 01 ... (共57字节)
 * buf[8]=0点, buf[10]=1点, ... buf[54]=23点 (1=启用, 0=未启用)
 * buf[56]=分钟数
 * @param msg 接收到的UART消息
 */
void parse_fixed_delivery_response(UartMessage *msg)
{
    // 检查响应格式：5A A5 36 83 20 00 19 ...
    if (msg->len >= 57 &&
        msg->data[0] == 0x5A && msg->data[1] == 0xA5 &&
        msg->data[2] == 0x36 && msg->data[3] == 0x83 &&
        msg->data[4] == 0x20 && msg->data[5] == 0x00)
    {
        printf("[定时送样] 开始解析响应数据\n");

        // 解析24小时启用状态，直接写入全局变量
        for (int i = 0; i < 24; i++)
        {
            g_DeliveryConfig.fixedhour[i] = msg->data[8 + i * 2];
        }
        // 解析分钟数
        g_DeliveryConfig.fixedmin = msg->data[56];

        // 打印启用的小时
        printf("[定时送样] 启用小时: ");
        for (int i = 0; i < 24; i++)
        {
            if (g_DeliveryConfig.fixedhour[i])
            {
                printf("%d ", i);
            }
        }
        printf("\n[定时送样] 分钟=%u\n", g_DeliveryConfig.fixedmin);

        // 保存到KVDB（延迟写入Flash）
        kvdb_cache_mark_dirty(KVDB_CACHE_DELIVERY);

        // 重新计算送样调度
        _recompute_delivery_schedule();

        // 跳转到主页
        uint8_t home_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x0B};
        vSendData(USART_SCREEN, home_buf, 10);

        printf("[定时送样] 设置完成\n");
    }
}

void handle_access_settings(UartMessage *msg)
{
    uint8_t sub_cmd = msg->data[5];

    // 检查消息长度：普通命令6字节，卡ID命令8字节
    uint8_t is_card_id = (msg->len >= 11); // 5A A5 08 83 50 XX 02 B3 B2 B1 B0

    uint8_t changed = 0;

    // 处理普通16位命令
    if (!is_card_id)
    {
        uint16_t value = (msg->data[7] << 8) | msg->data[8];
        printf("[门禁设置] 子命令=0x%02X, 值=%u\n", sub_cmd, value);

        switch (sub_cmd)
        {
        case 0xBF:
        {   // 是否自动运行
            uint8_t old = g_SystemSettingConfig.AutoRunMode;
            uint8_t v = (uint8_t)(value & 0xFF);
            if (v != old)
            {
                g_SystemSettingConfig.AutoRunMode = v;
                log_cfg_u8(0xBFu, old, v);
                changed = 1;

                // 联动修改系统运行状态
                if (v == 0)
                {
                    // 改为待机模式 → 停止系统
                    g_State.State = 0;
                }
                else
                {
                    // 改为自动模式 → 启动系统
                    g_State.State = 1;

                    // 重新初始化调度器（修复启动按钮问题）
                    extern void tp_scheduler_reinit_if_running(void);
                    tp_scheduler_reinit_if_running();
                    printf("[系统启动] 调度器已重新初始化\r\n");
                }
            }
            break;
        }
        case 0xD4:
        {   // 是否水站模式
            uint8_t old = g_SystemSettingConfig.WaterStationMode;
            uint8_t v = (uint8_t)(value & 0xFF);
            if (v != old)
            {
                g_SystemSettingConfig.WaterStationMode = v;
                log_cfg_u8(0xD4u, old, v);
                changed = 1;
            }
            break;
        }
        default:
            break;
        }
    }
    // 处理32位卡ID命令
    else
    {
        // 调用独立的ID卡号写入函数
        changed = handle_card_id_update(msg);
    }

    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SYSTEM);
        printf("[KVDB缓存] 系统设置已标记\r\n");
    }
}

/**
 * @brief 处理软件版本信息报文
 * @param msg 接收到的UART消息
 * @note 支持三种版本信息：
 *       - 50E0: 软件序列号
 *       - 50E8: 核心板版本
 *       - 54F0: 液晶屏版本
 */
void handle_software_version(UartMessage *msg)
{
    uint8_t addr_h = msg->data[4];
    uint8_t addr_l = msg->data[5];
    uint8_t char_count = msg->data[6];
    uint8_t changed = 0;

    // 软件序列号: 5A A5 xx 83 50 E0 nn ...
    if (addr_h == 0x50 && addr_l == 0xE0)
    {
        if (char_count > 0 && char_count <= 23)
        {
            char old_val[24];
            strncpy(old_val, g_SystemSettingConfig.SoftwareSerial, sizeof(old_val));

            memset(g_SystemSettingConfig.SoftwareSerial, 0, sizeof(g_SystemSettingConfig.SoftwareSerial));
            memcpy(g_SystemSettingConfig.SoftwareSerial, &msg->data[7], char_count);
            g_SystemSettingConfig.SoftwareSerial[char_count] = '\0';

            if (strcmp(old_val, g_SystemSettingConfig.SoftwareSerial) != 0)
            {
                printf("[软件版本] 序列号更新: %s -> %s\r\n", old_val, g_SystemSettingConfig.SoftwareSerial);
                changed = 1;
            }
        }
    }
    // 核心板版本: 5A A5 xx 83 50 E8 nn ...
    else if (addr_h == 0x50 && addr_l == 0xE8)
    {
        if (char_count > 0 && char_count <= 15)
        {
            char old_val[16];
            strncpy(old_val, g_SystemSettingConfig.SoftwareCoreVer, sizeof(old_val));

            memset(g_SystemSettingConfig.SoftwareCoreVer, 0, sizeof(g_SystemSettingConfig.SoftwareCoreVer));
            memcpy(g_SystemSettingConfig.SoftwareCoreVer, &msg->data[7], char_count);
            g_SystemSettingConfig.SoftwareCoreVer[char_count] = '\0';

            if (strcmp(old_val, g_SystemSettingConfig.SoftwareCoreVer) != 0)
            {
                printf("[软件版本] 核心板版本更新: %s -> %s\r\n", old_val, g_SystemSettingConfig.SoftwareCoreVer);
                changed = 1;
            }
        }
    }
    // 液晶屏版本: 5A A5 xx 83 54 F0 nn ...
    else if (addr_h == 0x54 && addr_l == 0xF0)
    {
        if (char_count > 0 && char_count <= 15)
        {
            char old_val[16];
            strncpy(old_val, g_SystemSettingConfig.SoftwareLcdVer, sizeof(old_val));

            memset(g_SystemSettingConfig.SoftwareLcdVer, 0, sizeof(g_SystemSettingConfig.SoftwareLcdVer));
            memcpy(g_SystemSettingConfig.SoftwareLcdVer, &msg->data[7], char_count);
            g_SystemSettingConfig.SoftwareLcdVer[char_count] = '\0';

            if (strcmp(old_val, g_SystemSettingConfig.SoftwareLcdVer) != 0)
            {
                printf("[软件版本] 液晶屏版本更新: %s -> %s\r\n", old_val, g_SystemSettingConfig.SoftwareLcdVer);
                changed = 1;
            }
        }
    }

    if (changed)
    {
        kvdb_cache_mark_dirty(KVDB_CACHE_SYSTEM);
        printf("[KVDB缓存] 系统设置(软件版本)已标记\r\n");
    }
}

void Screen_init(void)
{
//    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x0B}; // 管理员成功页面

    write_sampling_settings_page();
    vTaskDelay(10);
    write_delivery_settings_page();
    vTaskDelay(10);
    write_retain_settings_page();
    vTaskDelay(10);
    write_retain_channel_settings_page();
    vTaskDelay(10);
    write_retain_channel_data_page();
    vTaskDelay(10);
    write_retain_channel_current_page();
    vTaskDelay(10);
    write_retain_channel_channelCals_page();  // 写入通道1-5 + 通道6(流量)到508A,508E,5092,5096,509A,509E
    vTaskDelay(10);
    write_retain_channel_comm_page();
    vTaskDelay(10);
    write_retain_channel_AutoRunMode_page();
    vTaskDelay(10);
    write_retain_channel_WaterStationMode_page();
    vTaskDelay(10);
    write_retain_channel_calibration_page();
    vTaskDelay(10);
    write_software_version_page();
    vTaskDelay(10);
    // 电机使能
    // 检查当前开门和关门的状态，检查当前flash中保存的上次数据并载入,判断是否自动，自动需要判断时间进行

//    // 发送主界面屏幕的相应屏幕变量地址
//    vSendData(USART_SCREEN, Begin_buf, 10);
//    vTaskDelay(50);
//    //    vSendData(USART_SCREEN, Begin_buf, 10);
//    screen_send_notify(USART_SCREEN, Begin_buf, 10, 10);
}


void Screen_begin(void) {

    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x0B}; // 开始页
    vSendData(USART_SCREEN, Begin_buf, 10);
    vTaskDelay(50);
    screen_send_notify(USART_SCREEN, Begin_buf, 10, 10);

}

/**
 * @brief 独立发送时间更新到串口屏（不受页面状态影响）
 * 
 * 协议格式: 5A A5 09 82 52 20 [年月] [日时] [分秒]
 * 地址5220: 年(BCD)+月(BCD)  
 * 地址5221: 日(BCD)+时(BCD)
 * 地址5222: 分(BCD)+秒(BCD)
 */
void screen_send_time_update(void)
{
    update_global_state_time();  // 更新时间到g_State
    
    uint8_t buf[12] = {0x5A, 0xA5, 0x09, 0x82, 0x52, 0x20};
    buf[6] = (uint8_t)(((g_State.Time[0] / 10) << 4) | (g_State.Time[0] % 10)); // 年(BCD)
    buf[7] = (uint8_t)(((g_State.Time[1] / 10) << 4) | (g_State.Time[1] % 10)); // 月(BCD)
    buf[8] = (uint8_t)(((g_State.Time[2] / 10) << 4) | (g_State.Time[2] % 10)); // 日(BCD)
    buf[9] = (uint8_t)(((g_State.Time[3] / 10) << 4) | (g_State.Time[3] % 10)); // 时(BCD)
    buf[10] = (uint8_t)(((g_State.Time[4] / 10) << 4) | (g_State.Time[4] % 10)); // 分(BCD)
    buf[11] = (uint8_t)(((g_State.Time[5] / 10) << 4) | (g_State.Time[5] % 10)); // 秒(BCD)
    
    screen_send_notify(USART_SCREEN, buf, 12, 3);
}

void write_begin_page(void)
{
    uint8_t buf[128] = {0x5A, 0xA5, 0x7D, 0x82, 0x52, 0x00};

    // 更新时间信息到g_State
    update_global_state_time();

    buf[7] = g_State.SamplingMotor;                                              // 采样蠕动泵  变量5200 值0-2 停止/采样/反吹
    buf[9] = g_State.DeliveryMotor;                                              // 送样蠕动泵  变量5201 值0-2 停止/正转/反转
    buf[11] = g_State.InletThreeWayValve;                                        // 进水三通阀-A桶  变量5202 值1-A桶 2-B桶
    buf[13] = g_State.OutletThreeWayValve;                                       // 出水三通阀-关闭  变量5203 值0 A桶/1 B桶/2
    buf[15] = g_State.SampleThreeWayValve;                                       // 送样三通阀-开  变量5204 值0-1 开/关
    buf[17] = g_State.InstantThreeWayValve;                                      // 瞬时三通阀-直通  变量5205 值0-1 直通/瞬时
    buf[27] = g_State.DrainA;                                                    // A桶排水-关闭  变量520A 值0-1 停止/运行
    buf[29] = g_State.DrainB;                                                    // B桶排水-关闭  变量520B 值0-1 停止/运行
    buf[70] = (uint8_t)(((g_State.Time[0] / 10) << 4) | (g_State.Time[0] % 10)); // 地址5220 年(BCD)
    buf[71] = (uint8_t)(((g_State.Time[1] / 10) << 4) | (g_State.Time[1] % 10)); // 地址5220 月(BCD)
    buf[72] = (uint8_t)(((g_State.Time[2] / 10) << 4) | (g_State.Time[2] % 10)); // 地址5221 日(BCD)
    buf[73] = (uint8_t)(((g_State.Time[3] / 10) << 4) | (g_State.Time[3] % 10)); // 地址5221 时(BCD)
    buf[74] = (uint8_t)(((g_State.Time[4] / 10) << 4) | (g_State.Time[4] % 10)); // 地址5222 分(BCD)
    buf[75] = (uint8_t)(((g_State.Time[5] / 10) << 4) | (g_State.Time[5] % 10)); // 地址5222 秒(BCD)
    buf[77] = g_State.State;                                                     // 地址5223
    buf[81] = g_State.ExternalConnection;                                        // 外部连接 变量5225 值0-3 断开/ai模块/数采仪/在线
    buf[83] = g_State.CurrentBucket;                                             // 当前桶  变量5226 值0-1 当前A桶/B桶  只采样流程
    buf[85] = g_State.CurrentBucketRunState;                                     // 当前桶运行状态  变量 5227 值0-50
    buf[86] = g_State.CurrentBucketCountDown[0];                                 // 当前桶倒计时 变量 5228 值：输入编码字符串 计时过程
    buf[87] = g_State.CurrentBucketCountDown[1];                                 // 当前桶倒计时 变量 5228 值：输入编码字符串 计时过程
    buf[88] = g_State.CurrentBucketCountDown[2];                                 // 当前桶倒计时 变量 5228 值：输入编码字符串 计时过程
    buf[91] = g_State.ABucketState;                                              // A桶状态 变量522A 值0-50
    buf[92] = g_State.ABucketCountDown[0];                                       // A桶倒计时 变量522B 值：输入编码字符串 计时过程
    buf[93] = g_State.ABucketCountDown[1];                                       // A桶倒计时 变量522B 值：输入编码字符串 计时过程
    buf[94] = g_State.ABucketCountDown[2];                                       // A桶倒计时 变量522B 值：输入编码字符串 计时过程
    buf[96] = g_State.SaveWarterA >> 8;                                          // A桶存水量 变量522D 值uint16 多少ml  高8位
    buf[97] = g_State.SaveWarterA & 0xFF;                                        // A桶存水量 变量522D 值uint16 多少ml  低8位
    buf[99] = g_State.BBucketState;                                              // B桶状态 变量522E 值0-50
    buf[100] = g_State.BBucketCountDown[0];                                      // B桶倒计时 变量522F 值uint16 多少ml  高8位
    buf[101] = g_State.BBucketCountDown[1];                                      // B桶倒计时 变量522F 值uint16 多少ml  低8位
    buf[102] = g_State.BBucketCountDown[2];                                      // B桶倒计时 变量522F 值uint16 多少ml  低8位
    buf[104] = g_State.SaveWarterB >> 8;                                         // B桶存水量 变量5231 值uint16 多少ml  高8位
    buf[105] = g_State.SaveWarterB & 0xFF;                                       // B桶存水量 变量5231 值uint16 多少ml  低8位
    buf[107] = g_State.InstantOperationState;                                    // 瞬时操作状态 变量5232 值0-50
    buf[108] = g_State.InstantOperationStateCountDown[0];                        // 瞬时操作状态倒计时 变量5233  值：输入编码字符串 计时过程
    buf[109] = g_State.InstantOperationStateCountDown[1];                        // 瞬时操作状态倒计时 变量5233  值：输入编码字符串 计时过程
    buf[110] = g_State.InstantOperationStateCountDown[2];                        // 瞬时操作状态倒计时 变量5234  值：输入编码字符串 计时过程
    buf[114] = g_State.SamplingIntervalCountDown[0];                             // 采样间隔倒计时 变量5236  值：输入编码字符串 计时过程
    buf[115] = g_State.SamplingIntervalCountDown[1];                             // 采样间隔倒计时 变量5236  值：输入编码字符串 计时过程
    buf[116] = g_State.SamplingIntervalCountDown[2];                             // 采样间隔倒计时 变量5237  值：输入编码字符串 计时过程
    buf[118] = g_State.SamplingTotalTimeCountDown[0];                            // 采样总时倒计时 变量5238  值：输入编码字符串 计时过程
    buf[119] = g_State.SamplingTotalTimeCountDown[1];                            // 采样总时倒计时 变量5238  值：输入编码字符串 计时过程
    buf[120] = g_State.SamplingTotalTimeCountDown[2];                            // 采样总时倒计时 变量5239  值：输入编码字符串 计时过程
    buf[123] = g_State.SampleBottle1;                                            // 留样瓶1-已经留样  变量523A  值1-24
    buf[125] = g_State.SampleBottle2;                                            // 留样瓶2-准备留样  变量523B  值1-24
    buf[127] = g_State.SampleBottle3;                                            // 留样瓶3-空瓶  变量523C  值1-24
    vSendData(USART_SCREEN, buf, 128);
}

/*
人工测试

采样
5A A5 06 83 52 12 01 00 02 //选择B桶采样   写结构体  测试采样AB桶  uint8_t
5A A5 06 83 52 12 01 00 01 //选择A桶采样   写结构体  测试采样AB桶  uint8_t
5A A5 06 83 52 13 01 01 F4 //选择采样量  1F4=500ml  写结构体 测试采样量 uint16_t
5A A5 06 83 50 04 01 23 01 //按下开始
5A A5 06 83 00 00 01 23 01 //新页面开始 执行完成需要跳转页面    执行采样流程
5A A5 06 83 00 00 01 03 00 //新页面中断 执行排水同时跳转页面 5A A5 07 82 00 84 5A 01 00 6F

送样
5A A5 06 83 52 14 01 00 00 //选择瞬时送样   测试送样模式 瞬时  uint8_t
5A A5 06 83 52 14 01 00 01 //选择A桶送样    测试送样模式 A桶
5A A5 06 83 52 14 01 00 02 //选择B桶送样    测试送样模式 B桶
5A A5 06 83 52 15 01 01 F4 //选择送样量  1F4=500ml  写结构体 测试送样量  uint16_t
5A A5 06 83 50 04 01 21 01
5A A5 06 83 00 00 01 21 01 //新页面开始 执行完成需要跳转页面    执行送样流程（AB桶无水需要先采样）
5A A5 06 83 00 00 01 03 00 //新页面中断 不是瞬时样需要执行排水同时跳转页面 5A A5 07 82 00 84 5A 01 00 6F

手动留样
5A A5 06 83 52 16 01 00 00 //选择瞬时留样 测试留样模式 瞬时  uint8_t
5A A5 06 83 52 16 01 00 01 //选择A桶留样  测试留样模式 A桶
5A A5 06 83 52 16 01 00 02 //选择B桶留样  测试留样模式 B桶
5A A5 06 83 52 18 01 00 05 //选择留样瓶号  1-24  测试留样瓶号  uint8_t
5A A5 06 83 52 17 01 01 F4 //选择留样量 ml  测试留样量  uint16_t

5A A5 06 83 00 00 01 25 01 //确定开始

留样瓶控制
5A A5 06 83 50 04 01 14 01 //留样瓶复位
5A A5 06 83 00 00 01 14 01 //留样瓶复位确定
5A A5 06 83 52 19 01 00 05 //当前留样瓶选择 1-24 05=5号瓶
5A A5 06 83 52 1A 01 00 18 //选择排空瓶  1-24 0x18=24号瓶*/

/* ========== 测试采样函数实现 ========== */

/**
 * @brief 采样紧急停止函数
 * 用于紧急中止正在执行的手动采样/留样/送样过程
 * 停止所有电机、泵、阀门，恢复到安全状态
 */
void sampling_emergency_stop(void)
{
    printf("[紧急停止] ========================================\r\n");
    printf("[紧急停止] 手动采样/送样/留样已中止！\r\n");

    /* 1. 设置全局中止标志位，通知test_sampling_execute()等函数 */
    g_manual_operation_abort_flag = 1;
    printf("[紧急停止] 中止标志已设置（g_manual_operation_abort_flag = 1）\r\n");

    /* 2. 立即停止采样流程相关电机 */
    MotorStop(1); // 采样蠕动泵（主要）
    MotorStop(2); // 送样蠕动泵
    printf("[紧急停止] 所有蠕动泵已停止\r\n");

    /* 3. 立即停止采样流程相关泵 */
    ExternalPumpStop; // 外接泵（主要）
    MixAStop;         // A桶搅拌
    MixBStop;         // B桶搅拌
    DrainAStop;       // A桶排水
    DrainBStop;       // B桶排水
    printf("[紧急停止] 所有泵和搅拌器已停止\r\n");

    /* 4. 阀门恢复初始状态 */
    InletThreeWayValveB;        // 进水阀 -> B桶（初始状态）
    SampleThreeWayValveSTAY;    // 采样阀 -> 留样位置（初始状态）
    InstantThreeWayValveDirect; // 瞬时阀 -> 直通（初始状态）
    printf("[紧急停止] 所有阀门已重置为初始状态\r\n");
    printf("[紧急停止]   - 进水阀 -> B桶（初始状态）\r\n");
    printf("[紧急停止]   - 采样阀 -> 留样位置（初始状态）\r\n");
    printf("[紧急停止]   - 瞬时阀 -> 直通（初始状态）\r\n");

    /* 5. 记录TSDB紧急停止事件 */
    /* 使用TSDB缓存记录紧急停止（事件类型0x00EE） */
    {
        uint8_t emergency_data = 0xFF; // 紧急停止标记
        tsdb_cache_append(0x00EE, &emergency_data, sizeof(emergency_data));
        printf("[紧急停止] 紧急事件已记录到TSDB缓存（类型=0x00EE）\r\n");
    }

    /* 6. 跳转屏幕页面 */
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98}; // 管理员成功页面
    // 计算校验和（最后一个字节）
    //    screen_abort_cmd[8] = 0x6F;  // 根据协议预设的校验值
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("[紧急停止] 屏幕跳转命令已发送：5A A5 07 82 00 84 5A 01 00 6F\r\n");

    printf("[紧急停止] 系统已安全停止\r\n");
    printf("[紧急停止] ========================================\r\n");
}

/**
 * @brief 测试采样执行函数
 * 参考time_proportional_sampling流程，使用TestConfig_t结构体参数
 * 不更新全局状态，只记录TSDB（标记为测试状态）
 * @return 1=成功；0=失败
 */
uint8_t test_sampling_execute(void)
{
    /* ★ 诊断：函数入口检测 */
    printf("\r\n[测试采样] ========== 函数入口 ==========\r\n");
    printf("[测试采样] g_tmr2_seconds = %lu\r\n", g_tmr2_seconds);

#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
    UBaseType_t stack_remain = uxTaskGetStackHighWaterMark(NULL);
    printf("[测试采样] 入口处剩余栈空间：%u 字（%u 字节）\r\n",
           stack_remain, stack_remain * 4);
    if (stack_remain < 50)
    {
        printf("[测试采样] ?? 警告：栈空间几乎满！仅剩 %u 字！\r\n", stack_remain);
    }
#endif

    /* 清除紧急停止标志位 */
    g_manual_operation_abort_flag = 0;

    // 从测试配置获取参数
    uint8_t bucket_id = (g_SingleSampleTest.sampleBucket == 1) ? 0 : 1; // 1=A桶(0), 2=B桶(1)
    uint16_t sample_volume = g_SingleSampleTest.sampleVolume;

    // 使用默认配置参数（与正常采样流程相同）
    uint16_t blowback_time = _clamp_u16(g_SampleConfig.BlowbackTime, 0, 36000);
    uint16_t sampling_improve = _clamp_u16(g_SampleConfig.SamplingImproveTime, 0, 36000);
    uint16_t tube_hold_time = _clamp_u16(g_SampleConfig.TubeHoldTime, 0, 36000);
    uint16_t rpm = g_SystemSettingConfig.Motorspeed;

    if (sample_volume == 0)
    {
        _samp_tsdb(0xF0u, (uint8_t)(bucket_id == 0 ? 1 : 2), 0, 0); /* 测试状态：参数错误 */
        return 0;
    }

    uint16_t sampling_time = calc_sampling_time_by_volume(sample_volume);
    if (sampling_time == 0)
    {
        _samp_tsdb(0xF0u, (uint8_t)(bucket_id == 0 ? 1 : 2), sample_volume, 0); /* 测试状态：计算时间失败 */
        return 0;
    }

    /* 阶段0：测试开始事件 */
    _samp_tsdb(0xF1u, (uint8_t)(bucket_id == 0 ? 1 : 2), sample_volume, sampling_time);
    //    printf("测试采样开始  选择桶=%d\r\n",bucket_id);

    /* 选择采样阀门 */
    if (bucket_id == 0)
    {
        InletThreeWayValveA;
    }
    else
    {
        InletThreeWayValveB;
    }

    /* 阶段1：反吹 */
    uint32_t t0 = g_tmr2_seconds;
    _samp_tsdb(0xF2u, (uint8_t)(bucket_id == 0 ? 1 : 2), blowback_time, 0);
    printf("[测试采样] 阶段1开始：t0=%lu, 目标=%u 秒\r\n", t0, blowback_time);

    MotorRun(1, 0, rpm); // 每隔200ms送一次信号
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (g_manual_operation_abort_flag)
        {
            printf("[测试采样] 阶段1中被紧急停止中止\r\n");
            return 0;
        }
        MotorRun(1, 0, rpm);
        /* ★ 诊断：每1秒打印一次计时器状态 */
        static uint8_t debug_counter = 0;
        if (++debug_counter >= 5)
        {   // 每5次循环(1秒)打印一次
            debug_counter = 0;
            printf("[调试] 计时器：当前=%lu, 已用=%lu, 目标=%u\r\n", g_tmr2_seconds, (g_tmr2_seconds - t0), blowback_time);
        }
    }
    MotorStop(1);
    _samp_tsdb(0xF3u, (uint8_t)(bucket_id == 0 ? 1 : 2), (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试采样] 阶段1：预反吹完成（%lu 秒）\r\n", g_tmr2_seconds - t0);

    /* ★ 延时500ms：反转→正转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段2：外接泵提升 */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xF4u, (uint8_t)(bucket_id == 0 ? 1 : 2), sampling_improve, 0);
    //    printf("外接泵提升");

    ExternalPumpRun;
    while ((g_tmr2_seconds - t0) < sampling_improve)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (g_manual_operation_abort_flag)
        {
            printf("[测试采样] 阶段2中被紧急停止中止\r\n");
            return 0;
        }
    }
    printf("[测试采样] 阶段2：外接泵完成（%lu 秒）\r\n", g_tmr2_seconds - t0);

    /* 阶段3：管存（正转） */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xF5u, (uint8_t)(bucket_id == 0 ? 1 : 2), tube_hold_time, 0);

    MotorRun(1, 1, rpm);
    while ((g_tmr2_seconds - t0) < tube_hold_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (g_manual_operation_abort_flag)
        {
            printf("[测试采样] 阶段3中被紧急停止中止\r\n");
            return 0;
        }
        MotorRun(1, 1, rpm);
    }
    MotorStop(1);
    _samp_tsdb(0xF6u, (uint8_t)(bucket_id == 0 ? 1 : 2), (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试采样] 阶段3：管存完成（%lu 秒）\r\n", g_tmr2_seconds - t0);

    /* ★ 延时200ms：同向重启保护 */
    vTaskDelay(pdMS_TO_TICKS(200));

    /* 阶段4：计量采样（正转） */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xF7u, (uint8_t)(bucket_id == 0 ? 1 : 2), sampling_time, sample_volume);

    MotorRun(1, 1, rpm);
    while ((g_tmr2_seconds - t0) < sampling_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (g_manual_operation_abort_flag)
        {
            printf("[测试采样] 阶段4中被紧急停止中止\r\n");
            return 0;
        }
        MotorRun(1, 1, rpm);
    }
    MotorStop(1);
    _samp_tsdb(0xF8u, (uint8_t)(bucket_id == 0 ? 1 : 2), (uint16_t)(g_tmr2_seconds - t0), sample_volume);
    printf("[测试采样] 阶段4：计量完成（%lu 秒）\r\n", g_tmr2_seconds - t0);
    printf("采样\r\n");

    /* 阶段5：关闭外接泵 */
    ExternalPumpStop;
    printf("[测试采样] 阶段5：外接泵已停止\r\n");

    /* ★ 延时500ms：正转→反转方向切换保护（最关键！） */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段6：反抽 */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xF9u, (uint8_t)(bucket_id == 0 ? 1 : 2), blowback_time, 0);

    MotorRun(1, 0, rpm);
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (g_manual_operation_abort_flag)
        {
            printf("[测试采样] 阶段6中被紧急停止中止\r\n");
            return 0;
        }
        MotorRun(1, 0, rpm);
    }
    MotorStop(1);
    _samp_tsdb(0xFAu, (uint8_t)(bucket_id == 0 ? 1 : 2), (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试采样] 阶段6：后反吹完成（%lu 秒）\r\n", g_tmr2_seconds - t0);
    printf("反吹\r\n");

    /* ★ 阀位保持（测试模式不切换，便于后续送样） */
    // 在测试模式下，保持进水阀在当前采样的桶，不切换到另一个桶
    // 这样如果后续有送样操作，阀门状态保持一致
    printf("[测试采样] 阀门：进水阀保持在%d桶\r\n", (bucket_id == 0 ? 1 : 2));

    /* 测试完成事件 */
    _samp_tsdb(0xFEu, (uint8_t)(bucket_id == 0 ? 1 : 2), 1, 0);
    printf("[测试采样] 测试采样成功完成\r\n");

    /* ★ 更新桶内水量 */
    if (bucket_id == 0)
    {
        g_State.SaveWarterA += sample_volume;
        printf("[测试采样] A桶水量已更新：%d ml（增加 %d ml）\r\n",
               g_State.SaveWarterA, sample_volume);
    }
    else
    {
        g_State.SaveWarterB += sample_volume;
        printf("[测试采样] B桶水量已更新：%d ml（增加 %d ml）\r\n",
               g_State.SaveWarterB, sample_volume);
    }

    /* 6. 跳转屏幕页面 跳转屏幕页面*/
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98}; // 管理员成功页面
    // 计算校验和（最后一个字节）
    //    screen_abort_cmd[8] = 0x6F;  // 根据协议预设的校验值
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("手动采样完成\r\n");
    return 1;
}

/**
 * @brief AB桶送样测试执行函数
 * 从AB桶送样到分析设备
 * 不更新全局状态，只记录TSDB（标记为测试状态）
 * @return 1=成功；0=失败
 */
uint8_t test_delivery_execute(void)
{
    /* 清除紧急停止标志位 */
    g_manual_operation_abort_flag = 0;

    // 从测试配置获取参数
    uint8_t delivery_mode = g_SingleSampleTest.deliveryMode; // 0=瞬时, 1=A桶, 2=B桶
    uint16_t delivery_volume = g_SingleSampleTest.deliveryVolume;

    // AB桶送样
    uint8_t bucket_id = (delivery_mode == 1) ? 0 : 1; // 1=A桶(0), 2=B桶(1)
    uint8_t bucket12 = (uint8_t)(bucket_id == 0 ? 1 : 2);

    // 检查当前桶的存水量
    uint16_t current_water = (bucket_id == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;

    /* 水量不足，需要先采样 */
    if (current_water < delivery_volume)
    {
        _delivery_tsdb(0xE1u, bucket12, current_water, delivery_volume); /* 测试状态：水量不足 */
        printf("[测试送样]水量不足 (%d < %d), 调用test_sampling_execute()...\r\n", current_water, delivery_volume);

        /* ★ 诊断：检查任务栈使用情况 */
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
        UBaseType_t stack_remain = uxTaskGetStackHighWaterMark(NULL);
        printf("[测试送样] 采样前剩余栈空间: %u 字 (%u 字节)\r\n",
               stack_remain, stack_remain * 4);
#endif

        // 设置采样参数并执行采样
        uint8_t original_bucket = g_SingleSampleTest.sampleBucket;
        uint16_t original_volume = g_SingleSampleTest.sampleVolume;

        g_SingleSampleTest.sampleBucket = delivery_mode;         // 使用相同的桶
        g_SingleSampleTest.sampleVolume = delivery_volume + 100; // 采样量稍大于送样量

        printf("[测试送样] 正在调用嵌套test_sampling_execute()...\r\n");
        printf("[测试送样] ============== 进入嵌套采样 ==============\r\n");

        // 执行测试采样
        uint8_t sampling_result = test_sampling_execute();

        printf("[测试送样] ============== 退出嵌套采样 ==============\r\n");
        printf("[测试送样] 嵌套采样返回: %d\r\n", sampling_result);

        /* ★ 诊断：检查任务栈使用情况 */
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
        stack_remain = uxTaskGetStackHighWaterMark(NULL);
        printf("[测试送样] 采样后剩余栈空间: %u 字 (%u 字节)\r\n",
               stack_remain, stack_remain * 4);
#endif

        // 恢复原始采样配置
        g_SingleSampleTest.sampleBucket = original_bucket;
        g_SingleSampleTest.sampleVolume = original_volume;

        if (!sampling_result)
        {
            _delivery_tsdb(0xEFu, bucket12, 1, 0); /* 测试状态：采样失败 */
            return 0;
        }

        _delivery_tsdb(0xE2u, bucket12, 1, 0); /* 测试状态：采样完成 */
    }

    // 使用配置参数（与正常送样流程相同）
    uint16_t blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000);
    uint16_t backdraw_time = _clamp_u16(g_DeliveryConfig.Interval, 0, 36000);
    // 根据送样体积计算送样时间，使用校准参数
    uint16_t deliver_time = calc_delivery_time_by_volume(delivery_volume);
    if (deliver_time == 0)
    {
        _delivery_tsdb(0xE0u, bucket12, delivery_volume, 0); /* 测试状态：计算时间失败 */
        return 0;
    }
    uint16_t rpmR1 = g_SystemSettingConfig.Motorspeed;

    /* 测试送样开始事件 */
    _delivery_tsdb(0xE3u, bucket12, deliver_time, delivery_volume);

    /* 选择出水桶、接通送样路径 */
    if (bucket_id == 0)
    {
        OutletThreeWayValveA();
    }
    else
    {
        OutletThreeWayValveB();
    }
    SampleThreeWayValveSample;

    /* 阶段1：反吹（清线） */
    uint32_t t0 = g_tmr2_seconds;
    _delivery_tsdb(0xE4u, bucket12, blowback_time, 0);

    MotorRun(2, 0, rpmR1);
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpmR1);
    }
    MotorStop(2);
    _delivery_tsdb(0xE5u, bucket12, (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试送样] 阶段1: 反吹完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* 阶段2：稳定等待 1s */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 阶段3：混样电机启动 */
    if (bucket_id == 0)
    {
        MixARun;
    }
    else
    {
        MixBRun;
    }

    /* 阶段4：计量送样 */
    t0 = g_tmr2_seconds;
    _delivery_tsdb(0xE6u, bucket12, deliver_time, 0);

    MotorRun(2, 1, rpmR1);
    while ((g_tmr2_seconds - t0) < deliver_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 1, rpmR1);
    }
    MotorStop(2);
    _delivery_tsdb(0xE7u, bucket12, (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试送样] 阶段4: 送样完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* ★ 延时500ms：正转→反转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段5：回抽 */
    t0 = g_tmr2_seconds;
    _delivery_tsdb(0xE8u, bucket12, backdraw_time, 0);

    MotorRun(2, 0, rpmR1);
    while ((g_tmr2_seconds - t0) < backdraw_time)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        MotorRun(2, 0, rpmR1);
    }
    MotorStop(2);
    _delivery_tsdb(0xE9u, bucket12, (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试送样] 阶段5: 回抽完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* 阶段6：停止混样电机 */
    if (bucket_id == 0)
    {
        MixAStop;
    }
    else
    {
        MixBStop;
    }

    /* ★ 扣除桶内水量 */
    uint16_t water_before = (bucket_id == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;
    if (bucket_id == 0)
    {
        g_State.SaveWarterA = (g_State.SaveWarterA > delivery_volume)
                              ? (g_State.SaveWarterA - delivery_volume)
                              : 0;
        printf("[测试送样] A桶水量已更新: %d ml (原 %d ml, 送出 %d ml)\r\n",
               g_State.SaveWarterA, water_before, delivery_volume);
    }
    else
    {
        g_State.SaveWarterB = (g_State.SaveWarterB > delivery_volume)
                              ? (g_State.SaveWarterB - delivery_volume)
                              : 0;
        printf("[测试送样] B桶水量已更新: %d ml (原 %d ml, 送出 %d ml)\r\n",
               g_State.SaveWarterB, water_before, delivery_volume);
    }

    /* 测试送样完成事件 - 记录水量变化 */
    uint16_t water_after = (bucket_id == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;
    _delivery_tsdb(0xEEu, bucket12, water_after, delivery_volume); // vol=剩余水量, time=送出水量

    /* 跳转屏幕页面 */
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98};
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("[测试送样] 测试送样成功完成\r\n");

    return 1;
}

/**
 * @brief 瞬时送样测试执行函数
 * 水样不经过AB桶，直接从进水口送到分析设备
 * 不更新全局状态，只记录TSDB（标记为测试状态）
 * @return 1=成功；0=失败
 */
uint8_t test_instant_delivery_execute(void)
{
    /* 清除紧急停止标志位 */
    g_manual_operation_abort_flag = 0;

    // 从测试配置获取参数
    uint16_t delivery_volume = g_SingleSampleTest.deliveryVolume;
    if (delivery_volume == 0)
    {
        _delivery_tsdb(0xD0u, 0, 0, 0); /* 测试状态：参数错误 */
        return 0;
    }

    // 使用配置参数（与正常送样流程相同）
    uint16_t blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000);
    // 根据送样体积计算送样时间，使用校准参数
    uint16_t delivery_time = calc_delivery_time_by_volume(delivery_volume);
    if (delivery_time == 0)
    {
        _delivery_tsdb(0xD0u, 0, delivery_volume, 0); /* 测试状态：计算时间失败 */
        return 0;
    }
    uint16_t rpm = g_SystemSettingConfig.Motorspeed;

    /* 瞬时送样开始事件 */
    _delivery_tsdb(0xD1u, 0, delivery_volume, delivery_time);

    /* 阶段1：阀门设置 - 瞬时送样路径 */
    OutletThreeWayValveClose();  // 出水阀关闭（不使用AB桶）
    InstantThreeWayValveInstant; // 瞬时模式
    SampleThreeWayValveSample;   // 送样路径

    /* 阶段2：反吹清线 */
    uint32_t t0 = g_tmr2_seconds;
    _delivery_tsdb(0xD2u, 0, blowback_time, 0);

    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    _delivery_tsdb(0xD3u, 0, (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试瞬时送样] 阶段2: 反吹完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* ★ 延时500ms：反转→正转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段3：瞬时送样（只使用送样电机） */
    t0 = g_tmr2_seconds;
    _delivery_tsdb(0xD4u, 0, delivery_time, delivery_volume);

    MotorRun(2, 1, rpm); // 送样电机正转
    while ((g_tmr2_seconds - t0) < delivery_time)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        MotorRun(2, 1, rpm);
    }
    MotorStop(2);
    _delivery_tsdb(0xD5u, 0, (uint16_t)(g_tmr2_seconds - t0), delivery_volume);
    printf("[测试瞬时送样] 阶段3: 瞬时送样完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* ★ 延时500ms：正转→反转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段4：反抽清理（只使用送样电机） */
    t0 = g_tmr2_seconds;
    _delivery_tsdb(0xD6u, 0, blowback_time, 0);

    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    _delivery_tsdb(0xD7u, 0, (uint16_t)(g_tmr2_seconds - t0), 0);
    printf("[测试瞬时送样] 阶段4: 回抽完成 (%lu 秒)\r\n", g_tmr2_seconds - t0);

    /* 阶段5：复位阀门 */
    InstantThreeWayValveDirect; // 恢复直通模式
    printf("[测试瞬时送样] 阶段5: 阀门已重置为初始状态\r\n");

    /* 瞬时送样完成事件 */
    _delivery_tsdb(0xDEu, 0, 1, 0);

    /* 跳转屏幕页面 */
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98};
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("[测试瞬时送样] 测试瞬时送样成功完成\r\n");

    return 1;
}
/**
 * @brief 瞬时留样测试执行函数
 * 水样不经过AB桶，直接从进水口留样到指定留样瓶
 * 不更新全局状态，只记录TSDB（标记为测试状态）
 * @return 1=成功；0=失败
 */
uint8_t test_instant_retention_execute(void)
{
    /* 清除紧急停止标志位 */
    g_manual_operation_abort_flag = 0;

    // 从测试配置获取参数
    uint16_t retention_volume = g_SingleSampleTest.retainVolume;
    uint8_t bottle_number = g_SingleSampleTest.bottleNumber;

    if (retention_volume == 0 || bottle_number < 1 || bottle_number > 24)
    {
        _samp_tsdb(0xC0u, 0, retention_volume, bottle_number); /* 测试状态：参数错误 */
        return 0;
    }

    // 使用配置参数（与正常留样流程相同）
    uint16_t blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000); // 反吹时间
    // 根据留样体积计算留样时间，使用校准参数
    uint16_t retention_time = calc_retain_time_by_volume(retention_volume);
    if (retention_time == 0)
    {
        _samp_tsdb(0xC0u, bottle_number, retention_volume, 0); /* 测试状态：计算时间失败 */
        return 0;
    }
    uint16_t rpm = g_SystemSettingConfig.Motorspeed;

    /* 瞬时留样开始事件 */
    _samp_tsdb(0xC1u, bottle_number, retention_volume, retention_time);

    /* 阶段1：留样瓶定位 */
    _samp_tsdb(0xC2u, bottle_number, 0, 0);
    bottle_move_to(bottle_number, 40, 50000);
    _samp_tsdb(0xC3u, bottle_number, 1, 0); /* 定位完成 */

    /* 阶段2：阀门设置 - 瞬时留样路径 */
    OutletThreeWayValveClose();  // 出水阀关闭（不使用AB桶）
    InstantThreeWayValveInstant; // 瞬时模式
    SampleThreeWayValveSTAY;     // 留样路径

    /* 阶段3：反吹清线 */
    uint32_t t0 = g_tmr2_seconds;
    _samp_tsdb(0xC4u, bottle_number, blowback_time, 0);

    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    _samp_tsdb(0xC5u, bottle_number, (uint16_t)(g_tmr2_seconds - t0), 0);

    /* ★ 延时500ms：反转→正转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段4：瞬时留样（只使用送样电机） */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xC6u, bottle_number, retention_time, retention_volume);

    MotorRun(2, 1, rpm); // 送样电机正转
    while ((g_tmr2_seconds - t0) < retention_time)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        MotorRun(2, 1, rpm);
    }
    MotorStop(2);
    _samp_tsdb(0xC7u, bottle_number, (uint16_t)(g_tmr2_seconds - t0), retention_volume);

    /* ★ 延时500ms：正转→反转方向切换保护 */
    vTaskDelay(pdMS_TO_TICKS(500));

    /* 阶段5：反抽清理（只使用送样电机） */
    t0 = g_tmr2_seconds;
    _samp_tsdb(0xC8u, bottle_number, blowback_time, 0);

    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    _samp_tsdb(0xC9u, bottle_number, (uint16_t)(g_tmr2_seconds - t0), 0);

    /* 阶段6：复位阀门 */
    InstantThreeWayValveDirect; // 恢复直通模式
    SampleThreeWayValveSample;  // 恢复送样模式

    /* 瞬时留样完成事件 */
    _samp_tsdb(0xCEu, bottle_number, 1, 0);

    /* 跳转屏幕页面 */
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98};
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("[测试瞬时留样] 测试瞬时留样成功完成\r\n");

    return 1;
}

/**
 * @brief AB桶留样测试执行函数
 * 从AB桶中的水样留样到指定留样瓶，参考time_proportional_retention流程
 * 不更新全局状态，只记录TSDB（标记为测试状态）
 * @return 1=成功；0=失败
 */
uint8_t test_retention_execute(void)
{
    /* 清除紧急停止标志位 */
    g_manual_operation_abort_flag = 0;

    // 从测试配置获取参数
    uint8_t retention_mode = g_SingleSampleTest.retainMode; // 0=瞬时, 1=A桶, 2=B桶
    uint16_t retention_volume = g_SingleSampleTest.retainVolume;
    uint8_t bottle_number = g_SingleSampleTest.bottleNumber;

    // AB桶留样不支持瞬时模式（mode=0）
    if (retention_volume == 0 || bottle_number < 1 || bottle_number > 24 || retention_mode == 0 || retention_mode > 2)
    {
        _retain_tsdb(0xB0u, 0, retention_volume, bottle_number); /* 测试状态：参数错误 */
        return 0;
    }

    // 转换为bucket_id：1=A桶(0), 2=B桶(1)
    uint8_t bucket_id = (retention_mode == 1) ? 0 : 1;
    uint8_t bucket12 = (uint8_t)(bucket_id == 0 ? 1 : 2);

    // 使用配置参数（与正常留样流程相同）
    uint16_t blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000);
    uint16_t tube_hold_time = _clamp_u16(g_RetainSampleConfig.TubeHoldTime, 0, 36000);
    uint16_t backdraw_time = _clamp_u16(g_RetainSampleConfig.BackdrawTime, 0, 36000);
    uint16_t rpmR1 = g_SystemSettingConfig.Motorspeed;

    // 根据留样体积计算留样时间，使用校准参数
    uint16_t retain_time = calc_retain_time_by_volume(retention_volume);
    if (retain_time == 0)
    {
        _retain_tsdb(0xB0u, bucket12, retention_volume, 0); /* 测试状态：计算时间失败 */
        return 0;
    }

    /* 测试留样开始事件 */
    _retain_tsdb(0xB1u, bucket12, retention_volume, bottle_number);

    /* 阶段1：选择出水桶 */
    if (bucket_id == 0)
    {
        OutletThreeWayValveA();
    }
    else
    {
        OutletThreeWayValveB();
    }

    /* 阶段2：留样瓶定位 */
    _retain_tsdb(0xB2u, bucket12, 0, bottle_number);
    bottle_move_to(bottle_number, 40, 50000);
    _retain_tsdb(0xB3u, bucket12, 1, bottle_number); /* 定位完成 */

    /* 阶段3：留样三通阀切瓶 */
    SampleThreeWayValveSTAY;

    /* 阶段4：反吹清线 */
    uint32_t t0 = g_tmr2_seconds;
    _retain_tsdb(0xB4u, bucket12, blowback_time, bottle_number);

    MotorRun(2, 0, rpmR1); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpmR1);
    }
    MotorStop(2);
    _retain_tsdb(0xB5u, bucket12, (uint16_t)(g_tmr2_seconds - t0), bottle_number);

    /* 阶段5：启动混样电机（搅拌桶） */
    if (bucket_id == 0)
    {
        MixARun;
    }
    else
    {
        MixBRun;
    }

    /* 阶段6：管存阶段 */
    t0 = g_tmr2_seconds;
    _retain_tsdb(0xB6u, bucket12, tube_hold_time, bottle_number);

    MotorRun(2, 1, rpmR1); // 送样电机正转，填充管路
    while ((g_tmr2_seconds - t0) < tube_hold_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 1, rpmR1);
    }
    _retain_tsdb(0xB7u, bucket12, (uint16_t)(g_tmr2_seconds - t0), bottle_number);

    /* 阶段7：计量留样 */
    t0 = g_tmr2_seconds;
    _retain_tsdb(0xB8u, bucket12, retain_time, bottle_number);

    MotorRun(2, 1, rpmR1); // 送样电机正转，计量留样
    while ((g_tmr2_seconds - t0) < retain_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 1, rpmR1);
    }
    _retain_tsdb(0xB9u, bucket12, (uint16_t)(g_tmr2_seconds - t0), bottle_number);

    /* 阶段8：反抽清理 */
    t0 = g_tmr2_seconds;
    _retain_tsdb(0xBAu, bucket12, backdraw_time, bottle_number);

    MotorRun(2, 0, rpmR1); // 送样电机反转
    while ((g_tmr2_seconds - t0) < backdraw_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpmR1);
    }
    MotorStop(2);
    _retain_tsdb(0xBBu, bucket12, (uint16_t)(g_tmr2_seconds - t0), bottle_number);

    /* 阶段9：停止混样电机 */
    if (bucket_id == 0)
    {
        MixAStop;
    }
    else
    {
        MixBStop;
    }

    /* 阶段10：阀位复位到送样路 */
    SampleThreeWayValveSample;

    /* 测试留样完成事件 */
    _retain_tsdb(0xBEu, bucket12, retention_volume, bottle_number);

    /* 跳转屏幕页面 */
    uint8_t Begin_buf[10] = {0x5A, 0xA5, 0x07, 0x82, 0x00, 0x84, 0x5A, 0x01, 0x00, 0x98};
    vSendData(USART_SCREEN, Begin_buf, 10);
    printf("[测试留样] 测试留样成功完成\r\n");

    return 1;
}

/* ==================== 屏幕分发器控制函数 ==================== */

/**
 * @brief 停止屏幕消息分发器
 */
void screen_dispatcher_stop(void)
{
    s_screen_dispatcher_stopped = 1;
}

/**
 * @brief 恢复屏幕消息分发器
 */
void screen_dispatcher_resume(void)
{
    s_screen_dispatcher_stopped = 0;
}

/**
 * @brief 查询屏幕分发器是否停止
 * @return 1=已停止, 0=运行中
 */
uint8_t screen_dispatcher_is_stopped(void)
{
    return s_screen_dispatcher_stopped;
}

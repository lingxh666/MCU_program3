#ifndef SAMPLING_TIME_H
#define SAMPLING_TIME_H

#include <stdint.h>
#include "at32f403a_407.h"
#include "rtc.h"
#include "app_flashdb.h"
#include "sampling.h"

// 前向声明配置结构体（避免循环包含）
typedef struct
{
    uint8_t StartHour;
    uint8_t StartMin;
    uint16_t Duration;
} DeliveryConfig;

// 前向声明外部定义的结构体（定义在freertos_app.h中）
typedef struct SampleConfig_tag SampleConfig;

// 外部变量声明
extern calendar_type calendar;
extern volatile uint32_t g_tmr2_seconds;
extern volatile uint32_t g_tmr3_milliseconds;
extern volatile uint8_t g_manual_operation_abort_flag;

//==============================================================================
// 时间等比调度器数据结构定义
//==============================================================================

/* 时间点结构体 */
typedef struct
{
    uint8_t hour;
    uint8_t minute;
    uint8_t bucket_id; // 0=A桶 1=B桶 0xFF=未指定
} TpTimePoint;

/* 操作时段结构体 */
typedef struct
{
    TpTimePoint sample_time;
    TpTimePoint delivery_time;
    int16_t delay_offset_min;
    uint8_t is_valid;
} TpOperationSlot;

/* 全天时间表结构体 */
typedef struct
{
    uint8_t cycle_count;
    TpTimePoint cycle_start_times[24];
    TpTimePoint delivery_times[24];

    TpTimePoint bucket_a_first_sample;
    TpTimePoint bucket_b_first_sample;

    uint8_t bucket_a_sample_count;
    TpOperationSlot bucket_a_slots[96];
    uint8_t bucket_b_sample_count;
    TpOperationSlot bucket_b_slots[96];

    int16_t total_delay_offset_minutes;
    uint32_t last_delay_calculation_time;

    uint8_t start_bucket;
    uint8_t is_valid;
    uint8_t is_calculated;
    uint32_t calculation_time;
} DailyTimeSchedule;

// 系统启动模式定义
typedef enum
{
    STARTUP_FULL_SAMPLING = 0,    // 启动满量采样模式
    STARTUP_INSTANT_DELIVERY = 1, // 瞬时送样模式
    STARTUP_SKIP_TO_CYCLE = 2,    // 跳过到周期模式
    STARTUP_INSTANT_SAMPLING = 3  // 瞬时采样模式
} StartupSamplingMode;

/* 时间等比调度器状态结构体 */
typedef struct
{
    // 周期管理（送样驱动周期）
    uint8_t cycle_start_hour; // 周期起点小时数（首次送样后确定）
    uint32_t cycle_idx;       // 当前周期索引（从周期起点开始计数）
    uint8_t active_bucket;    // 活跃桶 (0=A, 1=B)

    // 采样桶与送样桶关联（新增字段）
    uint8_t current_sampling_bucket; // 当前周期正在采样的桶 (0=A, 1=B)
    uint8_t pending_delivery_bucket; // 等待送样的桶 (0=A, 1=B)
    uint8_t pending_delivery_valid;  // 是否有待送样的桶 (0=无, 1=有)

    // 首次送样管理
    uint8_t first_delivery_hour; // 首次送样小时（启动时确定）
    uint8_t first_delivery_min;  // 首次送样分钟（启动时确定）
    uint8_t first_delivery_done; // 首次送样是否完成（0=未完成，1=已完成）

    // 新增字段：完整送样时间支持
    uint8_t first_delivery_day_offset;  // 首次送样日期偏移(0=今天,1=明天)
    uint8_t cycle_start_day_offset;     // 周期开始日期偏移
    uint8_t configured_delivery_hour;   // 配置的送样小时(StartHour)
    uint8_t configured_delivery_min;    // 配置的送样分钟(StartMin)
    uint32_t seconds_to_first_delivery; // 到首次送样的剩余时间（秒，避免重复计算）

    // 采样管理
    uint32_t sample_done_mask;   // 采样完成位掩码
    uint8_t sample_count;        // 本周期采样次数
    uint16_t sample_offsets[24]; // 采样时间点数组 (分钟偏移)

    // 推迟采样支持（新增字段）
    uint8_t pending_sample_valid; // 是否有待触发的采样 (0=无, 1=有)
    int8_t pending_sample_idx;    // 待触发的采样索引 (-1=无效, >=0=有效)

    // 送样管理
    uint8_t delivery_done;    // 本周期送样是否完成
    uint8_t delivery_skipped; // 送样是否被手动跳过

    // 手动操作影响累计
    int16_t manual_water_delta_a; // 本周期手动操作对A桶水量的累计影响
    int16_t manual_water_delta_b; // 本周期手动操作对B桶水量的累计影响

    // 统计信息
    uint32_t total_cycles;        // 总周期数
    uint32_t total_samples;       // 总采样次数
    uint32_t total_deliveries;    // 总送样次数
    uint32_t skip_delivery_count; // 跳过送样次数

    // 启动满量采样管理（新增字段）
    uint32_t startup_sample_interval_sec; // 启动采样间隔（秒）
    uint32_t next_startup_sample_time;    // 下次启动采样触发时间（秒级时间戳）

    // 瞬时送样模式管理（新增字段）
    uint8_t instant_delivery_done;         // 瞬时送样是否完成（时间不足模式）
    uint8_t instant_sampling_done;         // 瞬时送样后采样是否完成（时间不足模式）
    uint8_t first_sample_sequence_started; // 首次采样序列是否已开始

    // 时间基准管理（新增字段，用于c<0和c>=0的区分）
    uint8_t time_reference_is_delivery; // 时间基准：0=整点，1=送样时间
    uint8_t nearest_hour;               // 最近整点（小时）
    int16_t c_value;                    // c值（送样时间-最近整点，分钟）

    // 运行状态
    uint8_t is_initialized;  // 是否已初始化
    uint8_t is_running;      // 是否正在运行
    uint8_t pause_requested; // 是否请求暂停

    // 采样/送样防重与时刻控制（新增）
    // ★ 修复：使用三个独立掩码替代原有的共享掩码，消除位冲突
    // ★ 修复：扩展为96位掩码数组，支持最多96个采样槽
    uint32_t sampling_mask_a[3];         // A桶采样完成掩码（96位，支持最多96个采样槽）
    uint32_t sampling_mask_b[3];         // B桶采样完成掩码（96位，支持最多96个采样槽）
    uint32_t delivery_mask;              // 送样完成掩码（位0-23对应24个送样时间点）
    uint8_t scheduled_delivery_valid;    // 已绑定的送样计划是否有效（1=待执行，0=需重新计算）
    uint32_t scheduled_delivery_ts;      // 本周期送样计划绝对时间戳（秒）
    uint8_t first_b_sample_armed;        // 首周期切到B后的首个采样一次性武装标志
    uint32_t pending_sample_deadline_ts; // 当前待触发采样的过窗截止（秒）
    uint8_t all_first_tasks_done;
} TimeProportionalSchedulerState;

// 首次A桶采样时间表结构
#define MAX_STARTUP_SAMPLES 24               // 最大启动采样次数（根据实际需求定义）

typedef struct
{
    uint8_t sample_count;                    // 采样次数（由周期时间/采样间隔计算得出）
    uint32_t sample_timestamps[MAX_STARTUP_SAMPLES];  // 采样时间戳（Unix，1970基准）
    uint32_t sample_durations[MAX_STARTUP_SAMPLES];   // 每次采样的持续时间（秒）
    uint8_t is_valid;                        // 时间表是否有效
    uint32_t calculation_time;               // 计算时间戳
} FirstBucketASchedule;

// 调度器TSDB事件码定义
#define SCHED_EVT_INIT 0x0060          // 调度器初始化（时间等比模式）
#define SCHED_EVT_START 0x0061         // 调度器启动（时间等比模式）
#define SCHED_EVT_PAUSE 0x0062         // 调度器暂停（时间等比模式）
#define SCHED_EVT_STOP 0x0063          // 调度器停止（时间等比模式）
#define SCHED_EVT_NEW_CYCLE 0x0064     // 新周期开始（时间等比模式）
#define SCHED_EVT_BUCKET_SWITCH 0x0065 // AB桶切换（时间等比模式）

//==============================================================================
// 调度器外部变量声明
//==============================================================================

/* 调度器全局状态变量 */
extern TimeProportionalSchedulerState g_tp_scheduler;
extern DailyTimeSchedule g_tp_daily_schedule;
extern StartupSamplingMode g_startup_mode;
extern FirstBucketASchedule g_first_bucket_a_schedule;

//==============================================================================
// 时间工具函数声明
//==============================================================================

/* RTC时间转换工具 - 实现在 sampling_time.c 中 */
/* 返回Unix时间戳（1970基准），兼容旧命名 */
uint32_t rtc_seconds_since_2000(void);
/* 将Unix/兼容时间戳转换为日期时间 */
void rtc_seconds_to_datetime(uint32_t seconds, RtcDateTimeComponents *out);

/* 体积-time换算工具 */
extern uint16_t calc_sampling_time_by_volume(uint16_t target_ml);

/* 调度器核心函数 - 实现在 sampling_time.c 中 */
void tp_scheduler_init(void);
void tp_scheduler_start(void);
void tp_scheduler_pause(void);
void tp_scheduler_resume(void);
void tp_scheduler_stop(void);
void tp_scheduler_reinit_if_running(void);
uint8_t tp_scheduler_is_running(void);
uint32_t tp_scheduler_get_cycle_index(void);
uint8_t tp_scheduler_get_active_bucket(void);
uint8_t tp_scheduler_get_sample_progress(void);
void scheduler_dispatcher(void);
void scheduler_time_proportional(void);
void tp_log_startup_params(uint8_t nearest_hour, int16_t c_value, StartupSamplingMode mode,
                           uint8_t first_delivery_day_offset, uint8_t cycle_start_day_offset);
void tp_schedule_reset(DailyTimeSchedule *schedule);
void tp_daily_schedule_build(const void *sc, const void *dc, DailyTimeSchedule *schedule);

/* 调试和监控函数 */
void tp_print_scheduler_detailed_info(void);

/* 内部函数声明 */
uint8_t tp_calculate_first_delivery(void);
void tp_compute_startup_interval(uint32_t seconds_to_delivery, uint8_t sample_count);
void tp_plan_bucket_samples(const SampleConfig *sc, DailyTimeSchedule *schedule, uint8_t start_bucket);
void handle_full_startup_sampling(uint32_t current_timestamp);
void handle_time_urgent_sampling(uint32_t current_timestamp);
void handle_cycle_based_sampling(uint32_t current_timestamp);
void tp_print_daily_cycle_timeline(void);

/* 调试函数 */
void tp_debug_print_status(void);


#endif // SAMPLING_TIME_H

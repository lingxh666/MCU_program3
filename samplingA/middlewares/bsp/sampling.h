#ifndef __SAMPLING_H__

#define __SAMPLING_H__

#ifdef __cplusplus

extern "C"
{

#endif

#include <stddef.h>

#include "at32f403a_407.h"

#include "rtc.h"

#include "app_flashdb.h"

// 外部变量声明（来自 RTC 模块）

extern calendar_type calendar;

extern volatile uint32_t g_tmr2_seconds;

extern volatile uint32_t g_tmr3_milliseconds;

// 体积-时间换算（由校准映射实现）

extern uint16_t calc_sampling_time_by_volume(uint16_t target_ml);

// 全局瓶号（1-24）

extern uint8_t g_current_bottle_number;

// 采样序列计数器（时间等比模式中的第几次采样）

// 上位机状态码系统定义
typedef enum
{
    BUCKET_STATE_IDLE = 0,      // 空闲
    BUCKET_STATE_SAMPLING = 1,  // 采样
    BUCKET_STATE_DELIVERY = 2,  // 送样
    BUCKET_STATE_RETENTION = 3, // 留样
    BUCKET_STATE_DRAINING = 4,  // 排空
    BUCKET_STATE_MIXING = 5,    // 搅拌
    BUCKET_STATE_OTHER = 6      // 其他
} BucketStateCode;

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RtcDateTimeComponents;

// 上位机状态码结构体
typedef struct
{
    uint8_t bucket_a_state;    // A桶状态码 0-6
    uint8_t bucket_b_state;    // B桶状态码 0-6
    uint32_t last_update_time; // 最后更新时间戳
    uint8_t system_running;    // 系统运行状态 0-停止 1-运行
} HostStatusCode;

// 全局上位机状态码变量
extern HostStatusCode g_host_status;

extern uint32_t g_sampling_sequence_A; // A桶采样序列
extern uint32_t g_sampling_sequence_B; // B桶采样序列

// 蓄水桶水样准备好标志（用于大岳协议40001寄存器）
extern uint8_t g_water_sample_ready_A;
extern uint8_t g_water_sample_ready_B;

// 最近一次送样记录（用于留样时确定对应的送样桶）

extern uint8_t g_last_delivery_bucket; // 最近送样桶号：0=A桶, 1=B桶, 0xFF=无效

extern uint32_t g_last_delivery_time; // 最近送样完成时间戳（秒，自1970年Unix基准）

// ============================================================================
// TSDB新事件类型定义（split记录模式）
// ============================================================================

#define LOG_SAMPLING_START      0x0040  // 采样开始记录
#define LOG_SAMPLING_COMPLETE   0x0041  // 采样完成记录
#define LOG_DELIVERY_START      0x0042  // 送样开始记录
#define LOG_DELIVERY_COMPLETE   0x0043  // 送样完成记录
#define LOG_RETAIN_RECORD       0x0044  // 留样记录

// 辅助事件类型
#define LOG_WATER_CHANGE        0x0050  // 水量变化记录
#define LOG_MANUAL_IMPACT       0x0051  // 手动操作影响记录
#define LOG_CYCLE_SKIP          0x0052  // 周期任务跳过记录

// 系统启动模式定义

typedef enum
{

    START_MODE_MANUAL = 0x01, // 手动启动（复位后）

    START_MODE_POWER_RECOVERY = 0x02 // 断电恢复

} SystemStartMode;

// ============================================================================
// 新日志记录结构体定义（split模式，含sample_id）
// ============================================================================

// 采样开始记录（0x0040）- 36字节
typedef struct __attribute__((packed))
{
    char sample_id[18];         // 唯一样本ID: "YYYYMMDDHHmmss-SSS\0"
    uint8_t sampling_mode;      // 采样模式 1-时间等比 2-流量触发 3-开关量触发 4-直接模式
    uint8_t bucket_id;          // 采样桶号 0-A桶 1-B桶
    uint32_t start_time;        // 采样开始时间戳（秒，Unix基准）
    uint32_t sequence;          // 采样序号（第几次采样）
    uint16_t target_volume;     // 目标采样量(ml)
    uint8_t is_manual;          // 是否手动 1-手动 0-周期
    uint8_t reserved[1];        // 对齐保留
} SamplingStartRecord;

// 采样完成记录（0x0041）- 30字节
typedef struct __attribute__((packed))
{
    char sample_id[18];         // 继承自采样开始记录
    uint32_t end_time;          // 采样结束时间戳（Unix）
    uint16_t actual_volume;     // 实际采样量(ml)
    uint16_t water_level;       // 采样后桶内存水量(ml)
    uint8_t result;             // 采样结果 0-失败 1-成功 2-中止
    uint8_t error_code;         // 错误码（失败时使用）
    uint8_t reserved[2];        // 对齐保留
} SamplingCompleteRecord;

// 送样开始记录（0x0042）- 30字节
typedef struct __attribute__((packed))
{
    char sample_id[18];         // 继承自采样记录
    uint8_t delivery_mode;      // 送样模式 1-定时送样 2-手动送样
    uint8_t bucket_id;          // 送样桶号 0-A桶 1-B桶
    uint32_t start_time;        // 送样开始时间戳（Unix）
    uint16_t target_volume;     // 目标送样量(ml)
    uint8_t is_manual;          // 是否手动 1-手动 0-周期
    uint8_t reserved[1];        // 对齐保留
} DeliveryStartRecord;

// 送样完成记录（0x0043）- 28字节
typedef struct __attribute__((packed))
{
    char sample_id[18];         // 继承自送样开始记录
    uint32_t end_time;          // 送样结束时间戳（Unix）
    uint16_t delivery_volume;   // 实际送样量(ml)
    uint8_t result;             // 送样结果 0-失败 1-成功 2-中止
    uint8_t error_code;         // 错误码（失败时使用）
    uint8_t reserved[2];        // 对齐保留
} DeliveryCompleteRecord;

// 留样记录（0x0044）- 50字节
typedef struct __attribute__((packed))
{
    char sample_id[18];         // 继承自送样记录
    uint8_t retain_mode;        // 留样模式
    uint8_t retain_reason;      // 留样原因
    uint32_t start_time;        // 留样开始时间戳（Unix）
    uint32_t end_time;          // 留样结束时间戳（Unix）
    uint32_t delivery_time;     // 对应的送样完成时间戳(用于关联查询，Unix)
    uint16_t retain_volume;     // 留样量(ml)
    uint16_t bottle_number;     // 留样瓶号(实际留样使用的瓶号)
    float trigger_value;        // 触发值
    uint8_t result;             // 留样结果 0-失败 1-成功 2-中止
    uint8_t error_code;         // 错误码（失败时使用）
    uint8_t acid_added;         // 是否加酸 0-否 1-是
    uint8_t reserved[1];        // 对齐保留
} RetainLogRecord;

// 弃样记录（0x0045）- 32字节
typedef struct __attribute__((packed)) {
    char sample_id[18];         // 样品编号 YYYYMMDDHHmmss-SSS
    uint32_t discard_time;      // 弃样时间戳（Unix）
    uint16_t bottle_number;     // 弃样瓶号(1-24)
    uint8_t result;             // 结果 0-失败 1-成功
    uint8_t error_code;         // 错误码
    uint8_t reserved[6];        // 保留对齐
} DiscardLogRecord;

#define LOG_DISCARD_RECORD  0x0045  // 弃样记录事件类型

// ============================================================================
// 桶上下文（sample_id继承机制）
// ============================================================================

// 水样上下文（替代全局变量g_last_delivery_bucket/time）
typedef struct
{
    char sample_id[18];             // 当前采样ID
    uint8_t bucket_id;              // 桶号 0-A桶 1-B桶
    uint32_t sampling_complete_time;// 采样完成时间戳（Unix）
    uint32_t delivery_complete_time;// 送样完成时间戳（Unix）
    uint8_t valid;                  // 是否有效 0-无效 1-有效
    uint8_t reserved[3];            // 对齐保留
} WaterSampleContext;

// 全局桶上下文变量（在sampling.c中定义）
extern WaterSampleContext g_water_ctx_A;
extern WaterSampleContext g_water_ctx_B;

// 日志记录函数声明
// 已删除：log_sampling_record() 和 log_delivery_record()（旧的单记录函数）
// 新设计使用split record：LOG_SAMPLING_START/COMPLETE, LOG_DELIVERY_START/COMPLETE

void log_retain_record(const RetainLogRecord *record);
void log_discard_record(const DiscardLogRecord *record);

//==============================================================================

// 状态机架构：采样/送样状态机数据结构

//==============================================================================

// 采样状态枚举

typedef enum
{

    SAMP_IDLE = 0, // 空闲

    SAMP_PRE_BLOWBACK, // 阶段1: 前反吹

    SAMP_DELAY_500MS_AFTER_PRE_BLOW, // 延时: 反转→正转保护

    SAMP_EXTERNAL_PUMP, // 阶段2: 外接泵提升

    SAMP_TUBE_HOLD, // 阶段3: 管存

    SAMP_DELAY_200MS_AFTER_TUBE_HOLD, // 延时: 同向重启保护

    SAMP_MEASURE, // 阶段4: 计量采样

    SAMP_DELAY_500MS_AFTER_MEASURE, // 延时: 正转→反转保护（500ms）

    SAMP_POST_BLOWBACK, // 阶段5: 后反吹

    SAMP_DELAY_VALVE_SETUP, // 延时: 等待阀门到位（非阻塞）

    SAMP_COMPLETED, // 完成

    SAMP_ABORTED // 中止

} SamplingStage;

// 采样状态机上下文

typedef struct
{

    SamplingStage stage; // 当前阶段

    uint8_t bucket_id; // 目标桶 (0=A, 1=B)

    uint16_t target_volume; // 目标体积 (ml)

    uint8_t is_manual; // 是否手动 (1=手动, 0=周期)

    // 参数快照（避免运行中被修改）

    uint16_t blowback_time; // 反吹时长 (s)

    uint16_t improve_time; // 提升时长 (s)

    uint16_t tube_hold_time; // 管存时长 (s)

    uint16_t measure_time; // 计量时长 (s)

    uint16_t rpm; // 电机转速

    // 运行时状态

    uint32_t stage_start_time; // 阶段开始时间 (g_tmr2_seconds)

    uint32_t delay_start_ms; // 延时开始时间 (g_tmr3_milliseconds)

    uint32_t total_start_time; // 总流程开始时间

    // 结果

    uint8_t result; // 0=失败, 1=成功, 2=中止

    uint8_t error_code; // 错误码

} SamplingContext;

// 送样状态枚举

typedef enum
{

    DELIV_IDLE = 0, // 空闲

    DELIV_PRE_BLOWBACK, // 阶段1: 反吹清线

    DELIV_DELAY_500MS_AFTER_PRE_BLOW, // 延时

    DELIV_STABILIZE, // 阶段2: 稳定等待 (2秒)

    DELIV_START_MIX, // 阶段3: 启动混样

    DELIV_MEASURE, // 阶段4: 计量送样

    DELIV_DELAY_500MS_AFTER_MEASURE, // 延时: 正转→反转保护

    DELIV_BACKDRAW, // 阶段5: 回抽

    DELIV_DELAY_VALVE_SETUP, // 延时: 等待阀门到位（非阻塞）

    DELIV_COMPLETED, // 完成

    DELIV_ABORTED // 中止

} DeliveryStage;

// 送样状态机上下文

typedef struct
{

    DeliveryStage stage;

    uint8_t bucket_id;

    uint16_t target_volume;

    uint8_t is_manual;

    uint16_t blowback_time;

    uint16_t deliver_time;

    uint16_t backdraw_time;

    uint16_t rpm;

    uint32_t stage_start_time;

    uint32_t delay_start_ms;

    uint32_t total_start_time;

    uint16_t actual_volume;

    uint8_t result;

    uint8_t error_code;

} DeliveryContext;

// 手动操作类型枚举

typedef enum
{

    MANUAL_NONE = 0, // 无操作

    // 流程类 (1-9)

    MANUAL_SAMPLING = 1, // 手动采样测试

    MANUAL_DELIVERY = 2, // 手动送样测试

    MANUAL_RETENTION = 3, // 手动留样测试

    MANUAL_INSTANT_DELIVERY = 4, // 瞬时送样

    MANUAL_INSTANT_RETENTION = 5, // 瞬时留样

    // 单点控制 (10-19)

    MANUAL_MOTOR_CONTROL = 10, // 电机控制

    MANUAL_PUMP_CONTROL = 11, // 泵控制

    MANUAL_VALVE_CONTROL = 12, // 阀门控制

    MANUAL_MIXER_CONTROL = 13, // 混样电机

    MANUAL_DRAIN_CONTROL = 14, // 排水控制

    // 瓶控制 (20-29)

    MANUAL_BOTTLE_RESET = 20, // 瓶复位

    MANUAL_BOTTLE_MOVE = 21, // 移动瓶

    MANUAL_BOTTLE_EMPTY = 22, // 排空瓶

    // 系统级 (90+)

    MANUAL_SYSTEM_RESET = 90, // 系统复位

    MANUAL_SYSTEM_START = 91 // 系统启动

} ManualOperationType;

// 手动操作影响记录结构

typedef struct
{

    ManualOperationType operation; // 操作类型

    uint32_t timestamp; // 时间戳

    // 水量影响

    int16_t water_delta_a; // A桶水量变化（正=增加，负=减少）

    int16_t water_delta_b; // B桶水量变化

    // 调度影响

    uint8_t skip_sampling; // 是否跳过周期采样

    uint8_t skip_delivery; // 是否跳过周期送样

    uint8_t skip_retention; // 是否跳过周期留样

    // 瓶位影响

    uint8_t bottle_changed; // 瓶位是否改变

    uint8_t bottle_after; // 操作后的瓶号

    uint8_t has_impact; // 是否有影响（总标志）

} ManualOperationImpact;

// ============================================================================
// 注意：旧的detail事件码（0x0010-0x002F）已删除
// 新系统使用split记录模式（0x0040-0x0044），不再记录流程细节
// ============================================================================

//==============================================================================

// 新状态机函数接口（周期流程使用）

//==============================================================================

/**

 * @brief 启动采样流程（周期调用）

 * @param bucket_id 目标桶 0=A, 1=B

 * @param target_volume 目标体积 (ml)

 * @param is_manual 是否手动模式 0=周期, 1=手动

 * @param skip_bucket_state_check 1=跳过桶状态检查（用于首周期B桶首次采样），0=正常检查

 * @return 1=成功启动, 0=参数错误或已有流程运行

 */

uint8_t sampling_start(uint8_t bucket_id, uint16_t target_volume, uint8_t is_manual, uint8_t skip_bucket_state_check);

/**

 * @brief Start a full sampling cycle once with aggregated target volume.

 * @param bucket_id 0 = bucket A, 1 = bucket B

 * @param is_manual 0 = automatic scheduling, 1 = manual operation

 * @return 1 on success, 0 on failure

 */

uint8_t sampling_start_full_volume(uint8_t bucket_id, uint8_t is_manual);

/**

 * @brief 推进采样状态机（task3轮询调用）

 * @note 非阻塞，10-20ms内返回

 */

void sampling_step_if_active(void);

/**

 * @brief 查询采样状态

 * @return 0=空闲, 1=运行中, 2=完成成功, 3=完成失败, 4=中止

 */

uint8_t sampling_get_status(void);

/**

 * @brief 获取采样结果

 * @param result 结果输出 (1=成功, 0=失败, 2=中止)

 * @param error_code 错误码输出

 * @return 1=有效结果, 0=还在运行

 */

uint8_t sampling_get_result(uint8_t *result, uint8_t *error_code);

/**

 * @brief 等待采样完成（测试函数使用）

 * @param timeout_ms 超时时间(ms), 0=无限等待

 * @return 1=成功, 0=失败/超时

 */

uint8_t sampling_wait(uint32_t timeout_ms);

/**

 * @brief 启动送样流程（周期调用）

 * @param bucket_id 目标桶 0=A, 1=B

 * @param target_volume 目标体积 (ml)

 * @param is_manual 是否手动模式 0=周期, 1=手动

 * @return 1=成功启动, 0=失败, 2=水量不足(仅测试模式返回)

 */

uint8_t delivery_start(uint8_t bucket_id, uint16_t target_volume, uint8_t is_manual);

/**

 * @brief 推进送样状态机（task3轮询调用）

 */

void delivery_step_if_active(void);

/**

 * @brief 查询送样状态

 * @return 0=空闲, 1=运行中, 2=完成成功, 3=完成失败, 4=中止

 */

uint8_t delivery_get_status(void);

/**
 * @brief 强制中止采样（如果正在进行）
 */
void sampling_force_abort_if_active(void);

/**
 * @brief 强制中止送样（如果正在进行）
 */
void delivery_force_abort_if_active(void);

/**

 * @brief 获取送样结果

 */

uint8_t delivery_get_result(uint8_t *result, uint8_t *error_code);

/**

 * @brief 等待送样完成（测试函数使用）

 */

uint8_t delivery_wait(uint32_t timeout_ms);

/**

 * @brief 记录水量变化到TSDB

 */

void log_water_volume_change(uint8_t bucket_id, uint16_t water_level, const char *operation);

/**

 * @brief 记录手动操作影响到TSDB

 */

void log_manual_operation_impact(const ManualOperationImpact *impact);

/**

 * @brief 记录周期任务跳过事件到TSDB

 * @param task_type 任务类型: 1=采样, 2=送样, 3=留样

 * @param reason 跳过原因码

 * @param description 描述字符串（可选）

 */

void log_cycle_task_skipped(uint8_t task_type, uint8_t reason, const char *description);

//==============================================================================

// 系统控制函数接口

//==============================================================================

/**

 * @brief 启动系统复位（非阻塞）

 */

void system_reset_start(void);

/**

 * @brief 系统复位状态机更新（非阻塞，需周期性调用）

 * @return 0=进行中, 1=已完成

 */

uint8_t system_reset_update(void);

/**

 * @brief 查询系统复位是否正在进行

 * @return 1=复位进行中, 0=空闲

 */

uint8_t system_reset_is_active(void);

/**

 * @brief 系统启动序列函数

 * @param start_mode 启动模式

 */

void system_start_sequence(SystemStartMode start_mode);

/**

 * @brief 更新全局状态时间

 */

void update_global_state_time(void);

/**

 * @brief 送样完成通知task4接口

 */

void scheduler_dispatcher(void);
void update_all_timers(void);
void notify_task4_delivery_complete(uint8_t bucket_id);
void analysis_report_switch(uint8_t level, uint32_t ts);
void analysis_report_modbus(uint8_t action_code, uint32_t ts);
void analysis_report_analog(const float ch_values[6], uint32_t ts);
void update_bucket_state(uint8_t bucket_id, BucketStateCode state);
void update_system_running_state(uint8_t running);

#ifdef __cplusplus
}

#endif

#endif

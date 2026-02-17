#include "freertos_app.h"
#include "sampling_time.h"
#include "at32f403a_407_rtc.h"

#include <string.h>
#include <stdio.h>

#define UNIX_OFFSET_2000 ((30UL * 365UL + 7UL) * 24UL * 3600UL) // 1970-01-01 到 2000-01-01 的秒数

#define TP_RESUME_BUFFER_MIN 2u

TimeProportionalSchedulerState g_tp_scheduler = {0};
DailyTimeSchedule g_tp_daily_schedule = {0};
FirstBucketASchedule g_first_bucket_a_schedule = {0};

// 临时全局变量，解决编译器作用域问题
static uint32_t g_seconds_to_delivery = 0;
StartupSamplingMode g_startup_mode = STARTUP_FULL_SAMPLING;

// 采样状态跟踪变量
static uint8_t g_a_samples_completed = 0; // A桶已完成采样次数
static uint8_t g_b_samples_completed = 0; // B桶已完成采样次数
static uint32_t g_last_b_sample_time = 0; // B桶上次采样时间戳

// 前置函数声明
static uint16_t tp_minutes_add(uint16_t base_minutes, int16_t delta_minutes);
static uint32_t tp_timepoint_to_timestamp(const TpTimePoint *tp);
static uint16_t tp_minutes_round_up(uint16_t minute_of_day);

// ★ 96位掩码工具函数（支持最多96个采样槽）
static inline uint8_t bitset96_test(const uint32_t *arr, uint8_t i)
{
    if (i >= 96) return 0;
    return (arr[i >> 5] & (1UL << (i & 31))) != 0;
}

static inline void bitset96_set(uint32_t *arr, uint8_t i)
{
    if (i >= 96) return;
    arr[i >> 5] |= (1UL << (i & 31));
}

static inline void bitset96_clear_all(uint32_t *arr)
{
    arr[0] = 0; arr[1] = 0; arr[2] = 0;
}

/**
 * @brief 获取当前Unix时间戳（基于RTC计数器，1970年基准）
 * @return uint32_t Unix时间戳（秒）
 */
uint32_t rtc_seconds_since_2000(void)
{
    return rtc_counter_get();
}

/**
 * @brief 将时间戳转换为日期时间结构体
 * @param seconds Unix时间戳（1970基准，兼容传入2000基准，会自动转换）
 * @param out 输出的日期时间结构体指针
 * @note 逆向转换，支持闰年计算，将时间戳分解为年月日时分秒
 */
void rtc_seconds_to_datetime(uint32_t seconds, RtcDateTimeComponents *out)
{
    if (seconds >= UNIX_OFFSET_2000)
    {
        seconds -= UNIX_OFFSET_2000; // 转换为2000基准，复用原有拆分逻辑
    }

    uint16_t year = 2000;
    uint8_t month, day, hour, minute, second;
    while (seconds >= (((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 * 86400 : 365 * 86400))
    {
        seconds -= (((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) ? 366 * 86400 : 365 * 86400);
        year++;
    }
    uint32_t day_of_year = seconds / 86400;
    seconds %= 86400;

    const uint8_t month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days_so_far = 0;

    for (month = 1; month <= 12; month++)
    {
        uint8_t days_in_month = month_days[month - 1];
        if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        {
            days_in_month = 29;
        }

        if (days_so_far + days_in_month > day_of_year)
        {
            day = day_of_year - days_so_far + 1;
            break;
        }

        days_so_far += days_in_month;
    }
    hour = seconds / 3600;
    seconds %= 3600;
    minute = seconds / 60;
    second = seconds % 60;
    out->year = year;
    out->month = month;
    out->day = day;
    out->hour = hour;
    out->minute = minute;
    out->second = second;
}

/**
 * @brief 将分钟数向上舍入到最近的小时
 * @param minute_of_day 一天中的分钟数(0-1439)
 * @return uint16_t 舍入后的分钟数
 */
static uint16_t tp_minutes_round_up(uint16_t minute_of_day)
{
    uint16_t hour = (minute_of_day / 60u) % 24u;
    uint16_t minute = minute_of_day % 60u;
    if (minute >= 30u)
    {
        hour = (hour + 1u) % 24u;
    }
    return hour * 60u;
}

/**
 * @brief 重置日调度表为初始状态
 * @param schedule 要重置的调度表指针
 * @note 清零所有调度数据，设置初始bucket_id为0xFF
 */
void tp_schedule_reset(DailyTimeSchedule *schedule)
{
    if (!schedule)
        return;
    memset(schedule, 0, sizeof(*schedule));
    schedule->bucket_a_first_sample.bucket_id = 0xFFu;
    schedule->bucket_b_first_sample.bucket_id = 0xFFu;
    // ★ 修复B：把未用项的bucket_id显式置为0xFF，避免被当成有效送样点
    for (uint8_t i = 0; i < 24; i++)
    {
        schedule->delivery_times[i].bucket_id = 0xFFu;
        schedule->cycle_start_times[i].bucket_id = 0xFFu;
    }
}

/**
 * @brief 构建日时间调度表
 * @param sc 采样配置指针 (SampleConfig*)
 * @param dc 投递配置指针 (DeliveryConfig*)
 * @param schedule 输出的调度表指针
 * @note 根据24小时周期和采样间隔生成完整的采样和投递时间点
 */
void tp_daily_schedule_build(const void *sc, const void *dc, DailyTimeSchedule *schedule)
{
    // printf("[调试] 开始构建日调度表...\n");

    if (!sc || !dc || !schedule)
    {
        // printf("[调试] 调度表构建失败: 参数无效\n");
        return;
    }

    tp_schedule_reset(schedule);

    if (!tp_calculate_first_delivery())
    {
        schedule->is_valid = 0;
        return;
    }

    const SampleConfig *sample_config = (const SampleConfig *)sc;

    // 构建送样时间表（基于tp_calculate_first_delivery计算的参数）
    uint8_t base_hour = g_tp_scheduler.first_delivery_hour;
    uint8_t base_min = g_tp_scheduler.first_delivery_min;
    uint16_t cycle_time = sample_config->CycleTime;  // ★ 修复：使用uint16_t避免>255分钟被截断
    // ★ 修改：动态计算cycle_count，而不是硬编码24
    uint8_t cycle_count = (cycle_time > 0) ? (uint8_t)(1440u / cycle_time) : 24u;
    // ★ 修复D：校验cycle_count不超过数组容量24
    if (cycle_count > 24u) cycle_count = 24u;

    schedule->cycle_count = cycle_count;

    for (uint8_t i = 0u; i < cycle_count; i++)
    {
        uint16_t delivery_minutes = (base_hour * 60 + base_min) + (uint16_t)i * cycle_time;
        delivery_minutes %= 1440;

        schedule->delivery_times[i].hour = (uint8_t)(delivery_minutes / 60);
        schedule->delivery_times[i].minute = (uint8_t)(delivery_minutes % 60);
        // 使用正确的桶分配：基于start_bucket轮流
        schedule->delivery_times[i].bucket_id = (schedule->start_bucket + i) % 2u;

        // 计算送样归属的周期整点
        uint16_t cycle_start_minutes = tp_minutes_round_up(delivery_minutes);
        schedule->cycle_start_times[i].hour = (uint8_t)(cycle_start_minutes / 60);
        schedule->cycle_start_times[i].minute = (uint8_t)(cycle_start_minutes % 60);
        schedule->cycle_start_times[i].bucket_id = 0xFF;
    }

    // 构建采样时间表（采样在送样的前一个周期）
    tp_plan_bucket_samples(sample_config, schedule, schedule->start_bucket);

    // 根据确定的start_bucket重新更新送样桶分配
    for (uint8_t i = 0u; i < cycle_count; i++)
    {
        schedule->delivery_times[i].bucket_id = (schedule->start_bucket + i) % 2u;
    }

    schedule->is_valid = 1;
    schedule->is_calculated = 1;
    schedule->calculation_time = g_tmr2_seconds;

    // printf("[调试] 日调度表构建完成: start_bucket=%d, cycle_count=%u\n",
    //        schedule->start_bucket, schedule->cycle_count);

    {
        uint32_t delivery_sec = (uint32_t)g_tp_scheduler.first_delivery_hour * 3600 +
                                (uint32_t)g_tp_scheduler.first_delivery_min * 60;
        uint32_t current_sec = (uint32_t)calendar.hour * 3600 +
                               (uint32_t)calendar.min * 60 + (uint32_t)calendar.sec;

        if (g_tp_scheduler.first_delivery_day_offset == 1)
        {
            delivery_sec += 24 * 3600;
        }

        if (delivery_sec >= current_sec)
        {
            g_seconds_to_delivery = delivery_sec - current_sec;
        }
        else
        {
            g_seconds_to_delivery = delivery_sec + 24 * 3600 - current_sec;
        }

        if (g_seconds_to_delivery < 3600 && g_tp_scheduler.first_delivery_day_offset == 1)
        {
            uint32_t current_day_minutes = (uint32_t)calendar.hour * 60 + (uint32_t)calendar.min;
            uint32_t delivery_minutes = (uint32_t)g_tp_scheduler.first_delivery_hour * 60 + (uint32_t)g_tp_scheduler.first_delivery_min;

            if (delivery_minutes < current_day_minutes)
            {
                g_seconds_to_delivery = (24 * 60 - current_day_minutes + delivery_minutes) * 60;
            }
        }
    }

    if (g_startup_mode != STARTUP_INSTANT_SAMPLING &&
        g_startup_mode != STARTUP_SKIP_TO_CYCLE &&
        g_seconds_to_delivery > 0 && g_seconds_to_delivery < 86400)
    {
        uint8_t sample_count = sample_config->CycleTime / sample_config->SampleInterval;
        if (sample_count > 24)
            sample_count = 24;
        if (sample_count > 0)
        {
            tp_compute_startup_interval(g_seconds_to_delivery, sample_count);
        }
    }
}

/**
 * @brief 初始化时间比例调度器
 * @note 清零调度器状态，设置初始化标志，为启动做准备
 */
void tp_scheduler_init(void)
{
    memset(&g_tp_scheduler, 0, sizeof(g_tp_scheduler));
    g_tp_scheduler.is_initialized = 1;
}

/**
 * @brief 启动时间比例调度器
 * @note 构建日调度表，设置运行状态，清除暂停标志
 */
void tp_scheduler_start(void)
{
    if (!g_tp_scheduler.is_initialized)
    {
        tp_scheduler_init();
    }

    tp_daily_schedule_build(&g_SampleConfig, &g_DeliveryConfig, &g_tp_daily_schedule);

    // 重置采样状态跟踪变量
    g_a_samples_completed = 0;
    g_b_samples_completed = 0;
    g_last_b_sample_time = 0;

    g_tp_scheduler.is_running = 1;
    g_tp_scheduler.pause_requested = 0;

    // printf("[调试] 调度器开始启动...\n");

    // 打印配置信息用于调试（已注释）
    // printf("[调试] 采样配置: 体积=%u ml, 间隔=%u 分钟, 周期=%u 分钟\n",
    //        g_SampleConfig.SampleVolume, g_SampleConfig.SampleInterval, g_SampleConfig.CycleTime);
    // printf("[调试] 送样配置: 送样时间=%02d:%02d\n",
    //        g_DeliveryConfig.StartHour, g_DeliveryConfig.StartMin);

    // 打印调度器详细信息（用于调试）- 已禁用，节省15KB栈/RAM空间
    // tp_print_scheduler_detailed_info();

    // printf("[调试] 调度器启动完成\n");
}

/**
 * @brief 暂停时间比例调度器
 * @note 设置暂停请求标志，调度器会在下一个循环中停止执行
 */
void tp_scheduler_pause(void)
{
    g_tp_scheduler.pause_requested = 1;
}

/**
 * @brief 恢复时间比例调度器运行
 * @note 清除暂停请求标志，调度器将在下一个循环中恢复执行
 */
void tp_scheduler_resume(void)
{
    g_tp_scheduler.pause_requested = 0;
}

/**
 * @brief 停止时间比例调度器
 * @note 完全停止调度器，清除运行状态和暂停标志
 */
void tp_scheduler_stop(void)
{
    g_tp_scheduler.is_running = 0;
    g_tp_scheduler.pause_requested = 0;
}

/**
 * @brief 如果调度器正在运行则重新初始化
 * @note 用于配置更新后的调度器重建，避免影响正常的采样流程
 */
void tp_scheduler_reinit_if_running(void)
{
    if (g_tp_scheduler.is_running)
    {
        tp_scheduler_init();
        tp_scheduler_start();
    }
}

/**
 * @brief 安全的分钟数加减运算，自动处理跨日
 * @param base_minutes 基础分钟数
 * @param delta_minutes 要增加或减少的分钟数
 * @return uint16_t 运算结果(0-1439范围)
 */
static uint16_t tp_minutes_add(uint16_t base_minutes, int16_t delta_minutes)
{
    int32_t total = (int32_t)base_minutes + (int32_t)delta_minutes;
    while (total < 0)
    {
        total += 1440;
    }
    while (total >= 1440)
    {
        total -= 1440;
    }
    return (uint16_t)total;
}

/**
 * @brief 从分钟数创建时间点结构体
 * @param tp 输出的时间点结构体指针
 * @param minutes 分钟数
 * @param bucket_id 桶ID
 */
static void tp_timepoint_from_minutes(TpTimePoint *tp, uint16_t minutes, uint8_t bucket_id)
{
    if (!tp)
        return;
    tp->hour = (minutes / 60u) % 24u;
    tp->minute = minutes % 60u;
    tp->bucket_id = bucket_id;
}

/**
 * @brief 从时间点结构体获取分钟数
 * @param tp 时间点结构体指针
 * @return uint16_t 分钟数
 */
static uint16_t tp_minutes_from_tp(const TpTimePoint *tp)
{
    if (!tp)
    {
        return 0u;
    }
    return (uint16_t)(((uint16_t)(tp->hour % 24u)) * 60u +
                      (uint16_t)(tp->minute % 60u));
}

/**
 * @brief 为采样桶规划具体的采样时间点
 * @param sc 采样配置指针
 * @param schedule 调度表指针
 * @param start_bucket 起始桶ID
 * @note 核心算法：根据周期时间、采样间隔、延迟偏移等参数计算每个采样时间点
 */
void tp_plan_bucket_samples(const SampleConfig *sc, DailyTimeSchedule *schedule, uint8_t start_bucket)
{
    (void)start_bucket;

    if (!sc || !schedule || schedule->cycle_count == 0u)
    {
        return;
    }

    schedule->bucket_a_sample_count = 0u;
    schedule->bucket_b_sample_count = 0u;
    schedule->bucket_a_first_sample.bucket_id = 0xFFu;
    schedule->bucket_b_first_sample.bucket_id = 0xFFu;
    schedule->start_bucket = 0u;

    uint8_t anchor_cycle = 0u;
    uint8_t anchor_found = 0u;
    uint16_t anchor_target_min = (uint16_t)((g_tp_scheduler.nearest_hour % 24u) * 60u);
    for (uint8_t idx = 0u; idx < schedule->cycle_count; ++idx)
    {
        uint16_t anchor_min = tp_minutes_from_tp(&schedule->cycle_start_times[idx]);
        if (anchor_min == anchor_target_min)
        {
            anchor_cycle = idx;
            anchor_found = 1u;
            break;
        }
    }
    if (!anchor_found)
    {
        anchor_cycle = 0u;
    }

    // A桶优先原则：所有启动模式下，首次采样都从A桶开始
    (void)anchor_cycle;  // 保留变量避免编译警告
    schedule->start_bucket = 0u;

    uint16_t sample_interval = sc->SampleInterval;
    if (sample_interval == 0u)
    {
        return;
    }

    uint8_t per_cycle_samples = (uint8_t)(sc->CycleTime / sample_interval);
    if (per_cycle_samples == 0u)
    {
        per_cycle_samples = 1u;
    }
    if (per_cycle_samples > 24u)
    {
        per_cycle_samples = 24u;
    }

    const uint8_t capacity_a = (uint8_t)(sizeof(schedule->bucket_a_slots) / sizeof(schedule->bucket_a_slots[0]));
    const uint8_t capacity_b = (uint8_t)(sizeof(schedule->bucket_b_slots) / sizeof(schedule->bucket_b_slots[0]));

    uint16_t delivery_duration_min = g_DeliveryConfig.Duration / 60;
    uint16_t analysis_minutes = g_SampleConfig.AnalysisTime;

    int16_t last_delay_offset = 0;
    uint8_t first_b_cycle_locked = 0u;

    uint8_t first_b_cycle_idx = 0xFFu;

    // 找到B桶送样对应的周期
    for (uint8_t i = 0u; i < schedule->cycle_count; ++i)
    {
        if (schedule->delivery_times[i].bucket_id == 1u) // B桶送样
        {
            // B桶首次采样就在这个周期的整点
            first_b_cycle_idx = i;
            break;
        }
    }

    uint8_t start_cycle_idx = (g_startup_mode == STARTUP_SKIP_TO_CYCLE) ? anchor_cycle : 0u;
    for (uint8_t off = 0u; off < schedule->cycle_count; ++off)
    {
        uint8_t cycle = (uint8_t)(((uint8_t)(start_cycle_idx + off)) % schedule->cycle_count);
        const TpTimePoint *cycle_anchor = &schedule->cycle_start_times[cycle];

        const TpTimePoint *delivery_tp = &schedule->delivery_times[cycle];
        uint16_t anchor_minutes = tp_minutes_from_tp(cycle_anchor);
        uint16_t delivery_minutes = tp_minutes_from_tp(delivery_tp);

        uint16_t resume_minutes = tp_minutes_add(tp_minutes_add(delivery_minutes, (int16_t)delivery_duration_min),
                                                 (int16_t)(analysis_minutes + TP_RESUME_BUFFER_MIN));

        uint8_t del_m = (uint8_t)(delivery_minutes % 60u);
        int16_t delay_minutes = 0;
        int16_t part1 = 0, part2 = 0, part3 = 0, part4 = 0;
        uint8_t use_delay = (sc->CycleTime == 60u) ? 1u : 0u;

        if (use_delay)
        {
            if (del_m <= 10u)
            {
                part1 = (int16_t)del_m;
                part2 = (int16_t)delivery_duration_min;
                part3 = (int16_t)analysis_minutes - 60;
                part4 = (int16_t)TP_RESUME_BUFFER_MIN;
                delay_minutes = part1 + part2 + part3 + part4;
            }
            else if (del_m >= 45u)
            {
                part1 = (int16_t)del_m - 60;
                part2 = (int16_t)delivery_duration_min;
                part3 = (int16_t)analysis_minutes - 60;
                part4 = (int16_t)TP_RESUME_BUFFER_MIN;
                delay_minutes = part1 + part2 + part3 + part4;
            }
            else
            {
                uint16_t diff = (uint16_t)((resume_minutes + 1440u - anchor_minutes) % 1440u);
                delay_minutes = (int16_t)diff;
            }
        }
        else
        {
            delay_minutes = 0;
        }

        if (delay_minutes < 0)
        {
            delay_minutes = 0;
        }

        // 正确的桶ID分配：采样桶与送样桶相同（每个桶在自己的前一个周期采样，为送样做准备）
        uint8_t bucket = delivery_tp->bucket_id; // 采样桶与送样桶相同

        if (g_startup_mode == STARTUP_SKIP_TO_CYCLE)
        {
            // 等待整点模式下，首个周期的采样桶无延时，立即开始
            if (cycle == 0u)
            {
                delay_minutes = 0;
            }
        }

        uint8_t is_first_b_cycle = (first_b_cycle_idx != 0xFFu) &&
                                   (bucket == 1u) &&
                                   (cycle == first_b_cycle_idx) &&
                                   (!first_b_cycle_locked);

        if (is_first_b_cycle)
        {
            // B桶首次采样在周期整点开始，不应该有延时
            delay_minutes = 0;
            first_b_cycle_locked = 1u;
        }

        last_delay_offset = delay_minutes;

        // 修正采样时间计算：采样在送样的前一个周期
        // 计算采样周期的开始时间（送样周期整点 - 周期时间）
        uint16_t sample_cycle_start = tp_minutes_add(anchor_minutes, -(int16_t)sc->CycleTime);

        // 调试信息（已注释以节省资源）
        // printf("[调试] 周期%u: 送样时间%02d:%02d(%c桶), 周期整点%02d:%02d, 采样周期开始%02d:%02d, 采样桶%c桶\n",
        //        cycle,
        //        delivery_tp->hour, delivery_tp->minute,
        //        delivery_tp->bucket_id == 0 ? 'A' : 'B',
        //        (uint8_t)(anchor_minutes / 60), (uint8_t)(anchor_minutes % 60),
        //        (uint8_t)(sample_cycle_start / 60), (uint8_t)(sample_cycle_start % 60),
        //        bucket == 0 ? 'A' : 'B');

        for (uint8_t i = 0u; i < per_cycle_samples; ++i)
        {
            // 正确的采样时间计算：在前一个周期内分配采样时间
            uint16_t scheduled_min = (uint16_t)((sample_cycle_start + (uint16_t)delay_minutes + ((uint16_t)i * sample_interval)) % 1440u);
            TpOperationSlot slot = {0};
            tp_timepoint_from_minutes(&slot.sample_time, scheduled_min, bucket);
            slot.delivery_time = *delivery_tp;
            slot.delay_offset_min = delay_minutes;
            slot.is_valid = 1u;

            // 调试：打印采样分配详情（已注释以节省资源）
            // printf("[调试]   采样#%u: 时间%02d:%02d, 分配到%c槽位 (实际桶ID=%u)\n",
            //        i, (uint8_t)(scheduled_min / 60), (uint8_t)(scheduled_min % 60),
            //        bucket == 0 ? 'A' : 'B', slot.sample_time.bucket_id);

            TpOperationSlot *slots = (bucket == 0u) ? schedule->bucket_a_slots : schedule->bucket_b_slots;
            uint8_t *count_ptr = (bucket == 0u) ? &schedule->bucket_a_sample_count : &schedule->bucket_b_sample_count;
            TpTimePoint *first_tp = (bucket == 0u) ? &schedule->bucket_a_first_sample : &schedule->bucket_b_first_sample;
            uint8_t capacity = (bucket == 0u) ? capacity_a : capacity_b;

            if (*count_ptr >= capacity)
            {
                continue;
            }

            slots[*count_ptr] = slot;
            if (first_tp->bucket_id == 0xFFu)
            {
                *first_tp = slot.sample_time;
            }
            (*count_ptr)++;
        }
    }

    schedule->total_delay_offset_minutes = last_delay_offset;
}

/**
 * @brief 检查调度器是否正在运行
 * @return uint8_t 1表示正在运行，0表示已停止或暂停
 */
uint8_t tp_scheduler_is_running(void)
{
    return g_tp_scheduler.is_running && !g_tp_scheduler.pause_requested;
}

/**
 * @brief 获取当前周期索引
 * @return uint32_t 当前周期的索引号
 */
uint32_t tp_scheduler_get_cycle_index(void)
{
    return g_tp_scheduler.cycle_idx;
}

/**
 * @brief 获取当前激活的采样桶
 * @return uint8_t 0表示A桶，1表示B桶
 */
uint8_t tp_scheduler_get_active_bucket(void)
{
    return g_tp_scheduler.active_bucket;
}

/**
 * @brief 获取当前周期的采样进度百分比
 * @return uint8_t 采样完成百分比(0-100)
 */
uint8_t tp_scheduler_get_sample_progress(void)
{
    if (g_tp_scheduler.sample_count == 0)
        return 0;

    uint8_t completed = 0;
    for (uint8_t i = 0; i < g_tp_scheduler.sample_count; i++)
    {
        if (g_tp_scheduler.sample_done_mask & (1u << i))
        {
            completed++;
        }
    }
    return (completed * 100) / g_tp_scheduler.sample_count;
}

#define TIME_MATCH_TOLERANCE_SEC 5

/**
 * @brief 将时间点转换为绝对时间戳（Unix基准）
 * @param tp 时间点结构体指针
 * @return uint32_t Unix时间戳
 */
static uint32_t tp_timepoint_to_timestamp(const TpTimePoint *tp)
{
    if (!tp || tp->hour > 23 || tp->minute > 59)
    {
        return 0;
    }

    // 确保calendar是最新的
    rtc_time_get();

    // 计算从2000年到当前日期的天数
    const uint16_t month_days[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    uint32_t total_days = 0;
    uint16_t year = calendar.year;

    // 从2000年开始计算
    for (uint16_t y = 2000; y < year; y++)
    {
        total_days += ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 366 : 365;
    }

    total_days += month_days[calendar.month - 1];
    total_days += calendar.date - 1;

    if (calendar.month > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
    {
        total_days += 1;
    }

    // 计算目标时间点的时间戳（2000年基准）
    uint32_t timestamp = total_days * 86400;
    timestamp += tp->hour * 3600;
    timestamp += tp->minute * 60;
    timestamp += 0;  // 秒为0

    // 转换为Unix时间戳
    timestamp += UNIX_OFFSET_2000;

    // 获取当前时间戳（Unix基准）
    uint32_t current_timestamp = rtc_seconds_since_2000();

    // 调整到最近的时间点（今天或明天）
    if (timestamp < current_timestamp)
    {
        // 如果目标时间已过，设置为明天
        timestamp += 24 * 3600;
    }
    else if (timestamp - current_timestamp > 24 * 3600)
    {
        // 如果差值超过24小时，设置为今天
        timestamp -= 24 * 3600;
    }

    return timestamp;
}

// ★ 以下两个函数暂时未使用，已注释以消除编译警告
// 如需使用时间匹配或窗口过期检测功能，可取消注释
#if 0
/**
 * @brief 检查当前时间是否匹配目标时间
 * @param current_ts 当前时间戳
 * @param target_ts 目标时间戳
 * @return uint8_t 1表示匹配，0表示不匹配
 */
static uint8_t is_time_match(uint32_t current_ts, uint32_t target_ts)
{
    if (current_ts < target_ts)
    {
        return 0;
    }

    uint32_t diff = current_ts - target_ts;
    return (diff <= TIME_MATCH_TOLERANCE_SEC) ? 1 : 0;
}

/**
 * @brief 检查时间窗口是否已过期
 * @param current_ts 当前时间戳
 * @param target_ts 目标时间戳
 * @param window_sec 时间窗口大小(秒)
 * @return uint8_t 1表示已过期，0表示未过期
 */
static uint8_t is_time_window_expired(uint32_t current_ts, uint32_t target_ts, uint32_t window_sec)
{
    if (current_ts < target_ts)
    {
        return 0;
    }

    uint32_t diff = current_ts - target_ts;
    return (diff > window_sec) ? 1 : 0;
}
#endif

static void handle_cycle_based_sampling(uint32_t current_timestamp);

/**
 * @brief 处理时间紧迫模式下的采样和送样
 * @note A桶大体积采样→A送样+B桶大体积采样并行→B采样完成进入周期
 */
static void handle_time_urgent_sampling(uint32_t current_timestamp)
{
    static uint8_t a_sampling_started = 0; // A桶采样是否已启动
    static uint8_t delivery_done = 0;      // A桶送样是否完成
    static uint8_t b_sampling_done = 0;    // B桶采样是否完成
    static uint8_t b_last_sample_started = 0;    // B桶是否已启动最后一次采样

    uint32_t current_time = rtc_seconds_since_2000();

    // 阶段0：A桶大体积采样 - 立即启动
    if (!a_sampling_started)
    {
        // printf("[紧迫模式] 启动A桶大体积采样（足量采样）\n");
        if (sampling_start_full_volume(0, 0)) // 足量采样，自动使用配置的总采样量
        {
            a_sampling_started = 1;
            // printf("[紧迫模式] A桶大体积采样已启动\n");
        }
    }

    // 检查A桶采样是否完成
    static uint8_t a_sampling_done = 0;
    if (a_sampling_started && !a_sampling_done)
    {
        uint8_t status = sampling_get_status();
        if (status == 0) // 采样完成
        {
            a_sampling_done = 1;
            g_tp_scheduler.instant_sampling_done = 1;
            // printf("[紧迫模式] A桶大体积采样完成\n");
        }
    }

    // 阶段1：A送样和B桶采样（按时间并行执行）
    // A桶送样 - 按照计算的送样时间执行
    if (!delivery_done)
    {
        uint32_t first_delivery_timestamp = tp_timepoint_to_timestamp(
            &(TpTimePoint){
                .hour = g_tp_scheduler.first_delivery_hour,
                .minute = g_tp_scheduler.first_delivery_min,
                .bucket_id = 0 // A桶
            });

        if (current_time >= first_delivery_timestamp)
        {
            // 调用送样函数，体积和时长由sampling.c内部处理
            if (delivery_start(0, g_SampleConfig.SampleVolume * 4, 0)) // 送样体积是4次采样的总量
            {
                delivery_done = 1;
                // printf("[紧迫模式] A桶送样按时启动（%02d:%02d）\n",
                //        g_tp_scheduler.first_delivery_hour, g_tp_scheduler.first_delivery_min);
            }
            else
            {
                // printf("[紧迫模式] A桶送样启动失败，重试中...\n");
            }
        }
    }

    // B桶整点采样序列 - 按照整点时间执行，使用单次采样体积
    static uint8_t b_sequence_started = 0;
    static uint8_t total_samples = 0;

    // 初始化总采样次数
    if (total_samples == 0)
    {
        total_samples = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
    }

    if (!b_sampling_done)
    {
        rtc_time_get();

        // 计算目标整点（送样时间的下一个整点）
        uint8_t target_hour = g_tp_scheduler.first_delivery_hour;
        if (g_tp_scheduler.first_delivery_min >= 30)
        {
            target_hour = (target_hour + 1) % 24; // 下个整点
        }

        // 在整点时刻启动B桶采样序列
        if (!b_sequence_started)
        {
            if (calendar.hour == target_hour && calendar.min == 0 && calendar.sec < 5)
            {
                b_sequence_started = 1;
                g_last_b_sample_time = current_time;
                g_b_samples_completed = 0; // 重置B桶采样计数
                // printf("[紧迫模式] B桶整点采样序列开始（%02d:00）\n", target_hour);
            }
        }

        // 如果序列已开始，按间隔启动后续采样
        if (b_sequence_started)
        {
            uint8_t current_b_index = g_b_samples_completed + 1;
            uint32_t b_sample_time = g_last_b_sample_time + (current_b_index - 1) * (g_SampleConfig.SampleInterval * 60);

            if (current_time >= b_sample_time && g_b_samples_completed < current_b_index)
            {
                if (sampling_start(1, g_SampleConfig.SampleVolume, 0, 0)) // B桶单次采样，使用配置的SampleVolume
                {
                    g_b_samples_completed = current_b_index;
                    // printf("[紧迫模式] B桶第%d次采样启动\n", current_b_index);

                    // 检查是否启动了最后一次采样
                    if (g_b_samples_completed == total_samples)
                    {
                        b_last_sample_started = 1;
                        // printf("[紧迫模式] B桶最后一次采样已启动\n");
                    }
                }
            }
        }
    }

    // 检查最后一次采样是否完成（使用两种方法判断）
    if (b_last_sample_started && !b_sampling_done)
    {
        // 方法1：检查采样状态
        uint8_t status = sampling_get_status();
        if (status == 0) // 采样已完成
        {
            // printf("[紧迫模式] B桶全部%d次采样完成（状态检查）\n", total_samples);
            b_sampling_done = 1;
        }
        else
        {
            // 方法2：检查B桶水量（或关系）
            uint16_t expected_total_water = g_SampleConfig.SampleVolume * total_samples;
            if (g_State.SaveWarterB >= expected_total_water)
            {
                // printf("[紧迫模式] B桶全部%d次采样完成（水量检查：%u/%u ml）\n",
                //        total_samples, g_State.SaveWarterB, expected_total_water);
                b_sampling_done = 1;
            }
        }
    }

    // 检查是否所有任务都已完成
    // B桶最后一次采样完成时，A桶采样和送样肯定都已完成
    static uint8_t completion_notified = 0; // 避免重复通知
    if (b_sampling_done && !completion_notified)
    {
        // printf("[紧迫模式] 所有任务完成，进入周期模式\n");
        g_tp_scheduler.first_sample_sequence_started = 1;
        g_tp_scheduler.instant_delivery_done = 1;
        completion_notified = 1; // 标记为已通知
    }
}

/**
 * @brief 处理完整启动采样模式：首次A采样、首次A送样、首次B采样
 * @note 完成三步流程后设置first_sample_sequence_started标志，进入周期模式
 */
static void handle_full_startup_sampling(uint32_t current_timestamp)
{
    static enum {
        STARTUP_PHASE_A_MULTIPLE = 0, // A桶多次采样
        STARTUP_PHASE_PARALLEL,       // A送样+B整点采样（并行）
        STARTUP_PHASE_COMPLETE
    } startup_phase = STARTUP_PHASE_A_MULTIPLE;

    static uint8_t a_sampling_active = 0; // A桶采样是否激活
    static uint8_t a_delivery_active = 0; // A桶送样是否激活
    static uint32_t call_count = 0;       // 调用计数器
    static uint8_t first_call = 1;        // 第一次调用标志
    static uint8_t b_last_sample_started = 0;    // B桶是否已启动最后一次采样
    static uint8_t last_sampling_status = 255; // 记录上次采样状态

    uint8_t total_samples = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
    call_count++;

    // 第一次调用时，检查是否已经有采样在运行
    if (first_call)
    {
        uint8_t status = sampling_get_status();
        last_sampling_status = status; // 初始化状态记录
        if (status == 1) // 已经有采样在运行
        {
            a_sampling_active = 1;
            // printf("[调试] 检测到已有采样在运行，设置a_sampling_active=1\n");
        }
        first_call = 0;
    }

    uint32_t current_time = rtc_seconds_since_2000();

    switch (startup_phase)
    {
    case STARTUP_PHASE_A_MULTIPLE:
        // 阶段1：A桶多次采样 - 使用专门的首桶采样时间表
        if (g_a_samples_completed < total_samples && g_first_bucket_a_schedule.is_valid)
        {
            // 使用静态数组记录已触发的采样，防止重复
            static uint8_t sample_triggered[MAX_STARTUP_SAMPLES] = {0};
            // static uint8_t first_warning_printed = 0;  // 注释：仅用于调试打印

            // 定义时间窗口容差（5秒）
            #define TIME_WINDOW_TOLERANCE 5

            // 使用专门为首次A桶计算的时间表
            for (uint8_t i = 0; i < g_first_bucket_a_schedule.sample_count && i < MAX_STARTUP_SAMPLES; i++)
            {
                uint32_t sample_ts = g_first_bucket_a_schedule.sample_timestamps[i];
                int32_t time_diff = (int32_t)current_timestamp - (int32_t)sample_ts;

                // 第1次采样：如果过点就立即启动（无容差限制）
                if (i == 0 && time_diff >= 0 && g_a_samples_completed == 0 && !a_sampling_active && !sample_triggered[i])
                {
                    sample_triggered[i] = 1;
                    // printf("[启动调度] 第1次启动采样过点，立即启动\n");
                    if (sampling_start(0, g_SampleConfig.SampleVolume, 0, 0))
                    {
                        a_sampling_active = 1;
                        last_sampling_status = 1;
                        // printf("[启动调度] A桶第1次采样启动（使用智能计算时间）\n");
                    }
                }
                // 后续采样：在时间窗口内启动
                else if (i > 0 &&
                         time_diff >= 0 && time_diff <= TIME_WINDOW_TOLERANCE &&
                         g_a_samples_completed < i + 1 &&
                         !a_sampling_active && !sample_triggered[i])
                {
                    sample_triggered[i] = 1;
                    RtcDateTimeComponents dt;
                    rtc_seconds_to_datetime(sample_ts, &dt);
                    // printf("[启动调度] 按时启动第%d次采样（计划时间：%02d:%02d:%02d，当前延迟：%d秒）\n",
                    //        i + 1, dt.hour, dt.minute, dt.second, time_diff);

                    if (sampling_start(0, g_SampleConfig.SampleVolume, 0, 0))
                    {
                        a_sampling_active = 1;
                        last_sampling_status = 1;
                        // printf("[启动调度] A桶第%d次采样启动（使用智能计算时间）\n", i + 1);
                    }
                }
                // 超过时间窗口的补救
                else if (i > 0 && time_diff > TIME_WINDOW_TOLERANCE && !sample_triggered[i] &&
                         g_a_samples_completed < i + 1 && !a_sampling_active)
                {
                    // if (!first_warning_printed || i == 1)
                    // {
                    //     printf("[警告] 第%d次采样错过时间窗口（延迟%d秒），将尝试补救触发\n",
                    //            i + 1, time_diff);
                    //     first_warning_printed = 1;
                    // }

                    // 补救触发
                    sample_triggered[i] = 1;
                    RtcDateTimeComponents dt;
                    rtc_seconds_to_datetime(sample_ts, &dt);
                    // printf("[启动调度] 补救启动第%d次采样（计划时间：%02d:%02d:%02d，延迟：%d秒）\n",
                    //        i + 1, dt.hour, dt.minute, dt.second, time_diff);

                    if (sampling_start(0, g_SampleConfig.SampleVolume, 0, 0))
                    {
                        a_sampling_active = 1;
                        last_sampling_status = 1;
                        // printf("[启动调度] A桶第%d次采样补救启动\n", i + 1);
                    }
                }
            }

            // 检查采样完成状态
            if (a_sampling_active)
            {
                uint8_t status = sampling_get_status();
                if (status != last_sampling_status)
                {
                    if (status == 0) // 采样完成
                    {
                        a_sampling_active = 0;
                        g_a_samples_completed++;
                        // printf("[启动调度] A桶第%d次采样完成\n", g_a_samples_completed);

                        if (g_a_samples_completed >= total_samples)
                        {
                            // printf("[启动调度] A桶全部%d次采样完成，进入并行阶段\n", total_samples);
                            startup_phase = STARTUP_PHASE_PARALLEL;
                        }
                    }
                    else if (status == 3 || status == 4) // 采样失败
                    {
                        a_sampling_active = 0;
                        g_a_samples_completed++;
                        // printf("[启动调度] A桶第%d次采样失败（状态=%d）\n", g_a_samples_completed, status);

                        if (g_a_samples_completed >= total_samples)
                        {
                            // printf("[启动调度] A桶采样完成（有失败），进入并行阶段\n");
                            startup_phase = STARTUP_PHASE_PARALLEL;
                        }
                    }
                    last_sampling_status = status;
                }
            }
        }
        else
        {
            startup_phase = STARTUP_PHASE_PARALLEL;
        }
        break;

    case STARTUP_PHASE_PARALLEL:
        // 阶段2：A桶送样和B桶整点采样（保持原有逻辑）
        // 任务1：A桶送样
        if (!a_delivery_active)
        {
            uint32_t first_delivery_timestamp = tp_timepoint_to_timestamp(
                &(TpTimePoint){
                    .hour = g_tp_scheduler.first_delivery_hour,
                    .minute = g_tp_scheduler.first_delivery_min,
                    .bucket_id = 0 // A桶
                });
            if (current_time >= first_delivery_timestamp && !g_tp_scheduler.first_delivery_done)
            {
                // printf("[启动阶段] 开始A桶首次送样（%02d:%02d）\n",
                //        g_tp_scheduler.first_delivery_hour, g_tp_scheduler.first_delivery_min);
                if (delivery_start(0, g_SampleConfig.SampleVolume * total_samples, 0)) // A桶送样
                {
                    a_delivery_active = 1;
                    // printf("[启动阶段] A桶送样启动\n");
                }
            }
        }

        // 任务2：B桶整点采样
        if (g_b_samples_completed < total_samples)
        {
            rtc_time_get();
            uint8_t target_hour = g_tp_scheduler.first_delivery_hour;
            if (g_tp_scheduler.first_delivery_min >= 30)
            {
                target_hour = (target_hour + 1) % 24;
            }

            static uint8_t b_sequence_started = 0;
            if (!b_sequence_started)
            {
                if (calendar.hour == target_hour && calendar.min == 0 && calendar.sec < 5)
                {
                    b_sequence_started = 1;
                    g_last_b_sample_time = current_time;
                    // printf("[启动阶段] B桶整点采样序列开始（%02d:00）\n", target_hour);
                }
            }

            if (b_sequence_started)
            {
                uint8_t current_b_index = g_b_samples_completed + 1;
                uint32_t b_sample_time = g_last_b_sample_time + (current_b_index - 1) * (g_SampleConfig.SampleInterval * 60);

                if (current_time >= b_sample_time && g_b_samples_completed < current_b_index)
                {
                    if (sampling_start(1, g_SampleConfig.SampleVolume, 0, 0))
                    {
                        g_b_samples_completed = current_b_index;
                        // printf("[启动阶段] B桶第%d次采样启动\n", current_b_index);

                        if (g_b_samples_completed == total_samples)
                        {
                            b_last_sample_started = 1;
                            // printf("[启动阶段] B桶最后一次采样已启动\n");
                        }
                    }
                }
            }
        }

        // 检查B桶最后一次采样是否完成
        if (b_last_sample_started && startup_phase != STARTUP_PHASE_COMPLETE)
        {
            uint8_t status = sampling_get_status();
            if (status == 0)
            {
                // printf("[启动阶段] B桶全部%d次采样完成\n", total_samples);
                startup_phase = STARTUP_PHASE_COMPLETE;
                g_tp_scheduler.first_sample_sequence_started = 1;
            }
            else
            {
                uint16_t expected_total_water = g_SampleConfig.SampleVolume * total_samples;
                if (g_State.SaveWarterB >= expected_total_water)
                {
                    // printf("[启动阶段] B桶全部%d次采样完成（水量检查：%u/%u ml）\n",
                    //        total_samples, g_State.SaveWarterB, expected_total_water);
                    startup_phase = STARTUP_PHASE_COMPLETE;
                    g_tp_scheduler.first_sample_sequence_started = 1;
                }
            }
        }
        break;

    case STARTUP_PHASE_COMPLETE:
        break;

    default:
        startup_phase = STARTUP_PHASE_A_MULTIPLE;
        break;
    }
}

/**
 * @brief 时间比例调度器主循环函数
 * @note 需要定期调用，负责执行所有采样和投递调度逻辑
 *       处理不同启动模式，触发采样和投递操作
 */
void scheduler_time_proportional(void)
{
    if (!tp_scheduler_is_running())
    {
        return;
    }

    rtc_time_get();
    uint32_t current_timestamp = rtc_seconds_since_2000();

    if (!g_tp_daily_schedule.is_calculated || !g_tp_daily_schedule.is_valid)
    {
        return;
    }

    static uint8_t first_startup_checked = 0;
    if (!first_startup_checked)
    {
        first_startup_checked = 1;

        if (g_startup_mode == STARTUP_SKIP_TO_CYCLE)
        {
            // 等待整点模式：不在初始化时立即设置标志
            // 将在handle_cycle_based_sampling中等到整点时设置
            // g_tp_scheduler.first_sample_sequence_started = 1;
        }
        else if (g_tp_scheduler.next_startup_sample_time > 0)
        {
            uint32_t next_sample_sec = g_tp_scheduler.next_startup_sample_time;
            RtcDateTimeComponents next_dt;
            rtc_seconds_to_datetime(next_sample_sec, &next_dt);

            uint32_t current_time_sec = rtc_seconds_since_2000();

            if (current_time_sec >= next_sample_sec)
            {
                // ★ 修复：使用96位掩码工具函数
                if (g_tp_scheduler.sampling_mask_a[0] == 0 &&
                    g_tp_scheduler.sampling_mask_a[1] == 0 &&
                    g_tp_scheduler.sampling_mask_a[2] == 0)
                {
                    bitset96_set(g_tp_scheduler.sampling_mask_a, 0);
                    sampling_start(0, g_SampleConfig.SampleVolume, 0, 0);
                }
            }
        }
    }

    switch (g_startup_mode)
    {
    case STARTUP_INSTANT_SAMPLING:
        handle_time_urgent_sampling(current_timestamp);
        if (g_tp_scheduler.instant_sampling_done && g_tp_scheduler.instant_delivery_done)
        {
            handle_cycle_based_sampling(current_timestamp);
        }
        break;

    case STARTUP_FULL_SAMPLING:
        if (g_tp_scheduler.first_sample_sequence_started)
        {
            handle_cycle_based_sampling(current_timestamp);
        }
        else
        {
            // 处理完整启动模式：包含A桶采样、A桶送样、B桶首次采样三个完整流程
            // printf("[调试] 调用handle_full_startup_sampling, 时间戳=%u\n", current_timestamp);
            handle_full_startup_sampling(current_timestamp);
        }
        break;

    case STARTUP_SKIP_TO_CYCLE:
    default:
        handle_cycle_based_sampling(current_timestamp);
        break;
    }

    static uint8_t last_check_day = 0;
    if (calendar.date != last_check_day)
    {
        // ★ 修复：使用96位掩码清零函数
        bitset96_clear_all(g_tp_scheduler.sampling_mask_a);
        bitset96_clear_all(g_tp_scheduler.sampling_mask_b);
        g_tp_scheduler.delivery_mask = 0;
        last_check_day = calendar.date;
    }
}

/**
 * @brief 处理基于周期的采样模式
 * @param current_timestamp 当前时间戳
 * @note 处理启动采样序列和常规周期采样，根据不同启动模式执行相应逻辑
 */
static void handle_cycle_based_sampling(uint32_t current_timestamp)
{
    // C90兼容：在函数开始处声明所有变量
    int i, j;
    uint32_t slot_ts;
    uint32_t current_time;
    uint32_t current_timestamp_for_list;
    static uint32_t last_current_time = 0;
    uint8_t current_min_5;
    static uint8_t last_debug_min = 0xFF;
    static uint8_t slots_debugged = 0;
    static uint8_t last_task_list_min = 0xFF;
    uint8_t bucket;
    uint32_t delivery_timestamp;
    uint32_t mask_bit;
    uint32_t recheck_time;
    uint32_t recheck_slot;
    uint8_t retry_count;
    uint8_t delivery_success;
    uint16_t current_water;
    uint8_t task_count;
    uint8_t a_sample_count;
    uint8_t b_sample_count;

    if (!g_tp_scheduler.first_sample_sequence_started)
    {
        switch (g_startup_mode)
        {
        case STARTUP_FULL_SAMPLING:
        case STARTUP_INSTANT_SAMPLING:
            // 注意：STARTUP_FULL_SAMPLING模式下，这个标志由handle_full_startup_sampling设置
            // STARTUP_INSTANT_SAMPLING模式由外部设置
            break;
        case STARTUP_SKIP_TO_CYCLE:
            // 等待整点模式：需要等到目标整点才开始执行周期时间表
            // 检查当前时间是否到达目标整点
            if (calendar.hour == g_tp_scheduler.nearest_hour && calendar.min == 0 && calendar.sec < 5)
            {
                g_tp_scheduler.first_sample_sequence_started = 1;
                // printf("[周期调度] 等待整点模式：到达目标整点%02d:00，开始执行周期时间表\n",
                //        g_tp_scheduler.nearest_hour);
            }
            break;
        default:
            g_tp_scheduler.first_sample_sequence_started = 1;
            break;
        }
    }

    // 只有当启动阶段完成后才扫描时间表
    // 对于STARTUP_FULL_SAMPLING，需要等待handle_full_startup_sampling完成
    if (!g_tp_scheduler.first_sample_sequence_started)
    {
        return;
    }

    // 对于STARTUP_FULL_SAMPLING，还需要确认B桶启动采样已完成
    if (g_startup_mode == STARTUP_FULL_SAMPLING)
    {
        // B桶启动采样完成后立即开始时间表扫描
        // 由handle_full_startup_sampling函数设置first_sample_sequence_started标志
        // 这里不需要额外等待，直接检查标志即可
        static uint8_t startup_completion_printed = 0;
        if (!startup_completion_printed)
        {
            // printf("[周期调度] 启动阶段完成，开始扫描时间表\n");
            startup_completion_printed = 1;
        }
    }

    // 对于STARTUP_SKIP_TO_CYCLE模式，打印等待整点信息
    static uint8_t skip_cycle_printed = 0;
    if (g_startup_mode == STARTUP_SKIP_TO_CYCLE && !skip_cycle_printed)
    {
        // printf("[周期调度] 等待整点模式，等待到达%02d:00后开始执行周期时间表\n",
        //        g_tp_scheduler.nearest_hour);
        skip_cycle_printed = 1;
    }

    // 核心时间表扫描和触发逻辑
    // 先刷新calendar，确保时间同步
    rtc_time_get();
    current_time = rtc_seconds_since_2000();

    // 检测时间回退或跨天，重置触发状态
    if (current_time < last_current_time || (last_current_time > 0 && current_time - last_current_time > 86400))
    {
        // ★ 修复：使用96位掩码清零函数
        bitset96_clear_all(g_tp_scheduler.sampling_mask_a);  // 重置A桶采样掩码
        bitset96_clear_all(g_tp_scheduler.sampling_mask_b);  // 重置B桶采样掩码
        g_tp_scheduler.delivery_mask = 0;    // 重置送样掩码
        // printf("[周期调度] 检测到时间变化，重置触发状态\n");
    }
    last_current_time = current_time;

    // 调试信息：确认时间表状态（已注释以节省资源）
    static uint8_t schedule_status_printed = 0;
    if (!schedule_status_printed)
    {
        // printf("[周期调度] 时间表状态: valid=%d, A采样数=%u, B采样数=%u\n",
        //        g_tp_daily_schedule.is_valid,
        //        g_tp_daily_schedule.bucket_a_sample_count,
        //        g_tp_daily_schedule.bucket_b_sample_count);

        // 验证时间表数据是否正确（已注释）
        // printf("[周期调度] ========== 时间表数据验证 ==========\n");
        // printf("[周期调度] 当前时间: %02d:%02d:%02d\n", calendar.hour, calendar.min, calendar.sec);

        // 验证A桶采样时间表（已注释）
        // for (int j = 0; j < g_tp_daily_schedule.bucket_a_sample_count && j < 3; j++)
        // {
        //     if (g_tp_daily_schedule.bucket_a_slots[j].is_valid)
        //     {
        //         slot_ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_a_slots[j].sample_time);
        //         RtcDateTimeComponents dt;
        //         rtc_seconds_to_datetime(slot_ts, &dt);
        //         printf("[周期调度] A槽[%d]: %02d:%02d -> %02d:%02d:%02d (桶%u, 时间戳=%u)\n",
        //                j,
        //                g_tp_daily_schedule.bucket_a_slots[j].sample_time.hour,
        //                g_tp_daily_schedule.bucket_a_slots[j].sample_time.minute,
        //                dt.hour, dt.minute, dt.second,
        //                g_tp_daily_schedule.bucket_a_slots[j].sample_time.bucket_id,
        //                slot_ts);
        //     }
        // }

        // 验证B桶采样时间表（已注释）
        // for (int j = 0; j < g_tp_daily_schedule.bucket_b_sample_count && j < 3; j++)
        // {
        //     if (g_tp_daily_schedule.bucket_b_slots[j].is_valid)
        //     {
        //         slot_ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_b_slots[j].sample_time);
        //         RtcDateTimeComponents dt;
        //         rtc_seconds_to_datetime(slot_ts, &dt);
        //         printf("[周期调度] B槽[%d]: %02d:%02d -> %02d:%02d:%02d (桶%u, 时间戳=%u)\n",
        //                j,
        //                g_tp_daily_schedule.bucket_b_slots[j].sample_time.hour,
        //                g_tp_daily_schedule.bucket_b_slots[j].sample_time.minute,
        //                dt.hour, dt.minute, dt.second,
        //                g_tp_daily_schedule.bucket_b_slots[j].sample_time.bucket_id,
        //                slot_ts);
        //     }
        // }

        // 验证送样时间表（已注释）
        // for (int j = 0; j < 5; j++)
        // {
        //     if (g_tp_daily_schedule.delivery_times[j].bucket_id != 0xFF)
        //     {
        //         delivery_timestamp = tp_timepoint_to_timestamp(&g_tp_daily_schedule.delivery_times[j]);
        //         RtcDateTimeComponents dt;
        //         rtc_seconds_to_datetime(delivery_timestamp, &dt);
        //         printf("[周期调度] 送样[%d]: %02d:%02d -> %02d:%02d:%02d (桶%c, 时间戳=%u)\n",
        //                j,
        //                g_tp_daily_schedule.delivery_times[j].hour,
        //                g_tp_daily_schedule.delivery_times[j].minute,
        //                dt.hour, dt.minute, dt.second,
        //                g_tp_daily_schedule.delivery_times[j].bucket_id == 0 ? 'A' : 'B',
        //                delivery_timestamp);
        //     }
        // }

        schedule_status_printed = 1;
    }

    // 动态检测周期整点附近的调试信息（已注释以节省资源）
    // if (calendar.min < 2 || calendar.min >= 58)
    // {
    //     if (last_debug_min != calendar.min)
    //     {
    //         last_debug_min = calendar.min;
    //         printf("[调试] %02d:%02d:%02d - A桶采样数=%u，B桶采样数=%u，触发掩码=0x%08X\n",
    //                calendar.hour, calendar.min, calendar.sec,
    //                g_tp_daily_schedule.bucket_a_sample_count,
    //                g_tp_daily_schedule.bucket_b_sample_count,
    //                g_tp_scheduler.sample_started_mask);
    //         printf("[时间表] A桶采样槽数量: %u\n", g_tp_daily_schedule.bucket_a_sample_count);
    //         for (i = 0; i < g_tp_daily_schedule.bucket_a_sample_count && i < 5; i++)
    //         {
    //             if (g_tp_daily_schedule.bucket_a_slots[i].is_valid)
    //             {
    //                 slot_ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_a_slots[i].sample_time);
    //                 printf("[时间表] A槽[%u]: %02d:%02d (桶%u, 时间戳=%u, 当前时间=%u)\n",
    //                        i,
    //                        g_tp_daily_schedule.bucket_a_slots[i].sample_time.hour,
    //                        g_tp_daily_schedule.bucket_a_slots[i].sample_time.minute,
    //                        g_tp_daily_schedule.bucket_a_slots[i].sample_time.bucket_id,
    //                        slot_ts, current_time);
    //             }
    //         }
    //     }
    // }

    // ★ 修复：使用独立掩码消除位冲突
    // 定义时间窗口和送样保障常量
    #define TIME_WINDOW_MAX_SEC      300   // 最大触发窗口5分钟
    #define MINIMUM_DELIVERY_WATER   50    // 最小送样量50ml（防止空转）
    #define DELIVERY_RETRY_MAX       3     // 送样最大重试次数
    #define DELIVERY_RETRY_DELAY_MS  1000  // 送样重试间隔1秒

    // 扫描A桶采样时间表，到点触发采样
    for (i = 0; i < g_tp_daily_schedule.bucket_a_sample_count; i++)
    {
        if (g_tp_daily_schedule.bucket_a_slots[i].is_valid)
        {
            slot_ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_a_slots[i].sample_time);

            // ★ 修复：使用96位掩码工具函数，避免i>=32时的未定义行为
            // 检查是否到达采样时间且还未触发，添加时间窗口限制
            if (current_time >= slot_ts &&
                (current_time - slot_ts) < TIME_WINDOW_MAX_SEC &&
                !bitset96_test(g_tp_scheduler.sampling_mask_a, i))
            {
                // 忙碌状态守卫：采样可跳过（下次周期重试）
                if (sampling_get_status() == 1)
                {
                    printf("[TP调度] A桶采样跳过：状态机忙碌，槽位=%d\r\n", i);
                    continue;  // 不标记掩码位，下次周期继续尝试
                }

                // 触发前再次验证时间
                rtc_time_get();
                recheck_time = rtc_seconds_since_2000();
                recheck_slot = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_a_slots[i].sample_time);

                if (recheck_time >= recheck_slot)
                {
                    if (sampling_start(0, g_SampleConfig.SampleVolume, 0, 0))
                    {
                        bitset96_set(g_tp_scheduler.sampling_mask_a, i);  // ★ 修复：使用96位掩码
                        printf("[TP调度] A桶采样触发: 槽位=%d\r\n", i);
                    }
                    else
                    {
                        printf("[TP调度] A桶采样启动失败: 槽位=%d\r\n", i);
                    }
                }
            }
            else if (current_time < slot_ts)
            {
                // 未到时间，跳过
            }
        }
    }

    // 扫描B桶采样时间表，到点触发采样
    for (i = 0; i < g_tp_daily_schedule.bucket_b_sample_count; i++)
    {
        if (g_tp_daily_schedule.bucket_b_slots[i].is_valid)
        {
            slot_ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_b_slots[i].sample_time);

            // ★ 修复：使用96位掩码工具函数，避免i>=32时的未定义行为
            // 检查是否到达采样时间且还未触发，添加时间窗口限制
            if (current_time >= slot_ts &&
                (current_time - slot_ts) < TIME_WINDOW_MAX_SEC &&
                !bitset96_test(g_tp_scheduler.sampling_mask_b, i))
            {
                // 忙碌状态守卫：采样可跳过（下次周期重试）
                if (sampling_get_status() == 1)
                {
                    printf("[TP调度] B桶采样跳过：状态机忙碌，槽位=%d\r\n", i);
                    continue;  // 不标记掩码位，下次周期继续尝试
                }

                if (sampling_start(1, g_SampleConfig.SampleVolume, 0, 0))
                {
                    bitset96_set(g_tp_scheduler.sampling_mask_b, i);  // ★ 修复：使用96位掩码
                    printf("[TP调度] B桶采样触发: 槽位=%d\r\n", i);
                }
            }
        }
    }

    // 扫描送样时间表，到点触发送样
    // ★ 核心原则：送样必须保证执行！
    // ★ 修复A：使用cycle_count而不是固定24，避免扫描到未初始化的伪项
    for (i = 0; i < g_tp_daily_schedule.cycle_count; i++)
    {
        if (g_tp_daily_schedule.delivery_times[i].bucket_id != 0xFF)
        {
            bucket = g_tp_daily_schedule.delivery_times[i].bucket_id;
            delivery_timestamp = tp_timepoint_to_timestamp(&g_tp_daily_schedule.delivery_times[i]);
            mask_bit = 1UL << i;

            // 检查是否到达送样时间且还未触发，添加时间窗口限制
            if (current_time >= delivery_timestamp &&
                (current_time - delivery_timestamp) < TIME_WINDOW_MAX_SEC &&
                !(g_tp_scheduler.delivery_mask & mask_bit))
            {
                // 等待整点模式：跳过第一个B桶送样，确保A桶优先
                if (g_startup_mode == STARTUP_SKIP_TO_CYCLE &&
                    bucket == 1 && // B桶
                    !g_tp_scheduler.first_delivery_done)
                {
                    g_tp_scheduler.delivery_mask |= mask_bit;  // 标记为已处理
                    printf("[TP调度] 跳过B桶首次送样，等待A桶先送样\r\n");
                    continue;
                }

                // ★ 核心修复：放宽送样条件 - 只要有水就送
                current_water = (bucket == 0) ? g_State.SaveWarterA : g_State.SaveWarterB;

                printf("[TP调度] 送样条件检查: 桶=%c, 水量=%u, 最小要求=%d\r\n",
                       bucket ? 'B' : 'A', current_water, MINIMUM_DELIVERY_WATER);

                if (current_water >= MINIMUM_DELIVERY_WATER)
                {
                    // ★ 送样重试机制
                    retry_count = 0;
                    delivery_success = 0;

                    while (!delivery_success && retry_count < DELIVERY_RETRY_MAX)
                    {
                        // 检查采样/送样状态机是否忙碌
                        if (sampling_get_status() == 1 || delivery_get_status() == 1)
                        {
                            vTaskDelay(pdMS_TO_TICKS(DELIVERY_RETRY_DELAY_MS));
                            retry_count++;
                            printf("[TP调度] 送样等待中，重试 %d/%d\r\n", retry_count, DELIVERY_RETRY_MAX);
                            continue;
                        }

                        // ★ 修改：实际送样量 = 当前水量（不是期望水量）
                        if (delivery_start(bucket, current_water, 0))
                        {
                            delivery_success = 1;
                            g_tp_scheduler.delivery_mask |= mask_bit;
                            printf("[TP调度] 送样触发成功: 桶=%c, 水量=%u, mask=0x%08lX\r\n",
                                   bucket ? 'B' : 'A', current_water, g_tp_scheduler.delivery_mask);

                            // 如果是A桶送样且是首次，标记首次送样完成
                            if (bucket == 0 && !g_tp_scheduler.first_delivery_done)
                            {
                                g_tp_scheduler.first_delivery_done = 1;
                            }
                        }
                        else
                        {
                            retry_count++;
                            if (retry_count < DELIVERY_RETRY_MAX)
                            {
                                vTaskDelay(pdMS_TO_TICKS(DELIVERY_RETRY_DELAY_MS));
                            }
                        }
                    }

                    if (!delivery_success)
                    {
                        // 重试耗尽，强制通知Task4排水
                        printf("[TP调度] 送样启动失败，强制通知Task4排水: 桶=%c\r\n", bucket ? 'B' : 'A');
                        notify_task4_delivery_complete(bucket);
                        g_tp_scheduler.delivery_mask |= mask_bit;  // 标记为已处理，防止死循环
                    }
                }
                else
                {
                    // ★ 完全没水，通知Task4执行排水清理
                    printf("[TP调度] 水量不足跳过送样，通知Task4排水: 桶=%c, 水量=%u\r\n",
                           bucket ? 'B' : 'A', current_water);
                    notify_task4_delivery_complete(bucket);
                    g_tp_scheduler.delivery_mask |= mask_bit;  // 标记为已处理
                }
            }
        }
    }

    // STARTUP_SKIP_TO_CYCLE模式现在完全依赖时间表扫描，无需额外处理

    current_min_5 = (calendar.min / 5) * 5;
    if (last_debug_min != current_min_5)
    {
        last_debug_min = current_min_5;
    }

    if (!slots_debugged)
    {
        slots_debugged = 1;
    }

    if (last_task_list_min != calendar.min)
    {
        current_timestamp_for_list = rtc_seconds_since_2000();

        typedef struct
        {
            uint32_t timestamp;
            const char *type;
            uint8_t bucket_id;
            uint8_t sequence_num;
            uint8_t total_count;
        } TaskInfo;

        TaskInfo tasks[20];
        task_count = 0;

        a_sample_count = 0;
        for (i = 0; i < g_tp_daily_schedule.bucket_a_sample_count && task_count < 20; i++)
        {
            uint32_t ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_a_slots[i].sample_time);
            if (ts > current_timestamp_for_list)
            {
                a_sample_count++;
                tasks[task_count].timestamp = ts;
                tasks[task_count].type = "A";
                tasks[task_count].bucket_id = 0;
                tasks[task_count].sequence_num = a_sample_count;
                tasks[task_count].total_count = 4;
                task_count++;
            }
        }

        b_sample_count = 0;
        for (i = 0; i < g_tp_daily_schedule.bucket_b_sample_count && task_count < 20; i++)
        {
            uint32_t ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.bucket_b_slots[i].sample_time);
            if (ts > current_timestamp_for_list)
            {
                b_sample_count++;
                tasks[task_count].timestamp = ts;
                tasks[task_count].type = "B";
                tasks[task_count].bucket_id = 1;
                tasks[task_count].sequence_num = b_sample_count;
                tasks[task_count].total_count = 4;
                task_count++;
            }
        }

        for (i = 0; i < g_tp_daily_schedule.cycle_count && task_count < 20; i++)
        {
            uint32_t ts = tp_timepoint_to_timestamp(&g_tp_daily_schedule.delivery_times[i]);
            if (ts > current_timestamp_for_list)
            {
                tasks[task_count].timestamp = ts;
                tasks[task_count].type = "????";
                tasks[task_count].bucket_id = g_tp_daily_schedule.delivery_times[i].bucket_id;
                tasks[task_count].sequence_num = 0;
                tasks[task_count].total_count = 0;
                task_count++;
            }
        }

        for (i = 0; i < task_count - 1; i++)
        {
            for (j = i + 1; j < task_count; j++)
            {
                if (tasks[i].timestamp > tasks[j].timestamp)
                {
                    TaskInfo temp = tasks[i];
                    tasks[i] = tasks[j];
                    tasks[j] = temp;
                }
            }
        }

        last_task_list_min = calendar.min;
    }

    // ★ 已删除冗余的采样/送样触发代码块（原1644-1765行）
    // 原因：存在两套触发逻辑，使用不同的位计算方式导致冲突
    // 保留第1418-1551行的逻辑作为唯一触发源
}

/**
 * @brief 计算首次投递时间并确定启动模式
 * @return uint8_t 1表示计算成功，0表示计算失败
 * @note 复杂算法：分析当前时间与配置投递时间的关系，智能选择启动模式
 */
uint8_t tp_calculate_first_delivery(void)
{
    // printf("[调试] 开始计算首次投递时间...\n");
    rtc_time_get();
    uint8_t current_hour = calendar.hour;
    uint8_t current_min = calendar.min;
    uint16_t current_time_total = current_hour * 60 + current_min;

    uint8_t base_hour = g_DeliveryConfig.StartHour;
    uint8_t base_min = g_DeliveryConfig.StartMin;
    uint16_t cycle_time = g_SampleConfig.CycleTime;

    uint8_t sample_count = cycle_time / g_SampleConfig.SampleInterval;
    if (sample_count > 24)
        sample_count = 24;

    uint16_t single_sample_time_sec = calc_sampling_time_by_volume(g_SampleConfig.SampleVolume);
    uint16_t total_volume = g_SampleConfig.SampleVolume * sample_count;
    uint16_t total_sample_time_sec = calc_sampling_time_by_volume(total_volume);

    uint8_t current_nearest_hour;
    if (current_min >= 30)
    {
        current_nearest_hour = (current_hour + 1) % 24;
    }
    else
    {
        current_nearest_hour = current_hour;
    }

    uint32_t nearest_hour_sec = (uint32_t)current_nearest_hour * 3600;
    uint32_t current_sec_total = (uint32_t)current_hour * 3600 + (uint32_t)current_min * 60 + (uint32_t)calendar.sec;
    int32_t distance_to_nearest_hour_sec = (int32_t)nearest_hour_sec - (int32_t)current_sec_total;

    if (distance_to_nearest_hour_sec < 0)
    {
        distance_to_nearest_hour_sec += 24 * 3600;
    }

    uint16_t a_sec = total_sample_time_sec + 120;

    if (distance_to_nearest_hour_sec < (int32_t)a_sec)
    {
        g_startup_mode = STARTUP_SKIP_TO_CYCLE;
        g_tp_scheduler.cycle_start_hour = current_nearest_hour;
        g_tp_scheduler.first_delivery_done = 0;
        g_tp_scheduler.nearest_hour = current_nearest_hour;

        // 等待整点模式下，需要跳过当前送样点，计算下一个真正的送样时间
        // 计算到下一个周期整点的时间
        uint32_t wait_time = (uint32_t)(distance_to_nearest_hour_sec + 60); // 等待到整点后

        // 找到下一个送样时间点（在下一个周期）
        int32_t delivery_times[72];
        uint8_t delivery_count = 0;
        uint16_t base_time_total = base_hour * 60 + base_min;

        // 从下一个周期开始搜索送样时间
        for (int day_offset = 0; day_offset <= 1; day_offset++)
        {
            int32_t day_base_min = (int32_t)base_time_total + day_offset * 24 * 60;

            for (int cycle = 0; cycle < 48; cycle++)
            {
                int32_t candidate_time_min = day_base_min + cycle * (int32_t)cycle_time;
                int32_t current_time_total = (int32_t)current_hour * 60 + (int32_t)current_min;

                // 确保送样时间在等待整点之后
                if (candidate_time_min > current_time_total + (int32_t)(wait_time / 60))
                {
                    // 验证是否有足够时间完成前一个周期的采样
                    // 计算送样时间对应的周期整点
                    int32_t candidate_hour = candidate_time_min / 60;
                    int32_t candidate_min = candidate_time_min % 60;
                    int32_t cycle_anchor_hour;
                    if (candidate_min >= 30)
                    {
                        cycle_anchor_hour = candidate_hour + 1;
                    }
                    else
                    {
                        cycle_anchor_hour = candidate_hour;
                    }

                    // 检查从当前时间到周期整点是否有足够时间完成采样
                    int32_t current_to_anchor_min = (cycle_anchor_hour * 60) - current_time_total;
                    int32_t required_sample_time_min = (total_sample_time_sec + 60) / 60; // 加1分钟余量

                    if (current_to_anchor_min >= required_sample_time_min)
                    {
                        // 时间足够，选择这个送样时间
                        delivery_times[delivery_count++] = candidate_time_min;
                        // printf("[调试] 送样时间 %02d:%02d 验证通过: 当前到周期整点有%d分钟, 需要%d分钟\n",
                        //        candidate_hour, candidate_min, current_to_anchor_min, required_sample_time_min);

                        // 找到第一个可行时间，停止搜索
                        break;
                    }
                    else
                    {
                        // 时间不够，跳过这个送样时间，继续寻找下一个
                        // printf("[调试] 送样时间 %02d:%02d 被跳过: 当前到周期整点只有%d分钟, 需要至少%d分钟\n",
                        //        candidate_hour, candidate_min, current_to_anchor_min, required_sample_time_min);
                    }
                }
            }

            if (delivery_count > 0)
            {
                break;
            }
        }

        // 设置首次送样时间
        if (delivery_count > 0)
        {
            int32_t final_delivery_min = delivery_times[0];
            g_tp_scheduler.first_delivery_hour = (uint8_t)(final_delivery_min / 60) % 24;
            g_tp_scheduler.first_delivery_min = (uint8_t)(final_delivery_min % 60);

            // 计算到首次送样时间的秒数
            int32_t current_day_sec = (int32_t)current_hour * 3600 + (int32_t)current_min * 60 + (int32_t)calendar.sec;
            int32_t delivery_day_sec = (int32_t)g_tp_scheduler.first_delivery_hour * 3600 + (int32_t)g_tp_scheduler.first_delivery_min * 60;

            g_seconds_to_delivery = (uint32_t)(delivery_day_sec - current_day_sec);
            if (delivery_day_sec <= current_day_sec)
            {
                g_seconds_to_delivery += 24 * 3600; // 跨天
                g_tp_scheduler.first_delivery_day_offset = 1; // 跨天
            }
            else
            {
                g_tp_scheduler.first_delivery_day_offset = 0; // 当天
            }

            // 同时设置g_tp_scheduler的seconds_to_first_delivery
            g_tp_scheduler.seconds_to_first_delivery = g_seconds_to_delivery;
        }
        else
        {
            // 如果没找到，使用默认值（等待到下一个整点后的周期）
            g_seconds_to_delivery = (uint32_t)distance_to_nearest_hour_sec + (uint32_t)cycle_time * 60;

            // 设置无效的首次送样时间，避免显示错误信息
            g_tp_scheduler.first_delivery_hour = 0xFF;
            g_tp_scheduler.first_delivery_min = 0xFF;
            g_tp_scheduler.first_delivery_day_offset = 0;
            g_tp_scheduler.seconds_to_first_delivery = g_seconds_to_delivery;

            // printf("[调试] 没有找到有效的送样时间，使用默认值\n");
        }

        return 1;
    }

    int32_t delivery_times[72];
    uint8_t delivery_count = 0;

    uint16_t base_time_total = base_hour * 60 + base_min;

    for (int day_offset = 0; day_offset <= 1; day_offset++) // 只搜索今天和明天
    {
        int32_t day_base_min = (int32_t)base_time_total + day_offset * 24 * 60;

        for (int cycle = 0; cycle < 48; cycle++) // 搜索足够多的周期
        {
            int32_t candidate_time_min = day_base_min + cycle * (int32_t)cycle_time;

            int32_t distance_min = candidate_time_min - (int32_t)current_time_total;

            // 计算送样时间对应的周期整点
            int32_t candidate_hour = candidate_time_min / 60;
            int32_t candidate_min = candidate_time_min % 60;
            int32_t cycle_anchor_hour;
            if (candidate_min >= 30)
            {
                cycle_anchor_hour = candidate_hour + 1;
            }
            else
            {
                cycle_anchor_hour = candidate_hour;
            }
            // 周期整点的分钟数（从午夜开始）
            int32_t cycle_anchor_min = cycle_anchor_hour * 60;
            // 周期整点与当前时间的差值
            int32_t anchor_distance = cycle_anchor_min - (int32_t)current_time_total;

            // 只选择送样时间在未来，且对应的周期整点也在未来的送样点
            if (distance_min >= 0 && anchor_distance >= 0)
            {
                // 验证是否有足够时间完成前一个周期的采样
                int32_t required_sample_time_min = (total_sample_time_sec + 60) / 60; // 加1分钟余量

                if (anchor_distance >= required_sample_time_min)
                {
                    // 时间足够，选择这个送样时间
                    delivery_times[delivery_count++] = candidate_time_min;
                    // printf("[调试] 送样时间 %02d:%02d 验证通过(正常模式): 当前到周期整点有%d分钟, 需要%d分钟\n",
                    //        candidate_hour, candidate_min, anchor_distance, required_sample_time_min);

                    // 找到第一个可行时间，停止搜索
                    if (day_offset == 0)
                    {
                        break;
                    }
                }
                else
                {
                    // 时间不够，跳过这个送样时间，继续寻找下一个
                    // printf("[调试] 送样时间 %02d:%02d 被跳过(正常模式): 当前到周期整点只有%d分钟, 需要至少%d分钟\n",
                    //        candidate_hour, candidate_min, anchor_distance, required_sample_time_min);
                }
            }
        }

        // 如果今天找到了未来送样点，就不需要搜索明天
        if (delivery_count > 0)
        {
            break;
        }
    }

    int32_t best_delivery_time_min = -1;
    int32_t min_distance = INT32_MAX;

    uint16_t blowback = g_SampleConfig.BlowbackTime;
    uint16_t improve_time = g_SampleConfig.SamplingImproveTime;
    uint16_t tube_hold = g_SampleConfig.TubeHoldTime;
    uint16_t time_per_sample_sec = blowback + improve_time + tube_hold + single_sample_time_sec + blowback;
    uint16_t buffer_time = 120;
    uint16_t min_required_time_min = (time_per_sample_sec + buffer_time + 59) / 60;

    // 选择距离当前时间最近的未来送样时间点
    for (uint8_t i = 0; i < delivery_count; i++)
    {
        int32_t delivery_time_min = delivery_times[i];
        int32_t distance = delivery_time_min - (int32_t)current_time_total;

        // 确保是未来时间（前面已经保证过）
        if (distance >= 0 && distance < min_distance)
        {
            min_distance = distance;
            best_delivery_time_min = delivery_time_min;
        }

        if (i < 10 || distance < 60)
        {
            int32_t time_min = delivery_time_min;
            if (time_min < 0)
            {
                time_min = time_min % (24 * 60);
                if (time_min < 0)
                {
                    time_min += 24 * 60;
                }
            }
            time_min = time_min % (24 * 60);

            uint8_t hour = (uint8_t)(time_min / 60);
            if (hour >= 24)
                hour = hour % 24;
        }
    }

    uint8_t final_delivery_hour, final_delivery_min;
    uint8_t candidate_day_offset = 0;

    if (best_delivery_time_min >= 0)
    {
        int32_t total_min_from_midnight = best_delivery_time_min;

        if (total_min_from_midnight >= 24 * 60)
        {
            candidate_day_offset = 1;
            total_min_from_midnight = total_min_from_midnight % (24 * 60);
        }
        else
        {
            candidate_day_offset = 0;
        }

        total_min_from_midnight = total_min_from_midnight % (24 * 60);
        if (total_min_from_midnight < 0)
        {
            total_min_from_midnight += 24 * 60;
        }

        final_delivery_hour = (uint8_t)(total_min_from_midnight / 60);
        final_delivery_min = (uint8_t)(total_min_from_midnight % 60);

        if (final_delivery_hour >= 24)
        {
            final_delivery_hour = final_delivery_hour % 24;
        }
    }
    else
    {
        for (uint8_t i = 0; i < delivery_count; i++)
        {
            int32_t delivery_time_min = delivery_times[i];
            int32_t distance = delivery_time_min - (int32_t)current_time_total;

            if (distance >= (int32_t)min_required_time_min)
            {
                best_delivery_time_min = delivery_time_min;
                min_distance = distance;

                int32_t total_min_from_midnight = best_delivery_time_min;
                if (total_min_from_midnight >= 24 * 60)
                {
                    candidate_day_offset = 1;
                    total_min_from_midnight = total_min_from_midnight % (24 * 60);
                }
                else
                {
                    candidate_day_offset = 0;
                }
                total_min_from_midnight = total_min_from_midnight % (24 * 60);
                if (total_min_from_midnight < 0)
                {
                    total_min_from_midnight += 24 * 60;
                }
                final_delivery_hour = (uint8_t)(total_min_from_midnight / 60);
                final_delivery_min = (uint8_t)(total_min_from_midnight % 60);
                if (final_delivery_hour >= 24)
                {
                    final_delivery_hour = final_delivery_hour % 24;
                }

                break;
            }
        }

        if (best_delivery_time_min < 0)
        {
            // 备用逻辑：选择明天的第一个送样点
            int32_t next_delivery_min = (int32_t)base_time_total + 24 * 60;
            candidate_day_offset = 1;
            best_delivery_time_min = next_delivery_min;
            min_distance = next_delivery_min - (int32_t)current_time_total;

            final_delivery_hour = (uint8_t)((next_delivery_min / 60) % 24);
            final_delivery_min = (uint8_t)(next_delivery_min % 60);
        }
    }

    uint16_t minutes_to_delivery;
    uint32_t delivery_sec_total = (uint32_t)final_delivery_hour * 3600 + (uint32_t)final_delivery_min * 60;

    if (candidate_day_offset == 1)
    {
        delivery_sec_total += 24 * 3600;
    }

    if (delivery_sec_total < current_sec_total)
    {
        if (candidate_day_offset == 0)
        {
            delivery_sec_total += 24 * 3600;
        }
    }

    g_seconds_to_delivery = delivery_sec_total - current_sec_total;

    minutes_to_delivery = (uint16_t)(g_seconds_to_delivery / 60);

    uint8_t nearest_hour;
    if (final_delivery_min >= 30)
    {
        nearest_hour = (final_delivery_hour + 1) % 24;
    }
    else
    {
        nearest_hour = final_delivery_hour;
    }

    int16_t c;
    if (nearest_hour == final_delivery_hour)
    {
        c = (int16_t)final_delivery_min;
    }
    else
    {
        c = (int16_t)final_delivery_min - 60;
    }

    uint16_t e = minutes_to_delivery;

    uint32_t nearest_hour_sec_abs = (uint32_t)nearest_hour * 3600;
    if (candidate_day_offset == 1 && nearest_hour <= final_delivery_hour)
    {
        nearest_hour_sec_abs += 24 * 3600;
    }

    int32_t d_sec = (int32_t)nearest_hour_sec_abs - (int32_t)current_sec_total;
    if (d_sec < 0)
    {
        d_sec += 24 * 3600;
    }
    uint16_t d = (uint16_t)(d_sec / 60);

    sample_count = cycle_time / g_SampleConfig.SampleInterval;

    uint16_t a_min = (a_sec + 59) / 60;
    uint16_t b_sec = sample_count * (single_sample_time_sec + 120) + sample_count * 120;
    uint16_t b_min = (b_sec + 59) / 60;

    StartupSamplingMode startup_mode;
    uint8_t time_reference_is_delivery = 0;
    uint8_t cycle_start_hour;

    if (c < 0)
    {
        time_reference_is_delivery = 1;

        if (e >= b_min)
        {
            startup_mode = STARTUP_FULL_SAMPLING;
            cycle_start_hour = nearest_hour;
        }
        else if (e >= a_min)
        {
            startup_mode = STARTUP_INSTANT_SAMPLING;
            cycle_start_hour = nearest_hour;
        }
        else
        {
            startup_mode = STARTUP_SKIP_TO_CYCLE;
            cycle_start_hour = nearest_hour;
        }
    }
    else
    {
        time_reference_is_delivery = 0;

        if (d >= b_min)
        {
            startup_mode = STARTUP_FULL_SAMPLING;
            cycle_start_hour = nearest_hour;
        }
        else if (d >= a_min)
        {
            startup_mode = STARTUP_INSTANT_SAMPLING;
            cycle_start_hour = nearest_hour;
        }
        else
        {
            startup_mode = STARTUP_SKIP_TO_CYCLE;
            cycle_start_hour = nearest_hour;
        }
    }

    if (startup_mode != STARTUP_FULL_SAMPLING &&
        startup_mode != STARTUP_INSTANT_SAMPLING &&
        startup_mode != STARTUP_SKIP_TO_CYCLE)
    {
        startup_mode = STARTUP_SKIP_TO_CYCLE;
        cycle_start_hour = nearest_hour;
        time_reference_is_delivery = 0;
    }

    {
        DailyTimeSchedule *schedule = &g_tp_daily_schedule;
        uint16_t cycle_minutes2 = cycle_time;
        if (cycle_minutes2 > 0u)
        {
            tp_schedule_reset(schedule);
            schedule->cycle_count = (uint8_t)(1440u / cycle_minutes2);
            // A桶优先启动
            schedule->start_bucket = 0u;

            // 注意：送样时间表和cycle_start_times的构建
            // 将在tp_daily_schedule_build中完成，这里只计算启动参数
        }
    }

    if (startup_mode == STARTUP_SKIP_TO_CYCLE)
    {
        int16_t delivery_minutes = (int16_t)(nearest_hour * 60) + (int16_t)cycle_time + (int16_t)base_min;

        if (delivery_minutes >= 1440)
        {
            delivery_minutes -= 1440;
        }

        g_tp_scheduler.first_delivery_hour = (uint8_t)(delivery_minutes / 60);
        g_tp_scheduler.first_delivery_min = (uint8_t)(delivery_minutes % 60);
    }
    else
    {
        g_tp_scheduler.first_delivery_hour = final_delivery_hour;
        g_tp_scheduler.first_delivery_min = final_delivery_min;
    }
    g_tp_scheduler.first_delivery_day_offset = candidate_day_offset;
    g_tp_scheduler.configured_delivery_hour = base_hour;
    g_tp_scheduler.configured_delivery_min = base_min;
    g_tp_scheduler.cycle_start_hour = cycle_start_hour;
    g_tp_scheduler.cycle_start_day_offset = candidate_day_offset;
    g_tp_scheduler.seconds_to_first_delivery = g_seconds_to_delivery;

    g_tp_scheduler.time_reference_is_delivery = time_reference_is_delivery;
    g_tp_scheduler.nearest_hour = nearest_hour;
    g_tp_scheduler.c_value = c;

    g_startup_mode = startup_mode;

    g_tp_scheduler.first_delivery_done = 0;

    {
        DailyTimeSchedule *schedule = &g_tp_daily_schedule;
        if (schedule->cycle_count > 0u)
        {
            uint8_t start_bucket_local = 0u;
            schedule->start_bucket = start_bucket_local;

            if (schedule->bucket_a_sample_count > 0u)
            {
                schedule->bucket_a_first_sample = schedule->bucket_a_slots[0].sample_time;
            }
            if (schedule->bucket_b_sample_count > 0u)
            {
                schedule->bucket_b_first_sample = schedule->bucket_b_slots[0].sample_time;
            }

            schedule->is_valid = 1u;
            schedule->is_calculated = 1u;
            schedule->calculation_time = rtc_counter_get();
        }
    }

    // printf("[调试] 首次投递计算完成: 启动模式=%d\n", g_startup_mode);
    return 1;
}

/**
 * @brief 计算启动阶段的采样时间间隔
 * @param seconds_to_delivery 距离首次投递的秒数
 * @param sample_count 需要完成的采样数量
 * @note 智能算法：在有限时间内合理安排多个采样，确保不影响正常投递
 */
void tp_compute_startup_interval(uint32_t seconds_to_delivery, uint8_t sample_count)
{
    if (sample_count <= 1)
    {
        g_tp_scheduler.startup_sample_interval_sec = 0;
        return;
    }

    uint32_t available_time;
    uint32_t buffer_time = 180;

    if (g_tp_scheduler.time_reference_is_delivery)
    {
        // 防止下溢：确保 seconds_to_delivery > buffer_time
        if (seconds_to_delivery <= buffer_time) {
            g_tp_scheduler.startup_sample_interval_sec = 1;
            return;
        }
        available_time = seconds_to_delivery - buffer_time;
    }
    else
    {
        rtc_time_get();
        uint32_t current_sec = (uint32_t)calendar.hour * 3600 + (uint32_t)calendar.min * 60 + (uint32_t)calendar.sec;
        uint32_t nearest_hour_sec = (uint32_t)g_tp_scheduler.nearest_hour * 3600;

        if (nearest_hour_sec < current_sec)
        {
            nearest_hour_sec += 24 * 3600;
        }

        // 防止下溢：确保有足够时间
        if (nearest_hour_sec <= current_sec + buffer_time) {
            g_tp_scheduler.startup_sample_interval_sec = 1;
            return;
        }
        available_time = nearest_hour_sec - current_sec - buffer_time;
    }

    uint32_t total_sample_time = 0;
    uint16_t time_per_sample = 0;

    uint16_t blowback = g_SampleConfig.BlowbackTime;
    uint16_t improve = g_SampleConfig.SamplingImproveTime;
    uint16_t tube_hold = g_SampleConfig.TubeHoldTime;

    uint16_t measure_time = calc_sampling_time_by_volume(g_SampleConfig.SampleVolume);
    time_per_sample = blowback + improve + tube_hold + measure_time + blowback;

    total_sample_time = (uint32_t)time_per_sample * sample_count;

    int32_t idle_time = (int32_t)available_time - (int32_t)total_sample_time;

    uint32_t interval;
    if (idle_time < 0)
    {
        interval = 1;
    }
    else
    {
        interval = (uint32_t)idle_time / (sample_count - 1);
        if (interval < 1)
        {
            interval = 1;
        }
    }

    uint32_t current_time_sec = rtc_seconds_since_2000();
    uint32_t delivery_time_sec = current_time_sec + seconds_to_delivery;

    uint32_t buffer_before_delivery = 180;
    // 防止下溢：计算所需最小时间
    uint32_t required_time = buffer_before_delivery + time_per_sample + (sample_count - 1) * interval;
    uint32_t max_start_time;
    if (delivery_time_sec <= required_time) {
        // 时间不足，使用当前时间作为起始
        max_start_time = current_time_sec;
    } else {
        max_start_time = delivery_time_sec - required_time;
    }

    uint32_t best_start_time;
    if (current_time_sec <= max_start_time)
    {
        best_start_time = current_time_sec;
    }
    else
    {
        if (max_start_time < current_time_sec - 3600)
        {
            g_tp_scheduler.startup_sample_interval_sec = 0;
            return;
        }
        best_start_time = max_start_time;
    }

    // 保存到首次A桶采样时间表
    g_first_bucket_a_schedule.sample_count = sample_count;
    g_first_bucket_a_schedule.calculation_time = current_time_sec;
    g_first_bucket_a_schedule.is_valid = 1;

    // 确保采样次数不超过最大限制
    uint8_t max_samples = sample_count;
    if (max_samples > MAX_STARTUP_SAMPLES)
    {
        max_samples = MAX_STARTUP_SAMPLES;
        printf("[警告] 采样次数%d超过最大值%d，将只保存前%d次\n",
               sample_count, MAX_STARTUP_SAMPLES, max_samples);
    }

    for (uint8_t i = 0; i < max_samples; i++)
    {
        uint32_t sample_start = best_start_time + i * interval;
        // uint32_t sample_end = sample_start + time_per_sample;  // 注释：仅用于调试打印

        // 保存到时间表
        g_first_bucket_a_schedule.sample_timestamps[i] = sample_start;
        g_first_bucket_a_schedule.sample_durations[i] = time_per_sample;

        // RtcDateTimeComponents dt_start, dt_end, dt_delivery;
        // rtc_seconds_to_datetime(sample_start, &dt_start);
        // rtc_seconds_to_datetime(sample_end, &dt_end);
        // rtc_seconds_to_datetime(delivery_time_sec, &dt_delivery);

        // printf("[时间等比调度] 启动采样#%d：开始=%02d:%02d:%02d, 结束=%02d:%02d:%02d, 距离送样=%d分%d秒\n",
        //        i + 1,
        //        dt_start.hour, dt_start.minute, dt_start.second,
        //        dt_end.hour, dt_end.minute, dt_end.second,
        //        (int32_t)(delivery_time_sec - sample_start) / 60,
        //        (int32_t)(delivery_time_sec - sample_start) % 60);
    }

    g_tp_scheduler.startup_sample_interval_sec = interval;
    g_tp_scheduler.next_startup_sample_time = best_start_time;
}

#if 0  // ★ 已禁用：此函数包含15KB大数组，会导致栈溢出
/**
 * @brief 打印时间比例调度器的详细信息
 * @note 使用实际计算函数显示启动模式检查、时间计算、采样安排等详细信息
 */
void tp_print_scheduler_detailed_info(void)
{
    printf("[时间等比调度] ========== 时间比例调度器详细信息 ==========\n");

    // 当前时间和基本时间信息
    rtc_time_get();
    uint8_t current_hour = calendar.hour;
    uint8_t current_min = calendar.min;
    uint8_t current_sec = calendar.sec;
    uint32_t current_sec_total = (uint32_t)current_hour * 3600 + (uint32_t)current_min * 60 + (uint32_t)current_sec;

    printf("[时间等比调度] 当前时间：%02d:%02d:%02d\n", current_hour, current_min, current_sec);
    printf("[时间等比调度] 采样体积：%u ml，采样间隔：%u 分钟\n", g_SampleConfig.SampleVolume, g_SampleConfig.SampleInterval);
    printf("[时间等比调度] 周期时间：%u 分钟，周期内采样次数：%u\n", g_SampleConfig.CycleTime, g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval);

    // 使用实际函数计算采样时间
    uint16_t single_sample_time_sec = calc_sampling_time_by_volume(g_SampleConfig.SampleVolume);
    uint16_t blowback = g_SampleConfig.BlowbackTime;
    uint16_t improve = g_SampleConfig.SamplingImproveTime;
    uint16_t tube_hold = g_SampleConfig.TubeHoldTime;
    uint16_t time_per_sample = blowback + improve + tube_hold + single_sample_time_sec + blowback;

    printf("[时间等比调度] ========== 单次采样时间计算 ==========\n");
    printf("[时间等比调度] 目标体积：%u ml -> 计量时间：%u 秒\n", g_SampleConfig.SampleVolume, single_sample_time_sec);
    printf("[时间等比调度] 前反吹：%u 秒，提升：%u 秒，管存：%u 秒，后反吹：%u 秒\n",
           blowback, improve, tube_hold, blowback);
    printf("[时间等比调度] 单次采样总时间：%u 秒 (%.1f 分钟)\n", time_per_sample, time_per_sample / 60.0);
    // 显示送样时间信息和调度器状态
    printf("[时间等比调度] ========== 送样时间信息 ==========\n");
    if (g_tp_scheduler.first_delivery_hour != 0xFF)
    {
        printf("[时间等比调度] 首次送样时间：%02d:%02d",
               g_tp_scheduler.first_delivery_hour, g_tp_scheduler.first_delivery_min);
        if (g_tp_scheduler.first_delivery_day_offset == 1)
        {
            printf(" (明天)");
        }
        else
        {
            printf(" (今天)");
        }
        printf("，距离送样：%u 分 (%u 秒)\n",
               g_tp_scheduler.seconds_to_first_delivery / 60, g_tp_scheduler.seconds_to_first_delivery);
    }
    printf("[时间等比调度] 时间参考基准：%s\n",
           g_tp_scheduler.time_reference_is_delivery ? "送样时间" : "最近整点");
    printf("[时间等比调度] 最近整点：%02d:00\n", g_tp_scheduler.nearest_hour);

    // 显示启动模式
    printf("[时间等比调度] ========== 启动模式 ==========\n");
    const char *mode_str;
    switch (g_startup_mode)
    {
    case STARTUP_FULL_SAMPLING:
        mode_str = "时间充足模式(完全采样)";
        break;
    case STARTUP_INSTANT_SAMPLING:
        mode_str = "时间紧迫模式(立即采样)";
        break;
    case STARTUP_SKIP_TO_CYCLE:
        mode_str = "等待整点模式(跳过到周期)";
        break;
    default:
        mode_str = "未知模式";
        break;
    }
    printf("[时间等比调度] 当前启动模式：%s\n", mode_str);

    // 根据不同模式显示详细信息
    if (g_startup_mode == STARTUP_FULL_SAMPLING)
    {
        printf("[时间等比调度] ========== 时间充足模式详细信息 ==========\n");

        uint8_t sample_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        if (sample_count > 24)
            sample_count = 24;

        printf("[时间等比调度] 策略：执行 %u 次等分采样\n", sample_count);
        printf("[时间等比调度] 每次采样体积：%u ml\n", g_SampleConfig.SampleVolume);

        // 显示tp_compute_startup_interval函数的计算过程和结果
        printf("[时间等比调度] ========== 启动间隔计算(tp_compute_startup_interval) ==========\n");
        printf("[时间等比调度] 距离送样时间：%u 秒 (%.1f 分钟)\n",
               g_tp_scheduler.seconds_to_first_delivery, g_tp_scheduler.seconds_to_first_delivery / 60.0);
        printf("[时间等比调度] 预留缓冲时间：180 秒 (3.0 分钟)\n");

        uint32_t available_time;
        if (g_tp_scheduler.time_reference_is_delivery)
        {
            available_time = g_tp_scheduler.seconds_to_first_delivery - 180;
            printf("[时间等比调度] 可用时间计算：%u - 180 = %u 秒\n",
                   g_tp_scheduler.seconds_to_first_delivery, available_time);
        }
        else
        {
            // 计算到最近整点的时间
            uint32_t nearest_hour_sec = (uint32_t)g_tp_scheduler.nearest_hour * 3600;
            if (nearest_hour_sec < current_sec_total)
            {
                nearest_hour_sec += 24 * 3600;
            }
            available_time = nearest_hour_sec - current_sec_total - 180;
            printf("[时间等比调度] 最近整点时间：%02d:00 (%u 秒)\n", g_tp_scheduler.nearest_hour, nearest_hour_sec);
            printf("[时间等比调度] 可用时间计算：%u - %u - 180 = %u 秒\n",
                   nearest_hour_sec, current_sec_total, available_time);
        }

        uint32_t total_sample_time = time_per_sample * sample_count;
        printf("[时间等比调度] 总采样时间：%u × %u = %u 秒\n", time_per_sample, sample_count, total_sample_time);

        int32_t idle_time = available_time - total_sample_time;
        printf("[时间等比调度] 剩余空闲时间：%u - %u = %d 秒\n", available_time, total_sample_time, idle_time);

        // 显示实际计算的间隔结果
        printf("[时间等比调度] 实际计算间隔：%u 秒 (%.1f 分钟)\n",
               g_tp_scheduler.startup_sample_interval_sec,
               g_tp_scheduler.startup_sample_interval_sec / 60.0);

        // 显示启动阶段A桶首次采样流程 - 使用实际计算的值
        printf("[时间等比调度] ========== 启动阶段A桶首次采样流程 ==========\n");
        printf("[时间等比调度] 注：这是启动阶段的流程1/3，直接执行不按时间表\n");
        printf("[时间等比调度] 使用tp_compute_startup_interval函数计算采样间隔\n");
        uint32_t current_timestamp = rtc_seconds_since_2000();
        uint32_t delivery_timestamp = current_timestamp + g_tp_scheduler.seconds_to_first_delivery;

        printf("[时间等比调度] 当前RTC时间戳：%u 秒\n", current_timestamp);
        printf("[时间等比调度] 送样时间戳：%u 秒\n", delivery_timestamp);
        printf("[时间等比调度] 首次采样时间戳：%u 秒（延迟%u秒）\n",
               g_tp_scheduler.next_startup_sample_time,
               g_tp_scheduler.next_startup_sample_time - current_timestamp);

        for (uint8_t i = 0; i < sample_count; i++)
        {
            uint32_t sample_start_timestamp = g_tp_scheduler.next_startup_sample_time + i * g_tp_scheduler.startup_sample_interval_sec;
            uint32_t sample_end_timestamp = sample_start_timestamp + time_per_sample;

            RtcDateTimeComponents start_dt, end_dt;
            rtc_seconds_to_datetime(sample_start_timestamp, &start_dt);
            rtc_seconds_to_datetime(sample_end_timestamp, &end_dt);

            uint32_t seconds_to_delivery = delivery_timestamp - sample_end_timestamp;

            printf("[时间等比调度] 启动采样#%u：开始=%02d:%02d:%02d, 结束=%02d:%02d:%02d, 距离送样=%u分%u秒\n",
                   i + 1,
                   start_dt.hour, start_dt.minute, start_dt.second,
                   end_dt.hour, end_dt.minute, end_dt.second,
                   seconds_to_delivery / 60, seconds_to_delivery % 60);
        }

        printf("[时间等比调度] 注：启动完成后，将切换到按调度表执行的周期采样模式\n");
    }
    else if (g_startup_mode == STARTUP_INSTANT_SAMPLING)
    {
        printf("[时间等比调度] ========== 时间紧迫模式详细信息 ==========\n");
        printf("[时间等比调度] 立即开始全量采样，不等待间隔\n");
        uint8_t sample_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        uint16_t total_volume = sample_count * g_SampleConfig.SampleVolume;
        uint16_t total_sample_time = calc_sampling_time_by_volume(total_volume);
        printf("[时间等比调度] 全量采样体积：%u ml\n", total_volume);
        printf("[时间等比调度] 全量采样时间：%u 秒 (%.1f 分钟)\n", total_sample_time, total_sample_time / 60.0);
    }
    else if (g_startup_mode == STARTUP_SKIP_TO_CYCLE)
    {
        printf("[时间等比调度] ========== 等待整点模式详细信息 ==========\n");
        printf("[时间等比调度] 等待到最近整点：%02d:00 开始周期采样\n", g_tp_scheduler.nearest_hour);
    }

    // 启动阶段和周期阶段说明
    printf("[时间等比调度] ========== 调度流程说明 ==========\n");
    printf("[时间等比调度] 启动阶段（当前阶段）：\n");
    printf("[时间等比调度]   1. A桶首次采样流程 - 直接执行，不按时间表\n");
    printf("[时间等比调度]   2. A桶首次送样流程 - 直接执行，不按时间表\n");
    printf("[时间等比调度]   3. B桶首次采样流程 - 直接执行，不按时间表，整点启动\n");
    printf("[时间等比调度] 周期阶段（启动完成后）：\n");
    printf("[时间等比调度]   - 按照g_tp_daily_schedule时间表执行采样和送样\n");
    printf("[时间等比调度]   - 时间表包含完整的24小时采样和送样计划\n");

    printf("[时间等比调度] ==========================================\n");

    // 显示24小时周期采样和送样时间线
    tp_print_daily_cycle_timeline();
}
#endif  // ★ tp_print_scheduler_detailed_info 禁用结束

#if 0  // ★ 已禁用：此函数包含15KB大数组，会导致栈溢出
/**
 * @brief 打印24小时周期采样和送样时间线
 * @note 按时间顺序显示完整的采样和送样调度
 */
void tp_print_daily_cycle_timeline(void)
{
    printf("[时间等比调度] 开始打印时间线...\n");

    if (!g_tp_daily_schedule.is_valid || g_tp_daily_schedule.cycle_count == 0)
    {
        printf("[时间等比调度] 调度表无效，无法显示周期时间线\n");
        return;
    }

    printf("[时间等比调度] ========== 24小时采样时间表 ==========\n");
    printf("[时间等比调度] 配置：单次%u ml, 间隔%u分钟, 周期%u分钟\n",
           g_SampleConfig.SampleVolume, g_SampleConfig.SampleInterval, g_SampleConfig.CycleTime);

    // 显示桶首次时间
    if (g_tp_daily_schedule.bucket_a_first_sample.bucket_id != 0xFF)
    {
        printf("[时间等比调度] A桶首次采样：%02d:%02d\n",
               g_tp_daily_schedule.bucket_a_first_sample.hour,
               g_tp_daily_schedule.bucket_a_first_sample.minute);
    }

    if (g_tp_daily_schedule.bucket_b_first_sample.bucket_id != 0xFF)
    {
        printf("[时间等比调度] B桶首次采样：%02d:%02d\n",
               g_tp_daily_schedule.bucket_b_first_sample.hour,
               g_tp_daily_schedule.bucket_b_first_sample.minute);
    }

    printf("[时间等比调度] 全天采样计划：A桶%u次，B桶%u次，总计%u次\n",
           g_tp_daily_schedule.bucket_a_sample_count,
           g_tp_daily_schedule.bucket_b_sample_count,
           g_tp_daily_schedule.bucket_a_sample_count + g_tp_daily_schedule.bucket_b_sample_count);

    // 收集所有事件 - 使用静态变量避免栈溢出（原300元素约15KB会导致栈溢出）
    typedef struct
    {
        uint16_t minutes;
        uint8_t hour;
        uint8_t minute;
        char type[16];   // "A采样", "B采样", "A送样", "B送样"
        char detail[30]; // 详细描述
    } TimelineEvent;

    static TimelineEvent events[300];  // ★ 改为static，避免栈溢出
    uint16_t event_count = 0;

    // 添加所有采样事件（根据实际的bucket_id显示标签）
    for (uint8_t i = 0; i < g_tp_daily_schedule.bucket_a_sample_count && event_count < 280; i++)
    {
        const TpOperationSlot *slot = &g_tp_daily_schedule.bucket_a_slots[i];
        if (slot->is_valid)
        {
            uint16_t min = slot->sample_time.hour * 60 + slot->sample_time.minute;
            events[event_count].minutes = min;
            events[event_count].hour = slot->sample_time.hour;
            events[event_count].minute = slot->sample_time.minute;

            // 根据实际的bucket_id显示标签，而不是数组名称
            if (slot->sample_time.bucket_id == 0)
            {
                // A桶采样
                if (i == 0 && g_tp_daily_schedule.bucket_a_sample_count > 0)
                {
                    strcpy(events[event_count].type, "A采样(首)");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml,整点", g_SampleConfig.SampleVolume);
                }
                else
                {
                    strcpy(events[event_count].type, "A采样");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml", g_SampleConfig.SampleVolume);
                }
            }
            else
            {
                // B桶采样
                strcpy(events[event_count].type, "B采样");
                snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                         "%u ml", g_SampleConfig.SampleVolume);
            }
            event_count++;
        }
    }

    // 添加B桶采样事件
    for (uint8_t i = 0; i < g_tp_daily_schedule.bucket_b_sample_count && event_count < 280; i++)
    {
        const TpOperationSlot *slot = &g_tp_daily_schedule.bucket_b_slots[i];
        if (slot->is_valid)
        {
            uint16_t min = slot->sample_time.hour * 60 + slot->sample_time.minute;
            events[event_count].minutes = min;
            events[event_count].hour = slot->sample_time.hour;
            events[event_count].minute = slot->sample_time.minute;

            // 根据实际的bucket_id显示标签，而不是数组名称
            if (slot->sample_time.bucket_id == 0)
            {
                // 检查是否是首次采样
                if (g_tp_daily_schedule.bucket_a_sample_count == 0)
                {
                    strcpy(events[event_count].type, "A采样(首)");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml,整点", g_SampleConfig.SampleVolume);
                }
                else
                {
                    strcpy(events[event_count].type, "A采样");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml", g_SampleConfig.SampleVolume);
                }
            }
            else
            {
                // 检查是否是首次采样
                if (g_tp_daily_schedule.bucket_b_sample_count == 0 || i == 0)
                {
                    strcpy(events[event_count].type, "B采样(首)");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml,整点", g_SampleConfig.SampleVolume);
                }
                else
                {
                    strcpy(events[event_count].type, "B采样");
                    snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                             "%u ml", g_SampleConfig.SampleVolume);
                }
            }
            event_count++;
        }
    }

    // 添加送样事件
    for (uint8_t i = 0; i < g_tp_daily_schedule.cycle_count && event_count < 280; i++)
    {
        const TpTimePoint *delivery_tp = &g_tp_daily_schedule.delivery_times[i];
        uint16_t min = delivery_tp->hour * 60 + delivery_tp->minute;
        events[event_count].minutes = min;
        events[event_count].hour = delivery_tp->hour;
        events[event_count].minute = delivery_tp->minute;

        snprintf(events[event_count].type, sizeof(events[event_count].type),
                 "%s送样", delivery_tp->bucket_id == 0 ? "A" : "B");

        uint8_t sample_count = g_SampleConfig.CycleTime / g_SampleConfig.SampleInterval;
        uint16_t delivery_volume = sample_count * g_SampleConfig.SampleVolume;
        snprintf(events[event_count].detail, sizeof(events[event_count].detail),
                 "%u ml", delivery_volume);
        event_count++;
    }

    // 按时间排序
    if (event_count > 1)
    {
        for (uint16_t i = 0; i < event_count - 1; i++)
        {
            for (uint16_t j = i + 1; j < event_count; j++)
            {
                if (events[i].minutes > events[j].minutes)
                {
                    TimelineEvent temp = events[i];
                    events[i] = events[j];
                    events[j] = temp;
                }
            }
        }
    }

    // 按时间显示事件
    printf("\n[时间等比调度] 时间表（按时间顺序）：\n");
    printf("时间      事件        详情\n");
    printf("--------  ----------  -------------------\n");

    uint8_t current_hour = 25;

    for (uint16_t i = 0; i < event_count; i++)
    {
        // 每小时显示一次分隔线
        if (events[i].hour != current_hour)
        {
            if (current_hour != 25)
            { // 不是第一行
                printf("--------  ----------  -------------------\n");
            }
            current_hour = events[i].hour;
        }

        printf("%02d:%02d    %-10s  %s\n",
               events[i].hour, events[i].minute,
               events[i].type, events[i].detail);
    }

    printf("--------  ----------  -------------------\n");
    printf("[时间等比调度] 总计：%u个事件\n", event_count);
    printf("[时间等比调度] =================================\n");
}
#endif  // ★ tp_print_daily_cycle_timeline 禁用结束

/**
 * @brief 手动触发调度器状态打印（调试用）
 * @note 可在任何时候调用来查看当前调度器状态
 */
void tp_debug_print_status(void)
{
    printf("[时间等比调试] ========== 手动触发状态打印 ==========\n");
    printf("[时间等比调试] 调度器运行状态：%s\n",
           g_tp_scheduler.is_running ? "运行中" : "已停止");
    printf("[时间等比调试] 调度器暂停状态：%s\n",
           g_tp_scheduler.pause_requested ? "请求暂停" : "正常");
    printf("[时间等比调试] 首次采样序列状态：%s\n",
           g_tp_scheduler.first_sample_sequence_started ? "已开始" : "未开始");
    printf("[时间等比调试] 当前启动模式：%d\n", g_startup_mode);

    // 打印完整信息 - 已禁用，节省15KB栈/RAM空间
    // tp_print_scheduler_detailed_info();
}

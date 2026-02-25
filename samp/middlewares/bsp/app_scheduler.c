/**
 * @file    app_scheduler.c
 * @brief   自动调度器 - 5种模式状态机 + 链式编排（S3增强版）
 */
#include "app_scheduler.h"
#include "app_config.h"
#include "app_sampling.h"
#include "app_adc_module.h"
#include "bsp_rtc.h"
#include <stdio.h>
#include <string.h>

/* 外部时间变量 */
extern volatile uint32_t g_tmr2_seconds;

/* 最大采样点数 */
#define MAX_SAMPLE_POINTS  24

/* ======================== 调度器内部状态（S3增强） ======================== */
typedef struct {
    /* 公共字段 */
    sched_mode_t   mode;
    sched_phase_t  phase;
    uint8_t        running;
    uint8_t        paused;
    uint8_t        active_bucket;
    uint32_t       cycle_idx;
    uint8_t        cycle_start_hour;
    uint8_t        sample_count;
    uint16_t       sample_offsets[MAX_SAMPLE_POINTS];
    uint32_t       sample_done_mask;
    uint8_t        delivery_done;
    uint32_t       total_cycles;
    uint32_t       total_samples;
    uint32_t       total_deliveries;

    /* S3-1: 时间等比增强 */
    struct {
        startup_sampling_mode_t startup_mode;
        uint8_t  delay_active;
        uint32_t delay_end_sec;
        uint32_t anchor_sec;
        /* 推迟累计 */
        int16_t  delay_accum_min;
        /* 手动操作水量影响 */
        int16_t  manual_water_delta_a;
        int16_t  manual_water_delta_b;
        /* 首次送样管理 */
        uint8_t  first_delivery_hour;
        uint8_t  first_delivery_min;
        uint8_t  first_delivery_done;
        /* 待触发采样 */
        uint8_t  pending_sample_valid;
        int8_t   pending_sample_idx;
        /* 启动满量采样 */
        uint32_t startup_sample_interval_sec;
        uint32_t next_startup_sample_time;
        uint8_t  instant_delivery_done;
        uint8_t  instant_sampling_done;
        /* 防重复 */
        uint32_t sample_started_mask;
        uint8_t  delivery_fired;
    } tp;

    /* S3-2: 定时触发增强 */
    struct {
        uint8_t  delivery_hours[24];    /* 启用的送样小时 */
        uint8_t  delivery_indices[24];  /* 送样索引映射 */
        uint8_t  delivery_count;        /* 送样时间点总数 */
        uint8_t  delivery_min;          /* 送样分钟 */
        uint8_t  current_delivery_idx;  /* 当前送样索引 */
        uint8_t  current_delivery_hour; /* 当前送样小时 */
        uint8_t  current_delivery_done; /* 当前送样是否完成 */
        uint8_t  last_check_hour;
    } ft;

    /* S3-3: 流量触发增强 */
    struct {
        uint8_t  flow_active;
        uint8_t  startup_phase;
        uint32_t flow_start_time;
        uint32_t flow_stop_time;
        /* 瞬时送样 */
        uint8_t  instant_delivery_done;
        uint8_t  full_sampling_done;
        /* 流量停止处理 */
        uint8_t  flow_stopped;
        uint8_t  waiting_delivery_complete;
        uint8_t  retention_notified;
        uint8_t  flow_stopped_delivery_bucket;
        /* 延迟偏移 */
        uint8_t  first_cycle_delayed;
        uint16_t delay_offset_min;
        uint8_t  configured_delivery_min;
        uint8_t  sampling_stopped;
    } fl;

    /* S3-4: 开关量增强 */
    struct {
        uint8_t  is_initialized;
        uint8_t  switch_signal_received;
        uint32_t switch_signal_time;
        uint8_t  waiting_first_trigger;
        uint32_t switch_trigger_count;
        /* 窗口检测 */
        uint8_t  window_triggered[MAX_SAMPLE_POINTS];
        uint32_t window_start_time[MAX_SAMPLE_POINTS];
        uint32_t window_end_time[MAX_SAMPLE_POINTS];
        /* 首次触发 */
        uint8_t  first_trigger_done;
        uint32_t first_trigger_time;
        /* 采样暂停/恢复 */
        uint8_t  sampling_stopped;
        uint8_t  waiting_resume;
        /* 启动采样 */
        uint32_t next_startup_sample_time;
        uint32_t startup_sample_interval_sec;
        uint8_t  configured_delivery_min;
    } sw;

    /* 通信触发 */
    struct {
        uint32_t last_delivery_time;
    } cm;
} scheduler_state_t;

static scheduler_state_t s_sched;
static DailyTimeSchedule s_daily_schedule;

/* 通信触发请求（全局，供Modbus写入） */
comm_trigger_req_t g_comm_trigger_req;

/* 前向声明：各模式处理函数 */
static void sched_time_prop(void);
static void sched_fixed_time(void);
static void sched_flow(void);
static void sched_switch(void);
static void sched_comm(void);

/* ======================== 公共接口 ======================== */

void scheduler_init(sched_mode_t mode)
{
    uint8_t i;
    memset(&s_sched, 0, sizeof(s_sched));
    s_sched.mode = mode;
    s_sched.phase = PHASE_IDLE;

    /* 计算采样点 */
    if (g_sampling_cfg.interval_min > 0 && g_sampling_cfg.cycle_time_min > 0) {
        s_sched.sample_count = g_sampling_cfg.cycle_time_min / g_sampling_cfg.interval_min;
        if (s_sched.sample_count > MAX_SAMPLE_POINTS)
            s_sched.sample_count = MAX_SAMPLE_POINTS;
    } else {
        s_sched.sample_count = 1;
    }

    for (i = 0; i < s_sched.sample_count; i++) {
        s_sched.sample_offsets[i] = i * g_sampling_cfg.interval_min;
    }

    /* S3-2: 定时触发初始化送样时间点列表 */
    if (mode == SCHED_MODE_FIXED_TIME) {
        uint8_t idx = 0;
        s_sched.ft.delivery_min = g_delivery_cfg.fixedmin;
        for (i = 0; i < 24; i++) {
            if (g_delivery_cfg.fixedhour[i]) {
                s_sched.ft.delivery_hours[idx] = i;
                s_sched.ft.delivery_indices[i] = idx;
                idx++;
            }
        }
        s_sched.ft.delivery_count = idx;
        printf("[调度器] 定时模式: %d个送样时间点\r\n", idx);
    }

    /* S3-4: 开关量初始化 */
    if (mode == SCHED_MODE_SWITCH) {
        s_sched.sw.is_initialized = 1;
        s_sched.sw.waiting_first_trigger = 1;
        s_sched.sw.configured_delivery_min = g_delivery_cfg.start_min;
    }

    /* S3-3: 流量触发初始化 */
    if (mode == SCHED_MODE_FLOW) {
        s_sched.fl.configured_delivery_min = g_delivery_cfg.start_min;
    }

    printf("[调度器] 初始化: 模式=%d, 周期=%d分, 间隔=%d分, 采样次数=%d\r\n",
           mode, g_sampling_cfg.cycle_time_min,
           g_sampling_cfg.interval_min, s_sched.sample_count);
}

void scheduler_start(void)
{
    if (!s_sched.running) {
        s_sched.running = 1;
        s_sched.paused = 0;
        s_sched.phase = PHASE_STARTUP;
        g_state.current_mode = (uint8_t)s_sched.mode;
        g_state.current_phase = (uint8_t)s_sched.phase;
        printf("[调度器] 启动: 模式=%d\r\n", s_sched.mode);
    }
}

void scheduler_stop(void)
{
    s_sched.running = 0;
    s_sched.paused = 0;
    s_sched.phase = PHASE_IDLE;
    g_state.current_phase = PHASE_IDLE;
    printf("[调度器] 停止\r\n");
}

void scheduler_pause(void)
{
    if (s_sched.running && !s_sched.paused) {
        s_sched.paused = 1;
        printf("[调度器] 暂停\r\n");
    }
}

void scheduler_resume(void)
{
    if (s_sched.running && s_sched.paused) {
        s_sched.paused = 0;
        printf("[调度器] 恢复\r\n");
    }
}

void scheduler_run(void)
{
    if (!s_sched.running || s_sched.paused) return;

    switch (s_sched.mode) {
    case SCHED_MODE_TIME_PROP:  sched_time_prop();  break;
    case SCHED_MODE_FIXED_TIME: sched_fixed_time(); break;
    case SCHED_MODE_FLOW:       sched_flow();       break;
    case SCHED_MODE_SWITCH:     sched_switch();     break;
    case SCHED_MODE_COMM:       sched_comm();       break;
    default: break;
    }

    /* 同步状态到g_state */
    g_state.current_bucket = s_sched.active_bucket;
    g_state.current_phase  = (uint8_t)s_sched.phase;
    g_state.cycle_count    = s_sched.total_cycles;
    g_state.sample_count   = s_sched.total_samples;
    g_state.delivery_count = s_sched.total_deliveries;
}

uint8_t scheduler_is_running(void) { return s_sched.running; }
sched_phase_t scheduler_get_phase(void) { return s_sched.phase; }
sched_mode_t  scheduler_get_mode(void)  { return s_sched.mode; }
uint32_t scheduler_get_total_cycles(void)     { return s_sched.total_cycles; }
uint32_t scheduler_get_total_samples(void)    { return s_sched.total_samples; }
uint32_t scheduler_get_total_deliveries(void) { return s_sched.total_deliveries; }
uint8_t  scheduler_get_active_bucket(void)    { return s_sched.active_bucket; }
startup_sampling_mode_t scheduler_get_startup_mode(void) { return s_sched.tp.startup_mode; }
const DailyTimeSchedule *scheduler_get_daily_schedule(void) { return &s_daily_schedule; }

/* ======================== 外部事件通知 ======================== */

void scheduler_notify_flow_start(uint32_t timestamp)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.flow_active = 1;
    s_sched.fl.flow_start_time = timestamp;
    s_sched.fl.flow_stopped = 0;
}

void scheduler_notify_flow_stop(uint32_t timestamp)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.flow_active = 0;
    s_sched.fl.flow_stop_time = timestamp;
    s_sched.fl.flow_stopped = 1;
}

void scheduler_notify_switch_signal(uint32_t timestamp)
{
    if (s_sched.mode != SCHED_MODE_SWITCH) return;
    s_sched.sw.switch_signal_received = 1;
    s_sched.sw.switch_signal_time = timestamp;
    s_sched.sw.switch_trigger_count++;
}

void scheduler_notify_comm(comm_req_type_t req, uint8_t bucket, uint16_t vol)
{
    g_comm_trigger_req.type    = req;
    g_comm_trigger_req.bucket  = bucket;
    g_comm_trigger_req.volume  = vol;
    g_comm_trigger_req.pending = 1;
}

void scheduler_flow_delivery_complete(uint8_t bucket_id, uint16_t volume)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.waiting_delivery_complete = 0;
    (void)bucket_id;
    (void)volume;
}

void scheduler_flow_retention_complete(void)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.retention_notified = 0;
}

/* Task04通知辅助 */
void scheduler_notify_task4_delivery(uint8_t bucket_id)
{
    extern void notify_task4_delivery_complete(uint8_t bucket_id);
    notify_task4_delivery_complete(bucket_id);
}

/* ======================== 时间等比辅助函数 ======================== */

/* 分钟取整辅助 */
static uint8_t tp_round_hour(uint8_t hour, uint8_t min)
{
    if (min >= 30) return (hour + 1) % 24;
    return hour;
}

/* 构建全天时间表 */
static void tp_build_daily_schedule(DailyTimeSchedule *sch)
{
    uint16_t cycle_min = g_sampling_cfg.cycle_time_min;
    uint16_t cycle_hours = cycle_min / 60;
    uint8_t  delivery_min = g_delivery_cfg.start_min;
    uint8_t  i, slot_idx;

    if (cycle_hours == 0) cycle_hours = 1;
    memset(sch, 0, sizeof(*sch));

    /* 计算周期数和各周期起止时间 */
    sch->cycle_count = (uint8_t)(24 / cycle_hours);
    if (sch->cycle_count > 24) sch->cycle_count = 24;

    for (i = 0; i < sch->cycle_count; i++) {
        uint8_t start_h = (uint8_t)(i * cycle_hours);
        sch->cycle_start_times[i].hour   = start_h;
        sch->cycle_start_times[i].minute = 0;
        sch->cycle_start_times[i].bucket_id = (uint8_t)(i & 1);

        /* 送样时间 = 周期末尾 */
        sch->delivery_times[i].hour   = (uint8_t)((start_h + cycle_hours - 1) % 24);
        sch->delivery_times[i].minute = delivery_min;
        sch->delivery_times[i].bucket_id = (uint8_t)(i & 1);
    }

    /* 填充A/B桶采样时段 */
    sch->bucket_a_sample_count = 0;
    sch->bucket_b_sample_count = 0;
    for (i = 0; i < sch->cycle_count; i++) {
        uint8_t bucket = (uint8_t)(i & 1);
        uint8_t start_h = sch->cycle_start_times[i].hour;
        uint8_t j;
        for (j = 0; j < s_sched.sample_count && j < TP_MAX_SLOTS; j++) {
            TpOperationSlot slot;
            uint16_t offset_min = s_sched.sample_offsets[j];
            memset(&slot, 0, sizeof(slot));
            slot.sample_time.hour   = (uint8_t)((start_h + offset_min / 60) % 24);
            slot.sample_time.minute = (uint8_t)(offset_min % 60);
            slot.sample_time.bucket_id = bucket;
            slot.delivery_time = sch->delivery_times[i];
            slot.is_valid = 1;

            if (bucket == 0) {
                slot_idx = sch->bucket_a_sample_count;
                if (slot_idx < TP_MAX_SLOTS)
                    sch->bucket_a_slots[slot_idx] = slot;
                sch->bucket_a_sample_count++;
            } else {
                slot_idx = sch->bucket_b_sample_count;
                if (slot_idx < TP_MAX_SLOTS)
                    sch->bucket_b_slots[slot_idx] = slot;
                sch->bucket_b_sample_count++;
            }
        }
    }

    sch->start_bucket = 0;
    sch->is_valid = 1;
    sch->total_delay_offset_min = 0;
}

/* ======================== S3-1: 时间等比模式（增强版） ======================== */

static void sched_time_prop(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec, cycle_min, cycle_hours;
    uint32_t cycle_start_sec, elapsed, new_cycle_idx;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;
    cycle_min = g_sampling_cfg.cycle_time_min;
    cycle_hours = cycle_min / 60;
    if (cycle_hours == 0) cycle_hours = 1;

    /* === STARTUP阶段: 4种启动模式判定 === */
    if (s_sched.phase == PHASE_STARTUP) {
        uint8_t anchor_hour = tp_round_hour(dt.hour, dt.min);
        uint32_t anchor_sec = (uint32_t)anchor_hour * 3600 +
                              (uint32_t)g_delivery_cfg.start_min * 60;
        uint32_t remaining;
        if (anchor_sec <= now_sec) anchor_sec += 3600;

        remaining = anchor_sec - now_sec;
        s_sched.tp.anchor_sec = anchor_sec;
        s_sched.tp.first_delivery_hour = (uint8_t)(anchor_sec / 3600) % 24;
        s_sched.tp.first_delivery_min  = g_delivery_cfg.start_min;

        /* 4种启动模式判定 */
        if (remaining >= cycle_min * 60) {
            s_sched.tp.startup_mode = STARTUP_FULL_SAMPLING;
            /* 计算启动采样间隔 */
            if (s_sched.sample_count > 0) {
                s_sched.tp.startup_sample_interval_sec =
                    remaining / s_sched.sample_count;
                s_sched.tp.next_startup_sample_time = g_tmr2_seconds;
            }
            printf("[时间等比] 启动模式: FULL_SAMPLING\r\n");
        } else if (remaining >= g_sampling_cfg.interval_min * 60 * 2) {
            s_sched.tp.startup_mode = STARTUP_SKIP_TO_CYCLE;
            printf("[时间等比] 启动模式: SKIP_TO_CYCLE\r\n");
        } else if (remaining >= g_sampling_cfg.interval_min * 60) {
            s_sched.tp.startup_mode = STARTUP_INSTANT_DELIVERY;
            printf("[时间等比] 启动模式: INSTANT_DELIVERY\r\n");
        } else {
            s_sched.tp.startup_mode = STARTUP_INSTANT_SAMPLING;
            printf("[时间等比] 启动模式: INSTANT_SAMPLING\r\n");
        }

        s_sched.cycle_start_hour = anchor_hour;
        s_sched.cycle_idx = 0;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.active_bucket = 0;
        s_sched.tp.sample_started_mask = 0;
        s_sched.tp.delivery_fired = 0;

        /* 构建全天时间表 */
        tp_build_daily_schedule(&s_daily_schedule);

        s_sched.phase = PHASE_CYCLING;
        printf("[时间等比] 进入周期循环: anchor=%02d:%02d\r\n",
               anchor_hour, g_delivery_cfg.start_min);
        return;
    }

    /* === CYCLING阶段 === */
    if (s_sched.phase != PHASE_CYCLING) return;

    cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
    if (now_sec >= cycle_start_sec) {
        elapsed = now_sec - cycle_start_sec;
    } else {
        elapsed = now_sec + 86400 - cycle_start_sec;
    }
    new_cycle_idx = elapsed / (cycle_min * 60);

    /* 周期切换 */
    if (new_cycle_idx != s_sched.cycle_idx) {
        s_sched.cycle_idx = new_cycle_idx;
        s_sched.active_bucket ^= 1;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.tp.sample_started_mask = 0;
        s_sched.tp.delivery_fired = 0;
        s_sched.tp.pending_sample_valid = 0;
        s_sched.tp.manual_water_delta_a = 0;
        s_sched.tp.manual_water_delta_b = 0;
        s_sched.total_cycles++;
        printf("[时间等比] 新周期: idx=%lu, 桶=%c\r\n",
               (unsigned long)new_cycle_idx,
               s_sched.active_bucket ? 'B' : 'A');
    }

    /* delay机制 */
    if (s_sched.tp.delay_active) {
        if (g_tmr2_seconds < s_sched.tp.delay_end_sec) return;
        s_sched.tp.delay_active = 0;
        /* 累计推迟 */
        s_sched.tp.delay_accum_min++;
        s_daily_schedule.total_delay_offset_min = s_sched.tp.delay_accum_min;
    }

    /* 采样触发 */
    {
        uint8_t i;
        uint32_t cycle_base = cycle_start_sec + s_sched.cycle_idx * cycle_min * 60;
        for (i = 0; i < s_sched.sample_count; i++) {
            uint32_t mask = 1u << i;
            uint32_t sample_sec;
            int32_t diff;
            if (s_sched.sample_done_mask & mask) continue;
            if (s_sched.tp.sample_started_mask & mask) continue;

            sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
            diff = (int32_t)(now_sec - sample_sec);
            if (diff < 0) diff = -diff;

            if (diff <= 30 && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.tp.sample_started_mask |= mask;
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                    printf("[时间等比] 采样#%d: 桶=%c\r\n",
                           i + 1, s_sched.active_bucket ? 'B' : 'A');
                }
            } else if (diff > 30 && now_sec > sample_sec &&
                       !(s_sched.sample_done_mask & mask)) {
                /* 推迟采样: 记录待触发 */
                s_sched.tp.pending_sample_valid = 1;
                s_sched.tp.pending_sample_idx = (int8_t)i;
            }
        }
    }

    /* 推迟采样补偿触发 */
    if (s_sched.tp.pending_sample_valid && !sampling_is_active()) {
        uint8_t idx = (uint8_t)s_sched.tp.pending_sample_idx;
        uint32_t mask = 1u << idx;
        if (!(s_sched.sample_done_mask & mask)) {
            if (sampling_start(s_sched.active_bucket, 0)) {
                s_sched.tp.sample_started_mask |= mask;
                s_sched.sample_done_mask |= mask;
                s_sched.total_samples++;
                s_sched.tp.delay_accum_min++;
                printf("[时间等比] 推迟采样#%d补偿: 桶=%c\r\n",
                       idx + 1, s_sched.active_bucket ? 'B' : 'A');
            }
        }
        s_sched.tp.pending_sample_valid = 0;
    }

    /* 送样触发 */
    if (!s_sched.delivery_done && !s_sched.tp.delivery_fired && dt.sec == 0) {
        uint8_t delivery_hour = (s_sched.cycle_start_hour +
                                 (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                                 24 - 1) % 24;
        if (dt.hour == delivery_hour && dt.min == g_delivery_cfg.start_min) {
            uint16_t water = s_sched.active_bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(s_sched.active_bucket, 0)) {
                    s_sched.delivery_done = 1;
                    s_sched.tp.delivery_fired = 1;
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(s_sched.active_bucket);
                    printf("[时间等比] 送样: 桶=%c, 水量=%u\r\n",
                           s_sched.active_bucket ? 'B' : 'A', water);
                }
            }
        }
    }
}

/* ======================== S3-2: 定时触发模式（增强版） ======================== */

static void sched_fixed_time(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec;
    uint8_t i;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;

    if (s_sched.phase == PHASE_STARTUP) {
        /* 找到下一个最近的送样时间点 */
        s_sched.ft.current_delivery_idx = 0;
        for (i = 0; i < s_sched.ft.delivery_count; i++) {
            uint8_t dh = s_sched.ft.delivery_hours[i];
            uint32_t d_sec = (uint32_t)dh * 3600 +
                             (uint32_t)s_sched.ft.delivery_min * 60;
            if (d_sec > now_sec) {
                s_sched.ft.current_delivery_idx = i;
                break;
            }
        }
        s_sched.ft.current_delivery_hour =
            s_sched.ft.delivery_hours[s_sched.ft.current_delivery_idx];
        s_sched.ft.current_delivery_done = 0;
        s_sched.active_bucket = 0;
        s_sched.sample_done_mask = 0;
        s_sched.ft.last_check_hour = 0xFF;
        s_sched.phase = PHASE_CYCLING;
        printf("[定时触发] 进入周期: 首个送样=%02d:%02d, 共%d个时间点\r\n",
               s_sched.ft.current_delivery_hour,
               s_sched.ft.delivery_min,
               s_sched.ft.delivery_count);
        return;
    }
    if (s_sched.phase != PHASE_CYCLING) return;
    if (s_sched.ft.delivery_count == 0) return;

    /* 当前周期的采样区间 */
    {
        uint8_t cur_dh = s_sched.ft.current_delivery_hour;
        uint8_t dm = s_sched.ft.delivery_min;
        uint8_t cycle_start_h = (dm >= 50) ? cur_dh :
            ((cur_dh + 24 - (uint8_t)(g_sampling_cfg.cycle_time_min / 60)) % 24);
        uint32_t cycle_base = (uint32_t)cycle_start_h * 3600;

        /* 采样触发 */
        for (i = 0; i < s_sched.sample_count; i++) {
            uint32_t mask = 1u << i;
            uint32_t sample_sec;
            int32_t diff;
            if (s_sched.sample_done_mask & mask) continue;

            sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
            diff = (int32_t)(now_sec - sample_sec);
            if (diff < 0) diff = -diff;

            if (diff <= 30 && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                }
            }
        }

        /* 送样触发 */
        if (!s_sched.ft.current_delivery_done &&
            dt.hour == cur_dh && dt.min == dm && dt.sec == 0)
        {
            uint16_t water = s_sched.active_bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(s_sched.active_bucket, 0)) {
                    s_sched.ft.current_delivery_done = 1;
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(s_sched.active_bucket);
                    printf("[定时触发] 送样: 桶=%c, idx=%d/%d\r\n",
                           s_sched.active_bucket ? 'B' : 'A',
                           s_sched.ft.current_delivery_idx + 1,
                           s_sched.ft.delivery_count);

                    /* 切换到下一个送样时间点 */
                    s_sched.active_bucket ^= 1;
                    s_sched.sample_done_mask = 0;
                    s_sched.ft.current_delivery_idx =
                        (s_sched.ft.current_delivery_idx + 1) %
                        s_sched.ft.delivery_count;
                    s_sched.ft.current_delivery_hour =
                        s_sched.ft.delivery_hours[s_sched.ft.current_delivery_idx];
                    s_sched.ft.current_delivery_done = 0;
                    s_sched.total_cycles++;
                }
            }
        }
    }
}

/* ======================== S3-3: 流量触发模式（增强版） ======================== */

static void sched_flow(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec;
    uint16_t cycle_min, cycle_hours;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;

    /* STOPPED/STARTUP: 等待流量信号 */
    if (s_sched.phase == PHASE_STOPPED || s_sched.phase == PHASE_STARTUP) {
        if (s_sched.fl.flow_active) {
            s_sched.phase = PHASE_STARTUP;
            s_sched.fl.startup_phase = 0;
            s_sched.fl.instant_delivery_done = 0;
            s_sched.fl.full_sampling_done = 0;
            s_sched.fl.flow_stopped = 0;
            s_sched.fl.retention_notified = 0;
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            printf("[流量触发] 流量开始，进入启动阶段\r\n");
        } else {
            if (s_sched.phase == PHASE_STARTUP)
                s_sched.phase = PHASE_STOPPED;
            return;
        }
    }

    /* 流量停止检测 */
    if (s_sched.fl.flow_stopped && s_sched.phase == PHASE_CYCLING) {
        uint16_t water;
        printf("[流量触发] 流量停止，触发送样\r\n");
        sampling_abort();
        s_sched.fl.sampling_stopped = 1;
        water = s_sched.active_bucket ? g_state.water_b : g_state.water_a;
        if (water > 0 && !delivery_is_active()) {
            s_sched.fl.flow_stopped_delivery_bucket = s_sched.active_bucket;
            s_sched.fl.waiting_delivery_complete = 1;
            delivery_start(s_sched.active_bucket, 0);
            scheduler_notify_task4_delivery(s_sched.active_bucket);
        }
        /* 通知留样任务 */
        s_sched.fl.retention_notified = 1;
        s_sched.phase = PHASE_STOPPED;
        s_sched.fl.flow_stopped = 0;
        return;
    }

    /* STARTUP: 3阶段（瞬时送样→满量采样→等整点） */
    if (s_sched.phase == PHASE_STARTUP) {
        switch (s_sched.fl.startup_phase) {
        case 0: /* 瞬时送样 */
            if (!s_sched.fl.instant_delivery_done) {
                if (!delivery_is_active()) {
                    uint16_t water = g_state.water_a;
                    if (water > 0) {
                        delivery_start(0, 0);
                        scheduler_notify_task4_delivery(0);
                    }
                    s_sched.fl.instant_delivery_done = 1;
                    s_sched.fl.startup_phase = 1;
                }
            }
            break;
        case 1: /* 满量采样(A桶) */
        {
            uint32_t expected = (1u << s_sched.sample_count) - 1;
            if (s_sched.sample_done_mask == expected) {
                s_sched.fl.full_sampling_done = 1;
                s_sched.fl.startup_phase = 2;
                break;
            }
            if (!sampling_is_active()) {
                uint8_t i;
                for (i = 0; i < s_sched.sample_count; i++) {
                    if (!(s_sched.sample_done_mask & (1u << i))) {
                        sampling_start(0, 0);
                        s_sched.sample_done_mask |= (1u << i);
                        s_sched.total_samples++;
                        break;
                    }
                }
            }
            break;
        }
        case 2: /* 等待整点 */
            if (dt.min == 0 && dt.sec == 0) {
                s_sched.cycle_start_hour = dt.hour;
                s_sched.cycle_idx = 0;
                s_sched.sample_done_mask = 0;
                s_sched.delivery_done = 0;
                s_sched.phase = PHASE_CYCLING;
                printf("[流量触发] 进入周期循环\r\n");
            }
            break;
        }
        return;
    }

    /* CYCLING: 周期循环 */
    cycle_min = g_sampling_cfg.cycle_time_min;
    cycle_hours = cycle_min / 60;
    if (cycle_hours == 0) cycle_hours = 1;

    {
        uint32_t cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
        uint32_t elapsed = (now_sec >= cycle_start_sec) ?
                           (now_sec - cycle_start_sec) :
                           (now_sec + 86400 - cycle_start_sec);
        uint32_t new_idx = elapsed / (cycle_min * 60);

        if (new_idx != s_sched.cycle_idx) {
            s_sched.cycle_idx = new_idx;
            s_sched.active_bucket ^= 1;
            s_sched.sample_done_mask = 0;
            s_sched.delivery_done = 0;
            s_sched.total_cycles++;
        }

        /* 采样触发 */
        {
            uint8_t i;
            uint32_t cycle_base = cycle_start_sec +
                                  s_sched.cycle_idx * cycle_min * 60;
            for (i = 0; i < s_sched.sample_count; i++) {
                uint32_t mask = 1u << i;
                uint32_t sample_sec;
                int32_t diff;
                if (s_sched.sample_done_mask & mask) continue;

                sample_sec = cycle_base +
                             (uint32_t)s_sched.sample_offsets[i] * 60;
                diff = (int32_t)(now_sec - sample_sec);
                if (diff < 0) diff = -diff;

                if (diff <= 30 && !sampling_is_active()) {
                    if (sampling_start(s_sched.active_bucket, 0)) {
                        s_sched.sample_done_mask |= mask;
                        s_sched.total_samples++;
                    }
                }
            }
        }

        /* 送样触发 */
        if (!s_sched.delivery_done && dt.sec == 0) {
            uint8_t delivery_hour = (s_sched.cycle_start_hour +
                (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                24 - 1) % 24;
            if (dt.hour == delivery_hour &&
                dt.min == s_sched.fl.configured_delivery_min)
            {
                uint16_t water = s_sched.active_bucket ?
                                 g_state.water_b : g_state.water_a;
                if (water > 0 && !delivery_is_active()) {
                    if (delivery_start(s_sched.active_bucket, 0)) {
                        s_sched.delivery_done = 1;
                        s_sched.total_deliveries++;
                        scheduler_notify_task4_delivery(
                            s_sched.active_bucket);
                    }
                }
            }
        }
    }
}

/* ======================== S3-4: 开关量触发模式（增强版） ======================== */

static void sched_switch(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec;
    uint16_t cycle_min;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;

    /* 等待首次GPIO信号 */
    if (s_sched.phase == PHASE_STARTUP && s_sched.sw.waiting_first_trigger) {
        extern uint8_t read_trigger_sampling_signal(void);
        if (s_sched.sw.switch_signal_received ||
            read_trigger_sampling_signal() == 0)
        {
            s_sched.sw.first_trigger_done = 1;
            s_sched.sw.first_trigger_time = g_tmr2_seconds;
            s_sched.sw.waiting_first_trigger = 0;
            s_sched.cycle_start_hour = (dt.hour + 2) % 24;
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            s_sched.sw.switch_signal_received = 0;
            s_sched.phase = PHASE_CYCLING;
            printf("[开关量] 首次触发 t=%lu, 周期起始=%02d:00\r\n",
                   (unsigned long)s_sched.sw.first_trigger_time,
                   s_sched.cycle_start_hour);
        }
        return;
    }

    if (s_sched.phase != PHASE_CYCLING) return;

    /* 采样暂停后等待信号恢复 */
    if (s_sched.sw.waiting_resume) {
        extern uint8_t read_trigger_sampling_signal(void);
        if (s_sched.sw.switch_signal_received ||
            read_trigger_sampling_signal() == 0)
        {
            s_sched.sw.waiting_resume = 0;
            s_sched.sw.sampling_stopped = 0;
            s_sched.sw.switch_signal_received = 0;
            s_sched.sw.waiting_first_trigger = 1;
            s_sched.phase = PHASE_STARTUP;
            printf("[开关量] 信号恢复，重新启动\r\n");
        }
        return;
    }

    /* 周期循环 + 窗口检测 */
    cycle_min = g_sampling_cfg.cycle_time_min;
    {
        uint32_t cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
        uint32_t elapsed = (now_sec >= cycle_start_sec) ?
                           (now_sec - cycle_start_sec) :
                           (now_sec + 86400 - cycle_start_sec);
        uint32_t new_idx = elapsed / (cycle_min * 60);

        if (new_idx != s_sched.cycle_idx) {
            s_sched.cycle_idx = new_idx;
            s_sched.active_bucket ^= 1;
            s_sched.sample_done_mask = 0;
            s_sched.delivery_done = 0;
            s_sched.total_cycles++;
            memset(s_sched.sw.window_triggered, 0,
                   sizeof(s_sched.sw.window_triggered));
        }

        /* 窗口检测采样 */
        {
            uint8_t i;
            uint32_t cycle_base = cycle_start_sec +
                                  s_sched.cycle_idx * cycle_min * 60;
            for (i = 0; i < s_sched.sample_count; i++) {
                uint32_t mask = 1u << i;
                uint32_t sample_sec;
                if (s_sched.sample_done_mask & mask) continue;

                sample_sec = cycle_base +
                    (uint32_t)s_sched.sample_offsets[i] * 60;

                /* 窗口范围: ±60秒 */
                if (now_sec >= sample_sec - 60 &&
                    now_sec <= sample_sec + 60)
                {
                    /* 窗口内检测GPIO信号 */
                    if (!s_sched.sw.window_triggered[i]) {
                        extern uint8_t read_trigger_sampling_signal(void);
                        if (s_sched.sw.switch_signal_received ||
                            read_trigger_sampling_signal() == 0)
                        {
                            s_sched.sw.window_triggered[i] = 1;
                            s_sched.sw.window_start_time[i] = g_tmr2_seconds;
                            s_sched.sw.switch_signal_received = 0;
                        }
                    }
                    /* 信号已触发则执行采样 */
                    if (s_sched.sw.window_triggered[i] &&
                        !sampling_is_active())
                    {
                        if (sampling_start(s_sched.active_bucket, 0)) {
                            s_sched.sample_done_mask |= mask;
                            s_sched.total_samples++;
                        }
                    }
                }
            }
        }

        /* 送样触发 */
        if (!s_sched.delivery_done && dt.sec == 0) {
            uint16_t ch = cycle_min / 60;
            if (ch == 0) ch = 1;
            {
                uint8_t delivery_hour = (s_sched.cycle_start_hour +
                    (uint8_t)((s_sched.cycle_idx + 1) * ch) +
                    24 - 1) % 24;
                if (dt.hour == delivery_hour &&
                    dt.min == s_sched.sw.configured_delivery_min)
                {
                    uint16_t water = s_sched.active_bucket ?
                                     g_state.water_b : g_state.water_a;
                    if (water > 0 && !delivery_is_active()) {
                        if (delivery_start(s_sched.active_bucket, 0)) {
                            s_sched.delivery_done = 1;
                            s_sched.total_deliveries++;
                            scheduler_notify_task4_delivery(
                                s_sched.active_bucket);
                        }
                    }
                }
            }
        }
    }
}

/* ======================== 通信触发模式 ======================== */

static void sched_comm(void)
{
    uint8_t bucket;
    uint16_t samples_per_cycle;

    if (s_sched.phase == PHASE_STARTUP) {
        s_sched.phase = PHASE_CYCLING;
    }
    if (!g_comm_trigger_req.pending) return;

    switch (g_comm_trigger_req.type) {
    case COMM_REQ_SAMPLING:
        bucket = (g_comm_trigger_req.bucket == 2) ?
                 s_sched.active_bucket : g_comm_trigger_req.bucket;
        if (!sampling_is_active()) {
            if (sampling_start(bucket, 0)) {
                s_sched.total_samples++;
                if (g_comm_trigger_req.bucket == 2 &&
                    g_sampling_cfg.cycle_time_min > 0 &&
                    g_sampling_cfg.interval_min > 0)
                {
                    samples_per_cycle =
                        g_sampling_cfg.cycle_time_min /
                        g_sampling_cfg.interval_min;
                    if (samples_per_cycle > 0 &&
                        (s_sched.total_samples %
                         samples_per_cycle) == 0)
                    {
                        s_sched.active_bucket ^= 1;
                        s_sched.total_cycles++;
                    }
                }
            }
        }
        break;

    case COMM_REQ_DELIVERY:
        bucket = (g_comm_trigger_req.bucket == 2) ?
                 (1 - s_sched.active_bucket) :
                 g_comm_trigger_req.bucket;
        {
            uint16_t water = bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(bucket, 0)) {
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(bucket);
                }
            }
        }
        break;

    case COMM_REQ_DRAIN:
        bucket = g_comm_trigger_req.bucket;
        if (bucket == 2) {
            drain_start(0);
        } else {
            drain_start(bucket);
        }
        break;

    default:
        break;
    }

    g_comm_trigger_req.pending = 0;
    g_comm_trigger_req.type = COMM_REQ_NONE;
}

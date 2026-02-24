/**
 * @file    app_scheduler.c
 * @brief   自动调度器 - 5种模式状态机 + 链式编排
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

/* 调度器内部状态 */
typedef struct {
    /* 公共字段 */
    sched_mode_t   mode;
    sched_phase_t  phase;
    uint8_t        running;
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

    /* 时间等比专用 */
    struct {
        uint8_t  startup_mode;   /* 0=FULL 1=SKIP 2=INSTANT */
        uint8_t  delay_active;
        uint32_t delay_end_sec;
        uint32_t anchor_sec;     /* 送样锚点(秒) */
    } tp;

    /* 定时触发专用 */
    struct {
        uint8_t  next_delivery_idx;
        uint8_t  last_check_hour;
    } ft;

    /* 流量触发专用 */
    struct {
        uint8_t  flow_active;
        uint8_t  startup_phase;  /* 0=瞬时送样 1=满量采样 2=等整点 */
        uint32_t flow_start_time;
        uint32_t flow_stop_time;
    } fl;

    /* 开关量专用 */
    struct {
        uint8_t  first_trigger_done;
        uint8_t  waiting_resume;
        uint8_t  window_triggered[MAX_SAMPLE_POINTS];
    } sw;

    /* 通信触发专用 */
    struct {
        uint32_t last_delivery_time;
    } cm;
} scheduler_state_t;

static scheduler_state_t s_sched;

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

    printf("[调度器] 初始化: 模式=%d, 周期=%d分, 间隔=%d分, 采样次数=%d\r\n",
           mode, g_sampling_cfg.cycle_time_min,
           g_sampling_cfg.interval_min, s_sched.sample_count);
}

void scheduler_start(void)
{
    if (!s_sched.running) {
        s_sched.running = 1;
        s_sched.phase = PHASE_STARTUP;
        g_state.current_mode = (uint8_t)s_sched.mode;
        g_state.current_phase = (uint8_t)s_sched.phase;
        printf("[调度器] 启动: 模式=%d\r\n", s_sched.mode);
    }
}

void scheduler_stop(void)
{
    s_sched.running = 0;
    s_sched.phase = PHASE_IDLE;
    g_state.current_phase = PHASE_IDLE;
    printf("[调度器] 停止\r\n");
}

void scheduler_run(void)
{
    if (!s_sched.running) return;

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

/* ======================== 外部事件通知 ======================== */

void scheduler_notify_flow(uint8_t active)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.flow_active = active;
    if (active) {
        s_sched.fl.flow_start_time = g_tmr2_seconds;
    } else {
        s_sched.fl.flow_stop_time = g_tmr2_seconds;
    }
}

void scheduler_notify_switch(void)
{
    if (s_sched.mode != SCHED_MODE_SWITCH) return;
    s_sched.sw.first_trigger_done = 0;
}

void scheduler_notify_comm(comm_req_type_t req, uint8_t bucket, uint16_t vol)
{
    g_comm_trigger_req.type    = req;
    g_comm_trigger_req.bucket  = bucket;
    g_comm_trigger_req.volume  = vol;
    g_comm_trigger_req.pending = 1;
}

/* Task04通知辅助 */
void scheduler_notify_task4_delivery(uint8_t bucket_id)
{
    extern void notify_task4_delivery_complete(uint8_t bucket_id);
    notify_task4_delivery_complete(bucket_id);
}

/* ======================== 时间等比模式 ======================== */

/* 分钟取整辅助 */
static uint8_t tp_round_hour(uint8_t hour, uint8_t min)
{
    if (min >= 30) return (hour + 1) % 24;
    return hour;
}

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

    /* === STARTUP阶段 === */
    if (s_sched.phase == PHASE_STARTUP) {
        uint8_t anchor_hour = tp_round_hour(dt.hour, dt.min);
        uint32_t anchor_sec = (uint32_t)anchor_hour * 3600 +
                              (uint32_t)g_delivery_cfg.start_min * 60;
        uint32_t remaining;
        if (anchor_sec <= now_sec) anchor_sec += 3600;

        remaining = anchor_sec - now_sec;
        s_sched.tp.anchor_sec = anchor_sec;

        if (remaining >= cycle_min * 60) {
            s_sched.tp.startup_mode = 0; /* FULL_SAMPLING */
            printf("[时间等比] 启动模式: FULL_SAMPLING\r\n");
        } else if (remaining < g_sampling_cfg.interval_min * 60) {
            s_sched.tp.startup_mode = 2; /* INSTANT_SAMPLING */
            printf("[时间等比] 启动模式: INSTANT_SAMPLING\r\n");
        } else {
            s_sched.tp.startup_mode = 1; /* SKIP_TO_CYCLE */
            printf("[时间等比] 启动模式: SKIP_TO_CYCLE\r\n");
        }

        s_sched.cycle_start_hour = anchor_hour;
        s_sched.cycle_idx = 0;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.active_bucket = 0;

        s_sched.phase = PHASE_CYCLING;
        printf("[时间等比] 进入周期循环: anchor=%02d:%02d, cycle_start=%02d\r\n",
               anchor_hour, g_delivery_cfg.start_min, s_sched.cycle_start_hour);
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
        s_sched.total_cycles++;
        printf("[时间等比] 新周期: idx=%lu, 桶=%c\r\n",
               (unsigned long)new_cycle_idx,
               s_sched.active_bucket ? 'B' : 'A');
    }

    /* delay机制 */
    if (s_sched.tp.delay_active) {
        if (g_tmr2_seconds < s_sched.tp.delay_end_sec) return;
        s_sched.tp.delay_active = 0;
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

            sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
            diff = (int32_t)(now_sec - sample_sec);
            if (diff < 0) diff = -diff;

            if (diff <= 30 && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                    printf("[时间等比] 采样#%d: 桶=%c\r\n",
                           i + 1, s_sched.active_bucket ? 'B' : 'A');
                }
            }
        }
    }

    /* 送样触发 */
    if (!s_sched.delivery_done && dt.sec == 0) {
        uint8_t delivery_hour = (s_sched.cycle_start_hour +
                                 (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                                 24 - 1) % 24;
        if (dt.hour == delivery_hour && dt.min == g_delivery_cfg.start_min) {
            uint16_t water = s_sched.active_bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(s_sched.active_bucket, 0)) {
                    s_sched.delivery_done = 1;
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(s_sched.active_bucket);
                    printf("[时间等比] 送样: 桶=%c, 水量=%u\r\n",
                           s_sched.active_bucket ? 'B' : 'A', water);
                }
            }
        }
    }
}

/* ======================== 占位模式（批次3实现） ======================== */

static void sched_fixed_time(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec;
    uint8_t h, i;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;

    if (s_sched.phase == PHASE_STARTUP) {
        s_sched.phase = PHASE_CYCLING;
        s_sched.active_bucket = 0;
        s_sched.ft.last_check_hour = 0xFF;
        printf("[定时触发] 直接进入周期循环\r\n");
        return;
    }
    if (s_sched.phase != PHASE_CYCLING) return;

    for (h = 0; h < 24; h++) {
        if (!g_delivery_cfg.fixedhour[h]) continue;

        {
            uint8_t fmin = g_delivery_cfg.fixedmin;
            uint8_t cycle_start_h = (fmin >= 50) ? h : ((h + 23) % 24);
            uint32_t cycle_base = (uint32_t)cycle_start_h * 3600;

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

            if (dt.hour == h && dt.min == fmin && dt.sec == 0 &&
                !s_sched.delivery_done)
            {
                uint16_t water = s_sched.active_bucket ?
                                 g_state.water_b : g_state.water_a;
                if (water > 0 && !delivery_is_active()) {
                    if (delivery_start(s_sched.active_bucket, 0)) {
                        s_sched.delivery_done = 1;
                        s_sched.total_deliveries++;
                        scheduler_notify_task4_delivery(s_sched.active_bucket);
                        s_sched.active_bucket ^= 1;
                        s_sched.sample_done_mask = 0;
                        s_sched.delivery_done = 0;
                        s_sched.total_cycles++;
                    }
                }
            }
        }
    }
}

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
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            printf("[流量触发] 流量开始，进入启动阶段\r\n");
        } else {
            if (s_sched.phase == PHASE_STARTUP) {
                s_sched.phase = PHASE_STOPPED;
            }
            return;
        }
    }

    /* 流量停止检测 */
    if (!s_sched.fl.flow_active && s_sched.phase == PHASE_CYCLING) {
        uint16_t water;
        printf("[流量触发] 流量停止\r\n");
        sampling_abort();
        water = s_sched.active_bucket ? g_state.water_b : g_state.water_a;
        if (water > 0 && !delivery_is_active()) {
            delivery_start(s_sched.active_bucket, 0);
            scheduler_notify_task4_delivery(0xFF);
        }
        s_sched.phase = PHASE_STOPPED;
        return;
    }

    /* STARTUP: 3阶段 */
    if (s_sched.phase == PHASE_STARTUP) {
        switch (s_sched.fl.startup_phase) {
        case 0: /* 瞬时送样 */
            if (!delivery_is_active()) {
                uint16_t water = g_state.water_a;
                if (water > 0) {
                    delivery_start(0, 0);
                    scheduler_notify_task4_delivery(0);
                }
                s_sched.fl.startup_phase = 1;
            }
            break;
        case 1: /* 满量采样(A桶) */
        {
            uint32_t expected = (1u << s_sched.sample_count) - 1;
            if (s_sched.sample_done_mask == expected) {
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
            uint32_t cycle_base = cycle_start_sec + s_sched.cycle_idx * cycle_min * 60;
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
        }

        /* 送样触发 */
        if (!s_sched.delivery_done && dt.sec == 0) {
            uint8_t delivery_hour = (s_sched.cycle_start_hour +
                                     (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                                     24 - 1) % 24;
            if (dt.hour == delivery_hour && dt.min == g_delivery_cfg.start_min) {
                uint16_t water = s_sched.active_bucket ?
                                 g_state.water_b : g_state.water_a;
                if (water > 0 && !delivery_is_active()) {
                    if (delivery_start(s_sched.active_bucket, 0)) {
                        s_sched.delivery_done = 1;
                        s_sched.total_deliveries++;
                        scheduler_notify_task4_delivery(s_sched.active_bucket);
                    }
                }
            }
        }
    }
}

static void sched_switch(void)
{
    rtc_datetime_t dt;
    uint32_t now_sec;
    uint16_t cycle_min;

    rtc_get_time(&dt);
    now_sec = (uint32_t)dt.hour * 3600 + (uint32_t)dt.min * 60 + dt.sec;

    /* 等待首次GPIO信号 */
    if (s_sched.phase == PHASE_STARTUP && !s_sched.sw.first_trigger_done) {
        extern uint8_t read_trigger_sampling_signal(void);
        if (read_trigger_sampling_signal() == 0) {
            s_sched.sw.first_trigger_done = 1;
            s_sched.cycle_start_hour = (dt.hour + 2) % 24;
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            s_sched.phase = PHASE_CYCLING;
            printf("[开关量] 首次触发，等待整点进入周期\r\n");
        }
        return;
    }

    if (s_sched.phase != PHASE_CYCLING) return;

    /* 恢复等待 */
    if (s_sched.sw.waiting_resume) {
        extern uint8_t read_trigger_sampling_signal(void);
        if (read_trigger_sampling_signal() == 0) {
            s_sched.sw.waiting_resume = 0;
            s_sched.sw.first_trigger_done = 0;
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
            memset(s_sched.sw.window_triggered, 0, sizeof(s_sched.sw.window_triggered));
        }

        /* 窗口检测采样 */
        {
            uint8_t i;
            uint32_t cycle_base = cycle_start_sec + s_sched.cycle_idx * cycle_min * 60;
            for (i = 0; i < s_sched.sample_count; i++) {
                uint32_t mask = 1u << i;
                uint32_t sample_sec;
                if (s_sched.sample_done_mask & mask) continue;

                sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
                if (now_sec >= sample_sec - 60 && now_sec <= sample_sec + 60) {
                    if (!s_sched.sw.window_triggered[i]) {
                        extern uint8_t read_trigger_sampling_signal(void);
                        if (read_trigger_sampling_signal() == 0) {
                            s_sched.sw.window_triggered[i] = 1;
                        }
                    }
                    if (s_sched.sw.window_triggered[i] && !sampling_is_active()) {
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
            uint16_t cycle_hours = cycle_min / 60;
            if (cycle_hours == 0) cycle_hours = 1;
            {
                uint8_t delivery_hour = (s_sched.cycle_start_hour +
                                         (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                                         24 - 1) % 24;
                if (dt.hour == delivery_hour && dt.min == g_delivery_cfg.start_min) {
                    uint16_t water = s_sched.active_bucket ?
                                     g_state.water_b : g_state.water_a;
                    if (water > 0 && !delivery_is_active()) {
                        if (delivery_start(s_sched.active_bucket, 0)) {
                            s_sched.delivery_done = 1;
                            s_sched.total_deliveries++;
                            scheduler_notify_task4_delivery(s_sched.active_bucket);
                        }
                    }
                }
            }
        }
    }
}

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
                    samples_per_cycle = g_sampling_cfg.cycle_time_min /
                                        g_sampling_cfg.interval_min;
                    if (samples_per_cycle > 0 &&
                        (s_sched.total_samples % samples_per_cycle) == 0)
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
                 (1 - s_sched.active_bucket) : g_comm_trigger_req.bucket;
        {
            uint16_t water = bucket ? g_state.water_b : g_state.water_a;
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

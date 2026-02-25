/**
 * @file    app_retain_judge.c
 * @brief   留样判定模块 — 7种留样模式判定 + 执行接口
 */
#include "app_retain_judge.h"
#include "app_config.h"
#include "app_adc_module.h"
#include "app_screen_cache.h"
#include "app_sampling.h"
#include "app_flashdb.h"
#include "bsp_io.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* Task04心跳事件位 */
#define TASK04_HB_BIT  (1 << 2)

/* 外部变量 */
extern EventGroupHandle_t my_event01_handle;
extern volatile uint32_t  g_tmr2_seconds;

/* ======================== 内部状态 ======================== */
typedef struct {
    uint8_t  over_state[6];      /* 6通道超标状态 */
    uint8_t  switch_triggered;   /* 开关量触发标志 */
    uint8_t  modbus_triggered;   /* Modbus触发标志 */
    uint8_t  bottle_current;     /* 当前瓶位(0-23) */
    /* 统计计数 */
    uint16_t analog_count;
    uint16_t flow_count;
    uint16_t switch_count;
    uint16_t modbus_count;
} retain_judge_state_t;

static retain_judge_state_t s_judge;

/* ======================== 内部函数声明 ======================== */
static uint8_t check_analog_alarm(void);

/* ======================== 初始化 ======================== */
void retain_judge_init(void)
{
    memset(&s_judge, 0, sizeof(s_judge));
    s_judge.bottle_current = g_state.bottle_current;
    printf("[留样判定] 初始化完成, 当前瓶位=%d\r\n",
           (int)s_judge.bottle_current);
}

/* ======================== 模拟量超标检查 ======================== */
static uint8_t check_analog_alarm(void)
{
    uint8_t ch;
    for (ch = 0; ch < ADC_MOD_CH_COUNT; ch++) {
        float ma;
        if (!g_ch_limits[ch].enable) {
            continue;
        }
        ma = adc_module_get_ma(ch);
        if (ma < g_ch_limits[ch].lower_limit ||
            ma > g_ch_limits[ch].upper_limit) {
            if (!s_judge.over_state[ch]) {
                s_judge.over_state[ch] = 1;
                printf("[留样判定] CH%d超标: %.2fmA "
                       "(限值%.2f~%.2f)\r\n",
                       (int)ch, (double)ma,
                       (double)g_ch_limits[ch].lower_limit,
                       (double)g_ch_limits[ch].upper_limit);
                return 1;
            }
        } else {
            s_judge.over_state[ch] = 0;
        }
    }
    return 0;
}

/* ======================== 综合留样判定 ======================== */
uint8_t retain_judge_commit(uint8_t bucket, uint32_t now_sec)
{
    (void)now_sec;
    (void)bucket;

    /* 留样功能未启用 */
    if (!g_retain_cfg.enable) {
        return 0;
    }

    /* 优先检查Modbus触发标志 */
    if (s_judge.modbus_triggered) {
        s_judge.modbus_triggered = 0;
        s_judge.modbus_count++;
        printf("[留样判定] Modbus触发已确认\r\n");
        return 1;
    }

    switch (g_retain_cfg.mode) {
    case RETAIN_MODE_ALARM:   /* 超标留样 */
    case RETAIN_MODE_SYNC:    /* 同步留样(同ALARM) */
        if (check_analog_alarm()) { s_judge.analog_count++; return 1; }
        return 0;

    case RETAIN_MODE_DIRECT:  /* 直接留样 */
        return 1;

    case RETAIN_MODE_COMPARE: /* 比对留样 */
        return check_analog_alarm();

    case RETAIN_MODE_MODBUS:  /* 通讯触发(由work直接执行) */
        return 0;

    case RETAIN_MODE_NEVER:   /* 不留样 */
        return 0;

    case RETAIN_MODE_SWITCH:  /* 开关量留样 */
        if (s_judge.switch_triggered) {
            s_judge.switch_triggered = 0;
            s_judge.switch_count++;
            printf("[留样判定] 开关量触发已确认\r\n");
            return 1;
        }
        return 0;

    default:
        return 0;
    }
}

/* ======================== 重置判定状态 ======================== */
void retain_judge_reset_state(void)
{
    uint8_t i;
    for (i = 0; i < 6; i++) {
        s_judge.over_state[i] = 0;
    }
    s_judge.switch_triggered = 0;
    s_judge.modbus_triggered = 0;
}

/* ======================== 外部事件通知 ======================== */
void retain_judge_notify_switch(void)
{
    s_judge.switch_triggered = 1;
    printf("[留样判定] 收到开关量触发信号\r\n");
}

void retain_judge_notify_modbus(void)
{
    s_judge.modbus_triggered = 1;
    printf("[留样判定] 收到Modbus触发信号\r\n");
}

/* ======================== 瓶位管理 ======================== */
uint8_t retain_get_next_bottle(void)
{
    return s_judge.bottle_current;
}

void retain_advance_bottle(void)
{
    uint8_t count = g_retain_cfg.bottle_count;
    if (count == 0) count = 24;

    /* 标记当前瓶已使用 */
    uint8_t bottle_1based = s_judge.bottle_current + 1;
    if (bottle_1based >= 1 && bottle_1based <= 24) {
        g_retain_bottle_state.used_mask |= (1u << (bottle_1based - 1));
    }

    /* 推进到下一瓶 */
    s_judge.bottle_current++;
    if (s_judge.bottle_current >= count) {
        s_judge.bottle_current = 0;
    }
    g_state.bottle_current = s_judge.bottle_current;
    g_retain_bottle_state.current_bottle = s_judge.bottle_current + 1;

    /* 持久化瓶位状态 */
    kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);

    printf("[留样判定] 瓶位推进到 %d, mask=0x%08X\r\n",
           (int)s_judge.bottle_current,
           (unsigned)g_retain_bottle_state.used_mask);
}

/* ======================== 阻塞执行留样 ======================== */
void retention_execute(uint8_t bucket, uint32_t now_sec)
{
    uint8_t bottle;
    (void)now_sec;

    bottle = retain_get_next_bottle();
    printf("[留样执行] 桶%c -> 瓶位%d\r\n",
           (bucket == 0) ? 'A' : 'B', (int)bottle);

    /* 启动留样状态机 */
    if (!retain_start(bottle, 0)) {
        printf("[留样执行] 启动失败\r\n");
        return;
    }

    /* 阻塞等待留样完成 */
    while (retain_is_active()) {
        xEventGroupSetBits(my_event01_handle, TASK04_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    /* 写入留样记录 */
    {
        RetainSampleLogData rlog;
        rlog.trigger_source = 0;
        rlog.bottle_id = bottle;
        rlog.retain_volume = 0;
        rlog.success = 1;
        rlog.acid_added = 0;
        tsdb_event_append(EVT_RETAIN_DONE, &rlog, sizeof(rlog));
    }

    /* 推进瓶位 */
    retain_advance_bottle();
    printf("[留样执行] 完成\r\n");
}

/* ======================== 阻塞执行排水 ======================== */
void drain_execute_blocking(uint8_t bucket)
{
    printf("[排水执行] 桶%c 开始\r\n",
           (bucket == 0) ? 'A' : 'B');

    if (!drain_start(bucket)) {
        printf("[排水执行] 启动失败\r\n");
        return;
    }

    /* 阻塞等待排水完成 */
    while (drain_is_active()) {
        xEventGroupSetBits(my_event01_handle, TASK04_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("[排水执行] 桶%c 完成\r\n",
           (bucket == 0) ? 'A' : 'B');
}

/* ======================== 瓶位管理（集成RetainBottleState） ======================== */
void retain_clear_all_bottles(void)
{
    g_retain_bottle_state.used_mask = 0;
    g_retain_bottle_state.current_bottle = 1;
    s_judge.bottle_current = 0;
    g_state.bottle_current = 0;
    cfg_save_retain_state(&g_retain_bottle_state);
    kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);
    printf("[留样判定] 所有瓶位已清空\r\n");
}

uint8_t retain_get_bottle_status(uint8_t bottle_1based)
{
    if (bottle_1based < 1 || bottle_1based > 24) return 0;
    return (g_retain_bottle_state.used_mask >> (bottle_1based - 1)) & 1;
}

/* ======================== 屏幕电流推送 ======================== */
void retain_send_current_values_to_screen(void)
{
    /* 将各通道mA值通过屏幕缓存推送（由屏幕任务定期调用） */
    uint8_t ch;
    for (ch = 0; ch < ADC_MOD_CH_COUNT; ch++) {
        float ma = adc_module_get_ma(ch);
        (void)ma; /* 实际推送逻辑在S4屏幕增强时实现 */
    }
}

/* ======================== 统计查询 ======================== */
void retain_judge_get_stats(RetainJudgeStats *out)
{
    if (out) {
        out->analog_count = s_judge.analog_count;
        out->flow_count   = s_judge.flow_count;
        out->switch_count = s_judge.switch_count;
        out->modbus_count = s_judge.modbus_count;
    }
}

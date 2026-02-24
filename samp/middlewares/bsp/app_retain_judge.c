/**
 * @file    app_retain_judge.c
 * @brief   留样判定模块 — 7种留样模式判定 + 执行接口
 */
#include "app_retain_judge.h"
#include "app_config.h"
#include "app_adc_module.h"
#include "app_sampling.h"
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
        printf("[留样判定] Modbus触发已确认\r\n");
        return 1;
    }

    switch (g_retain_cfg.mode) {
    case RETAIN_MODE_ALARM:   /* 超标留样 */
    case RETAIN_MODE_SYNC:    /* 同步留样(同ALARM) */
        return check_analog_alarm();

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
    if (count == 0) {
        count = 24;
    }
    s_judge.bottle_current++;
    if (s_judge.bottle_current >= count) {
        s_judge.bottle_current = 0;
    }
    g_state.bottle_current = s_judge.bottle_current;
    printf("[留样判定] 瓶位推进到 %d\r\n",
           (int)s_judge.bottle_current);
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

/**
 * @file    app_retain_judge.h
 * @brief   留样判定模块 — 7种留样模式判定 + 执行接口
 */
#ifndef APP_RETAIN_JUDGE_H
#define APP_RETAIN_JUDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 留样模式 */
typedef enum {
    RETAIN_MODE_ALARM   = 0,  /* 超标留样 */
    RETAIN_MODE_DIRECT  = 1,  /* 直接留样 */
    RETAIN_MODE_COMPARE = 2,  /* 比对留样 */
    RETAIN_MODE_MODBUS  = 3,  /* 通讯触发留样 */
    RETAIN_MODE_SYNC    = 4,  /* 同步留样 */
    RETAIN_MODE_NEVER   = 5,  /* 不留样 */
    RETAIN_MODE_SWITCH  = 6,  /* 开关量留样 */
} retain_mode_t;

/* 初始化 */
void retain_judge_init(void);

/* 判定是否需要留样（在分析窗口内周期调用）
 * @param bucket   桶号(0=A, 1=B)
 * @param now_sec  当前秒计数
 * @return 1=需要留样, 0=不需要
 */
uint8_t retain_judge_commit(uint8_t bucket, uint32_t now_sec);

/* 重置判定状态（每次留样/排水完成后调用） */
void retain_judge_reset_state(void);

/* 外部事件通知 */
void retain_judge_notify_switch(void);   /* 开关量信号 */
void retain_judge_notify_modbus(void);   /* Modbus触发 */

/* 阻塞执行留样流程（在Task04上下文中调用）
 * @param bucket   桶号
 * @param now_sec  当前秒计数
 */
void retention_execute(uint8_t bucket, uint32_t now_sec);

/* 阻塞执行排水（在Task04上下文中调用） */
void drain_execute_blocking(uint8_t bucket);

/* 瓶位管理 */
uint8_t retain_get_next_bottle(void);
void    retain_advance_bottle(void);
void    retain_clear_all_bottles(void);
uint8_t retain_get_bottle_status(uint8_t bottle_1based);

/* 屏幕推送 */
void    retain_send_current_values_to_screen(void);

/* 统计 */
typedef struct {
    uint16_t analog_count;
    uint16_t flow_count;
    uint16_t switch_count;
    uint16_t modbus_count;
} RetainJudgeStats;

void    retain_judge_get_stats(RetainJudgeStats *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_RETAIN_JUDGE_H */

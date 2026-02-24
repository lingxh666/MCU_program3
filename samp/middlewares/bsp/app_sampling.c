/**
 * @file    app_sampling.c
 * @brief   采样/排水非阻塞状态机实现
 *
 * 参考 samplingB 的 SamplingContext + _sampling_step 分发模式
 * 定时使用 xTaskGetTickCount()，50ms轮询周期由 Task02 保证
 */
#include "app_sampling.h"
#include "app_config.h"
#include "bsp_io.h"
#include "bsp_can_motor.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>

/* ======================== 采样上下文 ======================== */
static struct {
    samp_stage_t stage;
    uint8_t  bucket_id;       /* 0=A 1=B */
    uint8_t  is_manual;
    uint16_t blowback_sec;    /* 参数快照 */
    uint16_t improve_sec;
    uint16_t tube_hold_sec;
    uint16_t measure_sec;
    uint16_t rpm;
    uint32_t stage_start;     /* 阶段开始tick */
    uint8_t  result;          /* 0=fail 1=ok 2=abort */
} s_samp;

/* ======================== 排水上下文 ======================== */
static struct {
    drain_stage_t stage;
    uint8_t  bucket_id;
    uint16_t drain_sec;       /* 排水时长 */
    uint32_t stage_start;
} s_drain;

/* ======================== 辅助宏 ======================== */
#define ELAPSED_SEC(start)  ((xTaskGetTickCount() - (start)) / configTICK_RATE_HZ)
#define ELAPSED_MS(start)   ((xTaskGetTickCount() - (start)) * 1000 / configTICK_RATE_HZ)
#define NOW_TICK()          xTaskGetTickCount()

/* 默认排水时长(秒) */
#define DEFAULT_DRAIN_SEC   120
/* 阶段间延时(ms) */
#define STAGE_DELAY_MS      500

/* ================================================================
 *  采样状态机
 * ================================================================ */

uint8_t sampling_start(uint8_t bucket, uint8_t is_manual)
{
    if (s_samp.stage != SAMP_IDLE && s_samp.stage != SAMP_DONE
        && s_samp.stage != SAMP_ABORT)
        return 0;  /* 正在运行 */

    /* 快照配置参数 */
    s_samp.bucket_id    = bucket;
    s_samp.is_manual    = is_manual;
    s_samp.blowback_sec = g_sampling_cfg.blowback_sec;
    s_samp.improve_sec  = g_sampling_cfg.improve_sec;
    s_samp.tube_hold_sec= g_sampling_cfg.tube_hold_sec;
    s_samp.measure_sec  = 0;  /* 由体积/转速计算，暂用blowback_sec代替 */
    s_samp.rpm          = g_sampling_cfg.motor_rpm;
    s_samp.result       = 0;

    /* 开进水阀 */
    INLET_VALVE_ON();

    /* 进入前反吹阶段 */
    s_samp.stage       = SAMP_PRE_BLOW;
    s_samp.stage_start = NOW_TICK();

    /* 启动采样电机反转(反吹) */
    can_motor_set_speed(MOTOR_ID_SAMPLING, s_samp.rpm, MOTOR_DIR_CCW);
    can_motor_start(MOTOR_ID_SAMPLING);

    printf("[采样] 启动 桶%c %s blowback=%us\r\n",
           bucket ? 'B' : 'A', is_manual ? "手动" : "自动",
           s_samp.blowback_sec);

    /* 更新桶状态 */
    if (bucket == 0) g_state.bucket_a_state = BUCKET_SAMPLING;
    else             g_state.bucket_b_state = BUCKET_SAMPLING;

    return 1;
}

void sampling_step(void)
{
    if (s_samp.stage == SAMP_IDLE || s_samp.stage == SAMP_DONE
        || s_samp.stage == SAMP_ABORT)
        return;

    switch (s_samp.stage) {
    case SAMP_PRE_BLOW:
        /* 采样电机反转(反吹)，等待 blowback_sec */
        if (ELAPSED_SEC(s_samp.stage_start) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_DELAY_AFTER_PRE;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 前反吹完成 → 延时\r\n");
        }
        break;

    case SAMP_DELAY_AFTER_PRE:
        /* 500ms延时 */
        if (ELAPSED_MS(s_samp.stage_start) >= STAGE_DELAY_MS) {
            /* 开外接泵 */
            EXT_PUMP_ON();
            s_samp.stage = SAMP_IMPROVE;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 外接泵提升开始 %us\r\n", s_samp.improve_sec);
        }
        break;

    case SAMP_IMPROVE:
        /* 外接泵提升，等待 improve_sec */
        if (ELAPSED_SEC(s_samp.stage_start) >= s_samp.improve_sec) {
            EXT_PUMP_OFF();
            s_samp.stage = SAMP_TUBE_HOLD;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 提升完成 → 管存 %us\r\n", s_samp.tube_hold_sec);
        }
        break;

    case SAMP_TUBE_HOLD:
        /* 管存静置，等待 tube_hold_sec */
        if (ELAPSED_SEC(s_samp.stage_start) >= s_samp.tube_hold_sec) {
            /* 启动采样电机正转(计量采样) */
            can_motor_set_speed(MOTOR_ID_SAMPLING, s_samp.rpm, MOTOR_DIR_CW);
            can_motor_start(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_MEASURE;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 计量采样开始\r\n");
        }
        break;

    case SAMP_MEASURE:
        /* 采样电机正转计量，等待 blowback_sec 作为临时计量时间 */
        if (ELAPSED_SEC(s_samp.stage_start) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_DELAY_AFTER_MEAS;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 计量完成 → 延时\r\n");
        }
        break;

    case SAMP_DELAY_AFTER_MEAS:
        /* 500ms延时 */
        if (ELAPSED_MS(s_samp.stage_start) >= STAGE_DELAY_MS) {
            /* 启动采样电机反转(后反吹) */
            can_motor_set_speed(MOTOR_ID_SAMPLING, s_samp.rpm, MOTOR_DIR_CCW);
            can_motor_start(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_POST_BLOW;
            s_samp.stage_start = NOW_TICK();
            printf("[采样] 后反吹开始 %us\r\n", s_samp.blowback_sec);
        }
        break;

    case SAMP_POST_BLOW:
        /* 后反吹，等待 blowback_sec */
        if (ELAPSED_SEC(s_samp.stage_start) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            INLET_VALVE_OFF();
            s_samp.result = 1;  /* 成功 */
            s_samp.stage = SAMP_DONE;
            /* 恢复桶状态 */
            if (s_samp.bucket_id == 0) g_state.bucket_a_state = BUCKET_IDLE;
            else                       g_state.bucket_b_state = BUCKET_IDLE;
            printf("[采样] 完成 桶%c\r\n", s_samp.bucket_id ? 'B' : 'A');
        }
        break;

    default:
        break;
    }
}

uint8_t sampling_is_active(void)
{
    return (s_samp.stage != SAMP_IDLE && s_samp.stage != SAMP_DONE
            && s_samp.stage != SAMP_ABORT) ? 1 : 0;
}

uint8_t sampling_get_result(void)
{
    return s_samp.result;
}

void sampling_abort(void)
{
    if (!sampling_is_active()) return;
    can_motor_stop(MOTOR_ID_SAMPLING);
    EXT_PUMP_OFF();
    INLET_VALVE_OFF();
    s_samp.result = 2;
    s_samp.stage = SAMP_ABORT;
    if (s_samp.bucket_id == 0) g_state.bucket_a_state = BUCKET_IDLE;
    else                       g_state.bucket_b_state = BUCKET_IDLE;
    printf("[采样] 中止 桶%c\r\n", s_samp.bucket_id ? 'B' : 'A');
}

/* ================================================================
 *  排水状态机
 * ================================================================ */

uint8_t drain_start(uint8_t bucket)
{
    if (s_drain.stage != DRAIN_IDLE && s_drain.stage != DRAIN_DONE)
        return 0;

    s_drain.bucket_id  = bucket;
    s_drain.drain_sec  = DEFAULT_DRAIN_SEC;
    s_drain.stage      = DRAIN_OPEN_VALVE;
    s_drain.stage_start = NOW_TICK();

    /* 开排水阀 */
    if (bucket == 0) DRAIN_A_ON();
    else             DRAIN_B_ON();

    /* 更新桶状态 */
    if (bucket == 0) g_state.bucket_a_state = BUCKET_DRAINING;
    else             g_state.bucket_b_state = BUCKET_DRAINING;

    printf("[排水] 启动 桶%c %us\r\n", bucket ? 'B' : 'A', s_drain.drain_sec);
    return 1;
}

void drain_step(void)
{
    if (s_drain.stage == DRAIN_IDLE || s_drain.stage == DRAIN_DONE)
        return;

    switch (s_drain.stage) {
    case DRAIN_OPEN_VALVE:
        /* 开搅拌电机(可选) */
        if (s_drain.bucket_id == 0) STIR_A_ON();
        else                        STIR_B_ON();
        s_drain.stage = DRAIN_MIXING;
        s_drain.stage_start = NOW_TICK();
        printf("[排水] 搅拌开始\r\n");
        break;

    case DRAIN_MIXING:
        /* 搅拌5秒后进入等待排空 */
        if (ELAPSED_SEC(s_drain.stage_start) >= 5) {
            s_drain.stage = DRAIN_WAIT;
            s_drain.stage_start = NOW_TICK();
            printf("[排水] 等待排空 %us\r\n", s_drain.drain_sec);
        }
        break;

    case DRAIN_WAIT:
        /* 等待排水时长 */
        if (ELAPSED_SEC(s_drain.stage_start) >= s_drain.drain_sec) {
            /* 关阀 + 停搅拌 */
            if (s_drain.bucket_id == 0) {
                DRAIN_A_OFF();
                STIR_A_OFF();
                g_state.bucket_a_state = BUCKET_IDLE;
            } else {
                DRAIN_B_OFF();
                STIR_B_OFF();
                g_state.bucket_b_state = BUCKET_IDLE;
            }
            s_drain.stage = DRAIN_DONE;
            printf("[排水] 完成 桶%c\r\n", s_drain.bucket_id ? 'B' : 'A');
        }
        break;

    default:
        break;
    }
}

uint8_t drain_is_active(void)
{
    return (s_drain.stage != DRAIN_IDLE && s_drain.stage != DRAIN_DONE) ? 1 : 0;
}

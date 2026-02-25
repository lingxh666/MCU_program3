/**
 * @file    app_sampling.c
 * @brief   采样/排水非阻塞状态机实现
 *
 * 参考 samplingB 的 SamplingContext + _sampling_step 分发模式
 * 秒级定时使用 g_tmr2_seconds (TMR2, 1Hz)
 * 毫秒级定时使用 g_tmr4_milliseconds (TMR4, 1kHz)
 */
#include "app_sampling.h"
#include "app_config.h"
#include "bsp_io.h"
#include "bsp_can_motor.h"
#include "bsp_timer.h"
#include "app_flashdb.h"
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
    uint32_t stage_start_sec; /* 秒级阶段开始 (g_tmr2_seconds) */
    uint32_t delay_start_ms;  /* 毫秒级延时开始 (g_tmr4_milliseconds) */
    uint8_t  result;          /* 0=fail 1=ok 2=abort */
} s_samp;

/* ======================== 排水上下文 ======================== */
static struct {
    drain_stage_t stage;
    uint8_t  bucket_id;
    uint16_t drain_sec;       /* 排水时长 */
    uint32_t stage_start_sec; /* 秒级阶段开始 */
} s_drain;

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
    s_samp.stage          = SAMP_PRE_BLOW;
    s_samp.stage_start_sec = g_tmr2_seconds;

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
        if ((g_tmr2_seconds - s_samp.stage_start_sec) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_DELAY_AFTER_PRE;
            s_samp.delay_start_ms = g_tmr4_milliseconds;
            printf("[采样] 前反吹完成 → 延时\r\n");
        }
        break;

    case SAMP_DELAY_AFTER_PRE:
        /* 500ms延时 */
        if ((g_tmr4_milliseconds - s_samp.delay_start_ms) >= STAGE_DELAY_MS) {
            /* 开外接泵 */
            EXT_PUMP_ON();
            s_samp.stage = SAMP_IMPROVE;
            s_samp.stage_start_sec = g_tmr2_seconds;
            printf("[采样] 外接泵提升开始 %us\r\n", s_samp.improve_sec);
        }
        break;

    case SAMP_IMPROVE:
        /* 外接泵提升，等待 improve_sec */
        if ((g_tmr2_seconds - s_samp.stage_start_sec) >= s_samp.improve_sec) {
            EXT_PUMP_OFF();
            s_samp.stage = SAMP_TUBE_HOLD;
            s_samp.stage_start_sec = g_tmr2_seconds;
            printf("[采样] 提升完成 → 管存 %us\r\n", s_samp.tube_hold_sec);
        }
        break;

    case SAMP_TUBE_HOLD:
        /* 管存静置，等待 tube_hold_sec */
        if ((g_tmr2_seconds - s_samp.stage_start_sec) >= s_samp.tube_hold_sec) {
            /* 启动采样电机正转(计量采样) */
            can_motor_set_speed(MOTOR_ID_SAMPLING, s_samp.rpm, MOTOR_DIR_CW);
            can_motor_start(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_MEASURE;
            s_samp.stage_start_sec = g_tmr2_seconds;
            printf("[采样] 计量采样开始\r\n");
        }
        break;

    case SAMP_MEASURE:
        /* 采样电机正转计量，等待 blowback_sec 作为临时计量时间 */
        if ((g_tmr2_seconds - s_samp.stage_start_sec) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_DELAY_AFTER_MEAS;
            s_samp.delay_start_ms = g_tmr4_milliseconds;
            printf("[采样] 计量完成 → 延时\r\n");
        }
        break;

    case SAMP_DELAY_AFTER_MEAS:
        /* 500ms延时 */
        if ((g_tmr4_milliseconds - s_samp.delay_start_ms) >= STAGE_DELAY_MS) {
            /* 启动采样电机反转(后反吹) */
            can_motor_set_speed(MOTOR_ID_SAMPLING, s_samp.rpm, MOTOR_DIR_CCW);
            can_motor_start(MOTOR_ID_SAMPLING);
            s_samp.stage = SAMP_POST_BLOW;
            s_samp.stage_start_sec = g_tmr2_seconds;
            printf("[采样] 后反吹开始 %us\r\n", s_samp.blowback_sec);
        }
        break;

    case SAMP_POST_BLOW:
        /* 后反吹，等待 blowback_sec */
        if ((g_tmr2_seconds - s_samp.stage_start_sec) >= s_samp.blowback_sec) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            INLET_VALVE_OFF();
            s_samp.result = 1;  /* 成功 */
            /* 写入采样记录 */
            {
                SampleLogData log;
                log.trigger_source = s_samp.is_manual ? 0 : 1;
                log.bucket_id = (uint8_t)(s_samp.bucket_id + 1);
                log.sample_volume = 0;  /* 暂无流量计 */
                log.result = s_samp.result;
                tsdb_event_append(EVT_SAMPLE_DONE, &log, sizeof(log));
            }
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
    s_drain.stage_start_sec = g_tmr2_seconds;

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
        s_drain.stage_start_sec = g_tmr2_seconds;
        printf("[排水] 搅拌开始\r\n");
        break;

    case DRAIN_MIXING:
        /* 搅拌5秒后进入等待排空 */
        if ((g_tmr2_seconds - s_drain.stage_start_sec) >= 5) {
            s_drain.stage = DRAIN_WAIT;
            s_drain.stage_start_sec = g_tmr2_seconds;
            printf("[排水] 等待排空 %us\r\n", s_drain.drain_sec);
        }
        break;

    case DRAIN_WAIT:
        /* 等待排水时长 */
        if ((g_tmr2_seconds - s_drain.stage_start_sec) >= s_drain.drain_sec) {
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

/* ================================================================
 *  送样状态机
 * ================================================================ */

static struct {
    deliv_stage_t stage;
    uint8_t  bucket_id;
    uint8_t  is_manual;
    uint16_t blowback_sec;
    uint16_t deliver_sec;     /* 送样计量时长 */
    uint16_t backdraw_sec;
    uint16_t rpm;
    uint32_t stage_start_sec;
    uint32_t delay_start_ms;
    uint8_t  result;          /* 0=fail 1=ok 2=abort */
} s_deliv;

uint8_t delivery_start(uint8_t bucket, uint8_t is_manual)
{
    if (s_deliv.stage != DELIV_IDLE && s_deliv.stage != DELIV_DONE
        && s_deliv.stage != DELIV_ABORT)
        return 0;

    s_deliv.bucket_id    = bucket;
    s_deliv.is_manual    = is_manual;
    s_deliv.blowback_sec = g_sampling_cfg.blowback_sec;
    s_deliv.deliver_sec  = g_sampling_cfg.blowback_sec;  /* 临时：用反吹时长代替 */
    s_deliv.backdraw_sec = g_delivery_cfg.backdraw_sec;
    s_deliv.rpm          = g_delivery_cfg.motor_rpm;
    s_deliv.result       = 0;

    /* 开送留样阀(送样方向) */
    DELIVER_VALVE_ON();

    /* 进入前反吹 */
    s_deliv.stage = DELIV_PRE_BLOW;
    s_deliv.stage_start_sec = g_tmr2_seconds;

    /* 送样电机反转(清线) */
    can_motor_set_speed(MOTOR_ID_DELIVERY, s_deliv.rpm, MOTOR_DIR_CCW);
    can_motor_start(MOTOR_ID_DELIVERY);

    printf("[送样] 启动 桶%c %s\r\n",
           bucket ? 'B' : 'A', is_manual ? "手动" : "自动");

    if (bucket == 0) g_state.bucket_a_state = BUCKET_DELIVERY;
    else             g_state.bucket_b_state = BUCKET_DELIVERY;

    return 1;
}

void delivery_step(void)
{
    if (s_deliv.stage == DELIV_IDLE || s_deliv.stage == DELIV_DONE
        || s_deliv.stage == DELIV_ABORT)
        return;

    switch (s_deliv.stage) {
    case DELIV_PRE_BLOW:
        if ((g_tmr2_seconds - s_deliv.stage_start_sec) >= s_deliv.blowback_sec) {
            can_motor_stop(MOTOR_ID_DELIVERY);
            s_deliv.stage = DELIV_DELAY_AFTER_PRE;
            s_deliv.delay_start_ms = g_tmr4_milliseconds;
            printf("[送样] 反吹清线完成 → 延时\r\n");
        }
        break;

    case DELIV_DELAY_AFTER_PRE:
        if ((g_tmr4_milliseconds - s_deliv.delay_start_ms) >= STAGE_DELAY_MS) {
            s_deliv.stage = DELIV_STABILIZE;
            s_deliv.stage_start_sec = g_tmr2_seconds;
            printf("[送样] 稳定等待 2s\r\n");
        }
        break;

    case DELIV_STABILIZE:
        if ((g_tmr2_seconds - s_deliv.stage_start_sec) >= 2) {
            /* 开搅拌 */
            if (s_deliv.bucket_id == 0) STIR_A_ON();
            else                        STIR_B_ON();
            s_deliv.stage = DELIV_MIX;
            s_deliv.stage_start_sec = g_tmr2_seconds;
            printf("[送样] 搅拌开始\r\n");
        }
        break;

    case DELIV_MIX:
        /* 搅拌3秒后开始计量 */
        if ((g_tmr2_seconds - s_deliv.stage_start_sec) >= 3) {
            can_motor_set_speed(MOTOR_ID_DELIVERY, s_deliv.rpm, MOTOR_DIR_CW);
            can_motor_start(MOTOR_ID_DELIVERY);
            s_deliv.stage = DELIV_MEASURE;
            s_deliv.stage_start_sec = g_tmr2_seconds;
            printf("[送样] 计量送样开始\r\n");
        }
        break;

    case DELIV_MEASURE:
        if ((g_tmr2_seconds - s_deliv.stage_start_sec) >= s_deliv.deliver_sec) {
            can_motor_stop(MOTOR_ID_DELIVERY);
            /* 停搅拌 */
            if (s_deliv.bucket_id == 0) STIR_A_OFF();
            else                        STIR_B_OFF();
            s_deliv.stage = DELIV_DELAY_AFTER_MEAS;
            s_deliv.delay_start_ms = g_tmr4_milliseconds;
            printf("[送样] 计量完成 → 延时\r\n");
        }
        break;

    case DELIV_DELAY_AFTER_MEAS:
        if ((g_tmr4_milliseconds - s_deliv.delay_start_ms) >= STAGE_DELAY_MS) {
            /* 回抽 */
            can_motor_set_speed(MOTOR_ID_DELIVERY, s_deliv.rpm, MOTOR_DIR_CCW);
            can_motor_start(MOTOR_ID_DELIVERY);
            s_deliv.stage = DELIV_BACKDRAW;
            s_deliv.stage_start_sec = g_tmr2_seconds;
            printf("[送样] 回抽开始 %us\r\n", s_deliv.backdraw_sec);
        }
        break;

    case DELIV_BACKDRAW:
        if ((g_tmr2_seconds - s_deliv.stage_start_sec) >= s_deliv.backdraw_sec) {
            can_motor_stop(MOTOR_ID_DELIVERY);
            DELIVER_VALVE_OFF();
            s_deliv.result = 1;
            s_deliv.stage = DELIV_DONE;
            if (s_deliv.bucket_id == 0) g_state.bucket_a_state = BUCKET_IDLE;
            else                        g_state.bucket_b_state = BUCKET_IDLE;
            printf("[送样] 完成 桶%c\r\n", s_deliv.bucket_id ? 'B' : 'A');
        }
        break;

    default:
        break;
    }
}

uint8_t delivery_is_active(void)
{
    return (s_deliv.stage != DELIV_IDLE && s_deliv.stage != DELIV_DONE
            && s_deliv.stage != DELIV_ABORT) ? 1 : 0;
}

uint8_t delivery_get_result(void)
{
    return s_deliv.result;
}

/* ================================================================
 *  留样状态机
 * ================================================================ */

/* 转盘定位超时(秒) */
#define TURNTABLE_TIMEOUT_SEC  30

static struct {
    retain_stage_t stage;
    uint8_t  target_bottle;   /* 目标瓶号(1-24) */
    uint8_t  is_manual;
    uint16_t pump_sec;        /* 泵送时长 */
    uint16_t rpm;
    uint32_t stage_start_sec;
    uint8_t  result;
} s_retain;

uint8_t retain_start(uint8_t bottle_target, uint8_t is_manual)
{
    if (s_retain.stage != RETAIN_IDLE && s_retain.stage != RETAIN_DONE
        && s_retain.stage != RETAIN_ABORT)
        return 0;

    s_retain.target_bottle = bottle_target;
    s_retain.is_manual     = is_manual;
    s_retain.pump_sec      = g_sampling_cfg.blowback_sec;  /* 临时：用反吹时长代替 */
    s_retain.rpm           = g_retain_cfg.motor_rpm;
    s_retain.result        = 0;

    /* 启动转盘电机定位 */
    can_motor_set_speed(MOTOR_ID_TURNTABLE, s_retain.rpm, MOTOR_DIR_CW);
    can_motor_start(MOTOR_ID_TURNTABLE);

    s_retain.stage = RETAIN_MOVE_BOTTLE;
    s_retain.stage_start_sec = g_tmr2_seconds;

    printf("[留样] 启动 目标瓶%u %s\r\n",
           bottle_target, is_manual ? "手动" : "自动");
    return 1;
}

void retain_step(void)
{
    if (s_retain.stage == RETAIN_IDLE || s_retain.stage == RETAIN_DONE
        || s_retain.stage == RETAIN_ABORT)
        return;

    switch (s_retain.stage) {
    case RETAIN_MOVE_BOTTLE:
        /* 非阻塞转盘定位：检查瓶到位传感器 */
        if (input_read(INPUT_BOTTLE_POS) == 1) {
            /* 到位 → 停转盘 → 开始泵送 */
            can_motor_stop(MOTOR_ID_TURNTABLE);
            /* 开送留样阀(留样方向) + 留样电机正转 */
            DELIVER_VALVE_ON();
            can_motor_set_speed(MOTOR_ID_DELIVERY, s_retain.rpm, MOTOR_DIR_CW);
            can_motor_start(MOTOR_ID_DELIVERY);
            s_retain.stage = RETAIN_PUMP;
            s_retain.stage_start_sec = g_tmr2_seconds;
            printf("[留样] 瓶%u到位 → 泵送 %us\r\n",
                   s_retain.target_bottle, s_retain.pump_sec);
        } else if ((g_tmr2_seconds - s_retain.stage_start_sec) >= TURNTABLE_TIMEOUT_SEC) {
            /* 超时 → 中止 */
            can_motor_stop(MOTOR_ID_TURNTABLE);
            s_retain.result = 0;
            s_retain.stage = RETAIN_ABORT;
            printf("[留样] 转盘定位超时 中止\r\n");
        }
        break;

    case RETAIN_PUMP:
        if ((g_tmr2_seconds - s_retain.stage_start_sec) >= s_retain.pump_sec) {
            can_motor_stop(MOTOR_ID_DELIVERY);
            DELIVER_VALVE_OFF();
            s_retain.result = 1;
            s_retain.stage = RETAIN_DONE;
            printf("[留样] 完成 瓶%u\r\n", s_retain.target_bottle);
        }
        break;

    default:
        break;
    }
}

uint8_t retain_is_active(void)
{
    return (s_retain.stage != RETAIN_IDLE && s_retain.stage != RETAIN_DONE
            && s_retain.stage != RETAIN_ABORT) ? 1 : 0;
}

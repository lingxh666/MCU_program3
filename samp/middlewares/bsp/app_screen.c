/**
 * @file    app_screen.c
 * @brief   串口屏应用层 — 命令接收分发 + 状态刷新
 *
 * bsp_screen 的回调在 ISR 上下文中调用，这里用环形缓冲区
 * 将命令传递到 Task03 主循环中处理。
 *
 * 支持两套地址体系：
 *   - 旧体系: SCR_ADDR_CMD_MANUAL / SCR_ADDR_SAMP_xxx (向后兼容)
 *   - 新体系: cmd_type(高字节) + sub_cmd(低字节) 分发
 */
#include "app_screen.h"
#include "bsp_screen.h"
#include "app_config.h"
#include "app_flashdb.h"
#include "app_sampling.h"
#include "app_scheduler.h"
#include "app_record_query.h"
#include "bsp_io.h"
#include "bsp_can_motor.h"
#include <stdio.h>

/* ======================== 前向声明 ======================== */
static void screen_isr_callback(uint8_t cmd, uint16_t addr,
                                const uint8_t *data, uint16_t data_len);
static void screen_handle_command(uint16_t addr, uint16_t value);
static void screen_handle_settings(uint8_t sub_cmd, uint16_t value);
static void screen_handle_calib(uint8_t sub_cmd, uint16_t value);
static void screen_handle_manual(uint8_t sub_cmd, uint16_t value);
static void screen_handle_confirm(uint8_t sub_cmd, uint16_t value);
static void screen_handle_legacy(uint16_t addr, uint16_t value);
static uint16_t screen_settings_addr(uint8_t sub_cmd);
static void screen_manual_reset_defaults(void);

/* ======================== 命令环形缓冲区 ======================== */
static volatile scr_cmd_t s_cmd_buf[SCR_CMD_BUF_SIZE];
static volatile uint8_t   s_cmd_wr = 0;  /* ISR 写指针 */
static volatile uint8_t   s_cmd_rd = 0;  /* Task 读指针 */

/* ======================== 屏幕状态 ======================== */
static scr_state_t s_scr_state = {
    SCR_PAGE_UNKNOWN, 0, 0, 0
};

/* ======================== 外发命令缓冲区 ======================== */
static scr_out_cmd_t s_out_buf[SCR_OUT_BUF_SIZE];
static uint8_t s_out_wr = 0;
static uint8_t s_out_rd = 0;
static uint8_t s_bootstrap_done = 0;
static uint16_t s_login_password = 0;
static uint8_t s_login_wait_confirm = 0;

typedef struct {
    uint8_t sample_bucket;       /* 0=A桶 1=B桶 */
    uint16_t sample_volume_ml;
    uint8_t delivery_mode;       /* 0=瞬时 1=A桶 2=B桶 */
    uint16_t delivery_volume_ml;
    uint8_t retain_mode;         /* 0=瞬时 1=A桶 2=B桶 */
    uint16_t retain_volume_ml;
    uint8_t bottle_number;       /* 1..24 */
} scr_manual_ctx_t;

static scr_manual_ctx_t s_manual_ctx = {
    0, 0, 1, 0, 1, 0, 1
};

/* 外部定时器 */
extern volatile uint32_t g_tmr4_milliseconds;

static uint16_t screen_settings_addr(uint8_t sub_cmd)
{
    return (uint16_t)(((uint16_t)SCR_CMD_TYPE_SETTINGS << 8) | sub_cmd);
}

static void screen_manual_reset_defaults(void)
{
    s_manual_ctx.sample_bucket = (g_state.current_bucket <= 1) ? g_state.current_bucket : 0;
    s_manual_ctx.sample_volume_ml = g_sampling_cfg.volume_ml;

    s_manual_ctx.delivery_mode = (s_manual_ctx.sample_bucket == 0) ? 1 : 2;
    s_manual_ctx.delivery_volume_ml = g_delivery_cfg.volume_ml;

    s_manual_ctx.retain_mode = (s_manual_ctx.sample_bucket == 0) ? 1 : 2;
    s_manual_ctx.retain_volume_ml = g_retain_cfg.volume_ml;

    if (g_retain_bottle_state.current_bottle >= 1 &&
        g_retain_bottle_state.current_bottle <= 24) {
        s_manual_ctx.bottle_number = g_retain_bottle_state.current_bottle;
    } else {
        s_manual_ctx.bottle_number = 1;
    }
}

/* ======================== ISR 回调 ======================== */
static void screen_isr_callback(uint8_t cmd, uint16_t addr,
                                const uint8_t *data, uint16_t data_len)
{
    uint16_t value;
    uint8_t next;
    uint8_t word_count = 0;

    /* 只处理写通知 (0x82) 和读应答 (0x83) */
    if (cmd != 0x82 && cmd != 0x83)
        return;
    if (data_len < 2)
        return;

    /* 兼容DWIN帧：data[0] 可能是字数(通常=0x01) */
    if (data_len >= 3) {
        word_count = data[0];
        if (word_count > 0 && (uint16_t)(1u + (uint16_t)word_count * 2u) <= data_len) {
            value = ((uint16_t)data[1] << 8) | data[2];
        } else {
            value = ((uint16_t)data[0] << 8) | data[1];
        }
    } else {
        value = ((uint16_t)data[0] << 8) | data[1];
    }

    /* 页面ID变更检测（兼容 samplingB 页面字：0x0B/0x15） */
    if (addr == SCR_ADDR_PAGE_ID) {
        if (value == SCR_PAGE_HOME ||
            value == SCR_PANEL_PAGE_BOOT_HOME ||
            value == SCR_PANEL_PAGE_MAIN_HOME) {
            s_scr_state.current_page = SCR_PAGE_HOME;
        } else if (value <= SCR_PAGE_STATUS) {
            s_scr_state.current_page = (scr_page_id_t)value;
        } else {
            s_scr_state.current_page = SCR_PAGE_UNKNOWN;
        }
        return;  /* 页面切换不入命令队列 */
    }

    /* samplingB 的主页通知帧映射：5A A5 06 83 00 00 01 80 00 */
    if (addr == 0x0000u && (value == 0x0180u || value == 0x8000u)) {
        s_scr_state.current_page = SCR_PAGE_HOME;
        return;  /* 页面切换不入命令队列 */
    }

    /* 写入环形缓冲区（无锁，单生产者单消费者安全） */
    next = (s_cmd_wr + 1) % SCR_CMD_BUF_SIZE;
    if (next == s_cmd_rd)
        return;  /* 满了丢弃 */

    s_cmd_buf[s_cmd_wr].addr  = addr;
    s_cmd_buf[s_cmd_wr].value = value;
    s_cmd_wr = next;
}

/* ======================== 新体系: 设置类命令 ======================== */
static void screen_handle_settings(uint8_t sub_cmd, uint16_t value)
{
    uint8_t save_sample = 0;
    uint8_t save_delivery = 0;
    uint8_t save_retain = 0;
    uint8_t save_comm = 0;
    uint8_t save_system = 0;

    switch (sub_cmd) {
    /* 登录/密码设置（与samplingB兼容） */
    case 0x01:
        s_login_wait_confirm = 0;
        break;
    case 0x02:
        s_login_password = value;
        s_login_wait_confirm = 1;
        printf("[屏幕] 登录密码缓存=0x%04X\r\n", (unsigned int)s_login_password);
        break;
    case 0x04:
        /* 兼容部分页面将确认动作写到0x5004 */
        screen_handle_confirm(0, value);
        break;
    /* 采样设置 */
    case SCR_SUB_SAMP_MODE:
        if (g_sampling_cfg.mode != (uint8_t)value) {
            g_sampling_cfg.mode = (uint8_t)value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_INTERVAL:
        if (g_sampling_cfg.interval_min != value) {
            g_sampling_cfg.interval_min = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_VOLUME:
        if (g_sampling_cfg.volume_ml != value) {
            g_sampling_cfg.volume_ml = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_BLOWBACK:
        if (g_sampling_cfg.blowback_sec != value) {
            g_sampling_cfg.blowback_sec = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_IMPROVE:
        if (g_sampling_cfg.improve_sec != value) {
            g_sampling_cfg.improve_sec = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_TUBEHOLD:
        if (g_sampling_cfg.tube_hold_sec != value) {
            g_sampling_cfg.tube_hold_sec = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_CYCLETIME:
        if (g_sampling_cfg.cycle_time_min != value) {
            g_sampling_cfg.cycle_time_min = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_ANALYSIS:
        if (g_sampling_cfg.analysis_time_min != value) {
            g_sampling_cfg.analysis_time_min = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_FLOWSTART:
        if (g_sampling_cfg.flow_start != value) {
            g_sampling_cfg.flow_start = value;
            save_sample = 1;
        }
        break;
    case SCR_SUB_SAMP_FLOWSTOP:
        if (g_sampling_cfg.flow_stop != value) {
            g_sampling_cfg.flow_stop = value;
            save_sample = 1;
        }
        break;
    /* 送样设置 */
    case SCR_SUB_DELIV_HOUR:
        if (g_delivery_cfg.start_hour != (uint8_t)value) {
            g_delivery_cfg.start_hour = (uint8_t)value;
            save_delivery = 1;
        }
        break;
    case SCR_SUB_DELIV_MIN:
        if (g_delivery_cfg.start_min != (uint8_t)value) {
            g_delivery_cfg.start_min = (uint8_t)value;
            save_delivery = 1;
        }
        break;
    case SCR_SUB_DELIV_DURATION:
        if (g_delivery_cfg.duration_sec != value) {
            g_delivery_cfg.duration_sec = value;
            save_delivery = 1;
        }
        break;
    case SCR_SUB_DELIV_BACKDRAW:
        if (g_delivery_cfg.backdraw_sec != value) {
            g_delivery_cfg.backdraw_sec = value;
            save_delivery = 1;
        }
        break;
    case SCR_SUB_DELIV_ENABLE:
        if (g_delivery_cfg.enable != (uint8_t)value) {
            g_delivery_cfg.enable = (uint8_t)value;
            save_delivery = 1;
        }
        break;
    /* 留样设置 */
    case SCR_SUB_RETAIN_MODE:
        if (g_retain_cfg.mode != (uint8_t)value) {
            g_retain_cfg.mode = (uint8_t)value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_VOLUME:
        if (g_retain_cfg.volume_ml != value) {
            g_retain_cfg.volume_ml = value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_PARALLEL:
        if (g_retain_cfg.parallel_count != (uint8_t)value) {
            g_retain_cfg.parallel_count = (uint8_t)value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_MIX:
        if (g_retain_cfg.mix_count != (uint8_t)value) {
            g_retain_cfg.mix_count = (uint8_t)value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_BLOWBACK:
        if (g_retain_cfg.blowback_sec != value) {
            g_retain_cfg.blowback_sec = value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_ENABLE:
        if (g_retain_cfg.enable != (uint8_t)value) {
            g_retain_cfg.enable = (uint8_t)value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_ACID:
        if (g_retain_cfg.enable_acid != (uint8_t)value) {
            g_retain_cfg.enable_acid = (uint8_t)value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_TUBEHOLD:
        if (g_retain_cfg.tube_hold_sec != value) {
            g_retain_cfg.tube_hold_sec = value;
            save_retain = 1;
        }
        break;
    case SCR_SUB_RETAIN_BACKDRAW:
        if (g_retain_cfg.backdraw_sec != value) {
            g_retain_cfg.backdraw_sec = value;
            save_retain = 1;
        }
        break;
    /* 通讯设置 */
    case SCR_SUB_COMM_PROTOCOL:
        if (g_comm_cfg.protocol != (uint8_t)value) {
            g_comm_cfg.protocol = (uint8_t)value;
            save_comm = 1;
        }
        break;
    case SCR_SUB_COMM_ADDR:
        if (g_comm_cfg.device_addr != (uint8_t)value) {
            g_comm_cfg.device_addr = (uint8_t)value;
            save_comm = 1;
        }
        break;
    case SCR_SUB_COMM_FLOWLOWER:
        if (g_comm_cfg.flow_ad_lower != value) {
            g_comm_cfg.flow_ad_lower = value;
            save_comm = 1;
        }
        break;
    case SCR_SUB_SYS_AUTORUN:
        if (g_system_setting_cfg.auto_run_mode != (uint8_t)value) {
            g_system_setting_cfg.auto_run_mode = (uint8_t)value;
            save_system = 1;
        }
        break;
    case SCR_SUB_SYS_WATER_STATION:
        if (g_system_setting_cfg.water_station_mode != (uint8_t)value) {
            g_system_setting_cfg.water_station_mode = (uint8_t)value;
            save_system = 1;
        }
        break;
    default: break;
    }

    if (save_sample) {
        cfg_save_sample(&g_sampling_cfg);
        printf("[屏幕] 采样设置已写入KVDB\r\n");
    }
    if (save_delivery) {
        cfg_save_delivery(&g_delivery_cfg);
        printf("[屏幕] 送样设置已写入KVDB\r\n");
    }
    if (save_retain) {
        cfg_save_retain(&g_retain_cfg);
        printf("[屏幕] 留样设置已写入KVDB\r\n");
    }
    if (save_comm) {
        cfg_save_comm(&g_comm_cfg);
        printf("[屏幕] 通讯设置已写入KVDB\r\n");
    }
    if (save_system) {
        cfg_save_system(&g_system_setting_cfg);
        printf("[屏幕] 系统设置已写入KVDB\r\n");
    }
}

static void screen_handle_calib(uint8_t sub_cmd, uint16_t value)
{
    uint8_t changed = 0;

    switch (sub_cmd) {
    case 0x10: if (g_calib_params.sampling.time1 != value) { g_calib_params.sampling.time1 = value; changed = 1; } break;
    case 0x11: if (g_calib_params.sampling.real_value1 != value) { g_calib_params.sampling.real_value1 = value; changed = 1; } break;
    case 0x12: if (g_calib_params.sampling.time2 != value) { g_calib_params.sampling.time2 = value; changed = 1; } break;
    case 0x13: if (g_calib_params.sampling.real_value2 != value) { g_calib_params.sampling.real_value2 = value; changed = 1; } break;
    case 0x14: if (g_calib_params.sampling.time3 != value) { g_calib_params.sampling.time3 = value; changed = 1; } break;
    case 0x15: if (g_calib_params.sampling.real_value3 != value) { g_calib_params.sampling.real_value3 = value; changed = 1; } break;

    case 0x16: if (g_calib_params.retain.time1 != value) { g_calib_params.retain.time1 = value; changed = 1; } break;
    case 0x17: if (g_calib_params.retain.real_value1 != value) { g_calib_params.retain.real_value1 = value; changed = 1; } break;
    case 0x18: if (g_calib_params.retain.time2 != value) { g_calib_params.retain.time2 = value; changed = 1; } break;
    case 0x19: if (g_calib_params.retain.real_value2 != value) { g_calib_params.retain.real_value2 = value; changed = 1; } break;
    case 0x1A: if (g_calib_params.retain.time3 != value) { g_calib_params.retain.time3 = value; changed = 1; } break;
    case 0x1B: if (g_calib_params.retain.real_value3 != value) { g_calib_params.retain.real_value3 = value; changed = 1; } break;

    case 0x1C: if (g_calib_params.acid.time1 != value) { g_calib_params.acid.time1 = value; changed = 1; } break;
    case 0x1D: if (g_calib_params.acid.real_value1 != value) { g_calib_params.acid.real_value1 = value; changed = 1; } break;
    case 0x1E: if (g_calib_params.acid.time2 != value) { g_calib_params.acid.time2 = value; changed = 1; } break;
    case 0x1F: if (g_calib_params.acid.real_value2 != value) { g_calib_params.acid.real_value2 = value; changed = 1; } break;
    case 0x20: if (g_calib_params.acid.time3 != value) { g_calib_params.acid.time3 = value; changed = 1; } break;
    case 0x21: if (g_calib_params.acid.real_value3 != value) { g_calib_params.acid.real_value3 = value; changed = 1; } break;

    case 0x40: if (g_calib_params.temp.input_ad != value) { g_calib_params.temp.input_ad = value; changed = 1; } break;
    case 0x41: if (g_calib_params.temp.zero_point_ad != value) { g_calib_params.temp.zero_point_ad = value; changed = 1; } break;
    case 0x42: if (g_calib_params.temp.calib_ad != value) { g_calib_params.temp.calib_ad = value; changed = 1; } break;
    case 0x43: if (g_calib_params.temp.calib_value != value) { g_calib_params.temp.calib_value = value; changed = 1; } break;
    case 0x44: if (g_calib_params.temp.set_temp != value) { g_calib_params.temp.set_temp = value; changed = 1; } break;
    case 0x45: if (g_calib_params.temp.upper_dev != value) { g_calib_params.temp.upper_dev = value; changed = 1; } break;
    case 0x46: if (g_calib_params.temp.lower_dev != value) { g_calib_params.temp.lower_dev = value; changed = 1; } break;
    case 0x47: if (g_calib_params.temp.zero_temp != value) { g_calib_params.temp.zero_temp = value; changed = 1; } break;
    default:
        break;
    }

    if (changed) {
        cfg_save_calib(&g_calib_params);
        printf("[屏幕] 校准参数已写入KVDB\r\n");
    }
}

/* ======================== 新体系: 手动控制命令 ======================== */
static void screen_handle_manual(uint8_t sub_cmd, uint16_t value)
{
    uint8_t action = (uint8_t)(value & 0xFF);

    switch (sub_cmd) {
    case 0x00: /* 采样蠕动泵 */
        if (action == 0x00) {
            can_motor_stop(MOTOR_ID_SAMPLING);
            g_state.sampling_motor = 0;
        } else if (action == 0x01 || action == 0x02) {
            can_motor_set_speed(MOTOR_ID_SAMPLING, g_sampling_cfg.motor_rpm,
                                (action == 0x01) ? MOTOR_DIR_CW : MOTOR_DIR_CCW);
            can_motor_start(MOTOR_ID_SAMPLING);
            g_state.sampling_motor = action;
        }
        break;
    case 0x01: /* 送样蠕动泵 */
        if (action == 0x00) {
            can_motor_stop(MOTOR_ID_DELIVERY);
            g_state.delivery_motor = 0;
        } else if (action == 0x01 || action == 0x02) {
            can_motor_set_speed(MOTOR_ID_DELIVERY, g_delivery_cfg.motor_rpm,
                                (action == 0x01) ? MOTOR_DIR_CW : MOTOR_DIR_CCW);
            can_motor_start(MOTOR_ID_DELIVERY);
            g_state.delivery_motor = action;
        }
        break;
    case 0x02: /* 进水三通阀(硬件仅开关，动作1/2均视为打开) */
        if (action == 0x00) {
            INLET_VALVE_OFF();
            g_state.inlet_valve = 0;
        } else if (action == 0x01 || action == 0x02) {
            INLET_VALVE_ON();
            g_state.inlet_valve = action;
        }
        break;
    case 0x03: /* 出水三通阀 */
        if (action == 0x00) {
            OUTLET_VALVE_A_OFF();
            OUTLET_VALVE_B_OFF();
        } else if (action == 0x01) {
            OUTLET_VALVE_A_ON();
            OUTLET_VALVE_B_OFF();
        } else if (action == 0x02) {
            OUTLET_VALVE_A_OFF();
            OUTLET_VALVE_B_ON();
        }
        g_state.outlet_valve = action;
        break;
    case 0x04: /* 送/留样阀 */
        if (action == 0x00) {
            DELIVER_VALVE_OFF();
            g_state.sample_valve = 0;
        } else if (action == 0x01) {
            DELIVER_VALVE_ON();
            g_state.sample_valve = 1;
        }
        break;
    case 0x05: /* 瞬时三通阀 */
        if (action == 0x00) {
            INSTANT_VALVE_OFF();
            g_state.instant_valve = 0;
        } else if (action == 0x01) {
            INSTANT_VALVE_ON();
            g_state.instant_valve = 1;
        }
        break;
    case 0x07: /* 外接泵 */
        if (action == 0x00) {
            EXT_PUMP_OFF();
        } else if (action == 0x01) {
            EXT_PUMP_ON();
        }
        break;
    case 0x08: /* A桶搅拌 */
        if (action == 0x00) {
            STIR_A_OFF();
        } else if (action == 0x01) {
            STIR_A_ON();
        }
        break;
    case 0x09: /* B桶搅拌 */
        if (action == 0x00) {
            STIR_B_OFF();
        } else if (action == 0x01) {
            STIR_B_ON();
        }
        break;
    case 0x0A: /* A桶排水 */
        if (action == 0x00) {
            DRAIN_A_OFF();
            g_state.drain_a = 0;
        } else if (action == 0x01) {
            if (!drain_start(0)) {
                DRAIN_A_ON();
            }
            g_state.drain_a = 1;
        }
        break;
    case 0x0B: /* B桶排水 */
        if (action == 0x00) {
            DRAIN_B_OFF();
            g_state.drain_b = 0;
        } else if (action == 0x01) {
            if (!drain_start(1)) {
                DRAIN_B_ON();
            }
            g_state.drain_b = 1;
        }
        break;
    case 0x0C: /* 门锁 */
        if (action == 0x00 || action == 0x01) {
            LOCK_ON();
        } else {
            LOCK_OFF();
        }
        break;
    case 0x12: /* 手动采样AB桶选择 */
        if (action == 0x01) {
            s_manual_ctx.sample_bucket = 0;
        } else if (action == 0x02) {
            s_manual_ctx.sample_bucket = 1;
        }
        g_state.current_bucket = s_manual_ctx.sample_bucket;
        break;
    case 0x13: /* 手动采样量 */
        if (value > 0) {
            s_manual_ctx.sample_volume_ml = value;
        }
        break;
    case 0x14: /* 手动送样模式 */
        if (action <= 0x02) {
            s_manual_ctx.delivery_mode = action;
        }
        break;
    case 0x15: /* 手动送样量 */
        if (value > 0) {
            s_manual_ctx.delivery_volume_ml = value;
        }
        break;
    case 0x16: /* 手动留样模式 */
        if (action <= 0x02) {
            s_manual_ctx.retain_mode = action;
        }
        break;
    case 0x17: /* 手动留样量 */
        if (value > 0) {
            s_manual_ctx.retain_volume_ml = value;
        }
        break;
    case 0x18: /* 选择留样瓶号 */
        if (action >= 1 && action <= 24) {
            s_manual_ctx.bottle_number = action;
            g_state.bottle_next = action;
        }
        break;
    case 0x19: /* 转到留样瓶号 */
        if (action >= 1 && action <= 24) {
            s_manual_ctx.bottle_number = action;
            g_state.bottle_current = action;
            g_state.bottle_next = action;
            g_retain_bottle_state.current_bottle = action;
            cfg_save_retain_state(&g_retain_bottle_state);
            cfg_save_retain(&g_retain_cfg);
        }
        break;
    case 0x1A: /* 排空留样瓶(清空使用标记) */
        if (action >= 1 && action <= 24) {
            g_retain_bottle_state.used_mask &= ~(1UL << (action - 1));
            cfg_save_retain_state(&g_retain_bottle_state);
        }
        break;
    default:
        printf("[屏幕] 手动命令: sub=0x%02X val=%u\r\n",
               (unsigned int)sub_cmd, (unsigned int)value);
        break;
    }
}

/* ======================== 新体系: 确认/系统命令 ======================== */
static void screen_handle_confirm(uint8_t sub_cmd, uint16_t value)
{
    /* 确认命令: addr高字节=0x00, value编码为 (action<<8)|param */
    uint8_t action = (uint8_t)(value >> 8);
    uint8_t param  = (uint8_t)(value & 0xFF);
    (void)sub_cmd;

    switch (action) {
    case SCR_ACT_START:
        if (param == 0x02) {
            g_state.running = 1;
            scheduler_init((sched_mode_t)g_sampling_cfg.mode);
            scheduler_start();
            printf("[屏幕] 系统启动\r\n");
        }
        break;
    case SCR_ACT_ESTOP:
        g_state.running = 0;
        scheduler_stop();
        sampling_abort();
        can_motor_stop(MOTOR_ID_DELIVERY);
        can_motor_stop(MOTOR_ID_TURNTABLE);
        INLET_VALVE_OFF();
        INSTANT_VALVE_OFF();
        DELIVER_VALVE_OFF();
        DRAIN_A_OFF();
        DRAIN_B_OFF();
        STIR_A_OFF();
        STIR_B_OFF();
        EXT_PUMP_OFF();
        g_state.sampling_motor = 0;
        g_state.delivery_motor = 0;
        g_state.inlet_valve = 0;
        g_state.instant_valve = 0;
        g_state.sample_valve = 0;
        g_state.drain_a = 0;
        g_state.drain_b = 0;
        printf("[屏幕] 紧急停止\r\n");
        break;
    case SCR_ACT_MANUAL_SAMP:
        if (param == 0x01) {
            if (!sampling_start(s_manual_ctx.sample_bucket, 1)) {
                printf("[屏幕] 手动采样启动失败(状态忙)\r\n");
            } else {
                printf("[屏幕] 手动采样启动: bucket=%u volume=%u\r\n",
                       (unsigned int)(s_manual_ctx.sample_bucket + 1),
                       (unsigned int)s_manual_ctx.sample_volume_ml);
            }
        }
        break;
    case SCR_ACT_MANUAL_DELIV:
        if (param == 0x01) {
            uint8_t bucket = s_manual_ctx.sample_bucket;
            if (s_manual_ctx.delivery_mode == 1) {
                bucket = 0;
            } else if (s_manual_ctx.delivery_mode == 2) {
                bucket = 1;
            }
            if (!delivery_start(bucket, 1)) {
                printf("[屏幕] 手动送样启动失败(状态忙)\r\n");
            } else {
                printf("[屏幕] 手动送样启动: mode=%u bucket=%u volume=%u\r\n",
                       (unsigned int)s_manual_ctx.delivery_mode,
                       (unsigned int)(bucket + 1),
                       (unsigned int)s_manual_ctx.delivery_volume_ml);
            }
        }
        break;
    case SCR_ACT_MANUAL_RETAIN:
        if (param == 0x01) {
            if (!retain_start(s_manual_ctx.bottle_number, 1)) {
                printf("[屏幕] 手动留样启动失败(状态忙)\r\n");
            } else {
                g_retain_bottle_state.current_bottle = s_manual_ctx.bottle_number;
                cfg_save_retain_state(&g_retain_bottle_state);
                printf("[屏幕] 手动留样启动: mode=%u bottle=%u volume=%u\r\n",
                       (unsigned int)s_manual_ctx.retain_mode,
                       (unsigned int)s_manual_ctx.bottle_number,
                       (unsigned int)s_manual_ctx.retain_volume_ml);
            }
        }
        break;
    case SCR_ACT_BOTTLE_RESET:
        g_state.bottle_current = 1;
        g_state.bottle_next = 1;
        g_retain_bottle_state.current_bottle = 1;
        g_retain_bottle_state.used_mask = 0;
        cfg_save_retain_state(&g_retain_bottle_state);
        printf("[屏幕] 留样瓶复位\r\n");
        break;
    case SCR_ACT_LOGIN_CONFIRM:
        if (param == 0x01) {
            if (!s_login_wait_confirm) {
                printf("[屏幕] 登录确认被忽略: 无待确认密码\r\n");
                break;
            }
            uint8_t page = SCR_PANEL_PAGE_LOGIN_FAIL;
            if (s_login_password == 0x091A) {
                page = SCR_PANEL_PAGE_ADMIN;
            } else if (s_login_password == 0x1A0A) {
                page = SCR_PANEL_PAGE_OPERATOR;
            } else if (s_login_password == 0x0000) {
                page = SCR_PANEL_PAGE_SAMPLER;
            }
            s_scr_state.current_page = SCR_PAGE_SETTINGS;
            screen_switch_page(page);
            printf("[屏幕] 登录确认: pwd=0x%04X -> page=0x%02X\r\n",
                   (unsigned int)s_login_password, (unsigned int)page);
            s_login_password = 0;
            s_login_wait_confirm = 0;
        }
        break;
    case SCR_ACT_LOG_QUERY: {
        /* param: 0x71=采样 0x72=送样 0x73=留样 0x74=电源 0x75=门禁 */
        rq_type_t qt = (rq_type_t)(param - 0x71);
        if ((uint8_t)qt < RQ_TYPE_COUNT)
            record_query_init(qt);
        break;
    }
    case SCR_ACT_LOG_SAMP:
    case SCR_ACT_LOG_DELIV:
    case SCR_ACT_LOG_RETAIN:
    case SCR_ACT_LOG_POWER:
    case SCR_ACT_LOG_DOOR:
        /* 0x71~0x75 与 RQ_SAMPLING~RQ_DOOR 一一对应 */
        record_query_page_nav((rq_type_t)(action - SCR_ACT_LOG_SAMP), param);
        break;
    default:
        break;
    }
}

/* ======================== 旧体系: 兼容处理 ======================== */
static void screen_handle_legacy(uint16_t addr, uint16_t value)
{
    if (addr == SCR_ADDR_CMD_MANUAL) {
        switch (value) {
        case SCR_CMD_SAMPLING_A:
            sampling_start(0, 1);
            break;
        case SCR_CMD_SAMPLING_B:
            sampling_start(1, 1);
            break;
        case SCR_CMD_DRAIN_A:
            drain_start(0);
            break;
        case SCR_CMD_DRAIN_B:
            drain_start(1);
            break;
        case SCR_CMD_SYS_START:
            g_state.running = 1;
            scheduler_init((sched_mode_t)g_sampling_cfg.mode);
            scheduler_start();
            printf("[屏幕] 系统启动\r\n");
            break;
        case SCR_CMD_SYS_STOP:
            g_state.running = 0;
            scheduler_stop();
            printf("[屏幕] 系统停止\r\n");
            break;
        case SCR_CMD_SAMP_ABORT:
            sampling_abort();
            break;
        default:
            break;
        }
    }

    /* 配置参数写入处理 */
    if (addr == SCR_ADDR_SAMP_MODE) {
        g_sampling_cfg.mode = (uint8_t)value;
    } else if (addr == SCR_ADDR_SAMP_INTERVAL) {
        g_sampling_cfg.interval_min = value;
    } else if (addr == SCR_ADDR_SAMP_VOLUME) {
        g_sampling_cfg.volume_ml = value;
    }
}

/* ======================== 命令分发（新旧体系统一入口） ======================== */
static void screen_handle_command(uint16_t addr, uint16_t value)
{
    uint8_t cmd_type = (uint8_t)(addr >> 8);
    uint8_t sub_cmd  = (uint8_t)(addr & 0xFF);

    if (cmd_type == SCR_CMD_TYPE_SETTINGS || cmd_type == SCR_CMD_TYPE_CALIB) {
        s_scr_state.current_page = SCR_PAGE_SETTINGS;
    } else if (cmd_type == SCR_CMD_TYPE_MANUAL) {
        s_scr_state.current_page = SCR_PAGE_MANUAL;
    }

    switch (cmd_type) {
    case SCR_CMD_TYPE_SETTINGS:
        screen_handle_settings(sub_cmd, value);
        break;
    case SCR_CMD_TYPE_CALIB:
        screen_handle_calib(sub_cmd, value);
        break;
    case SCR_CMD_TYPE_MANUAL:
        screen_handle_manual(sub_cmd, value);
        break;
    case SCR_CMD_TYPE_CONFIRM:
        screen_handle_confirm(sub_cmd, value);
        break;
    default:
        /* 兼容旧地址体系 */
        screen_handle_legacy(addr, value);
        break;
    }
}

/* ======================== 公共接口 ======================== */

void screen_task_init(void)
{
    screen_init();
    screen_set_cmd_callback(screen_isr_callback);
    s_scr_state.current_page = SCR_PAGE_HOME;
    s_scr_state.ready = 0;
    s_scr_state.home_refresh_tick = 0;
    s_scr_state.ready_tick = 0;
    s_out_wr = 0;
    s_out_rd = 0;
    s_bootstrap_done = 0;
    s_login_password = 0;
    s_login_wait_confirm = 0;
    screen_manual_reset_defaults();
    printf("[屏幕] 初始化完成\r\n");
}

void screen_bootstrap_on_powerup(void)
{
    if (s_bootstrap_done) {
        printf("[屏幕] 开机同步已完成，忽略重复调用\r\n");
        return;
    }
    s_bootstrap_done = 1;

    /* 同步设置页关键参数，按 samplingB 的 0x50xx 地址体系回写 */
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_MODE), g_sampling_cfg.mode);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_INTERVAL), g_sampling_cfg.interval_min);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_VOLUME), g_sampling_cfg.volume_ml);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_BLOWBACK), g_sampling_cfg.blowback_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_IMPROVE), g_sampling_cfg.improve_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_TUBEHOLD), g_sampling_cfg.tube_hold_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_CYCLETIME), g_sampling_cfg.cycle_time_min);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_ANALYSIS), g_sampling_cfg.analysis_time_min);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_FLOWSTART), g_sampling_cfg.flow_start);
    screen_write_u16(screen_settings_addr(SCR_SUB_SAMP_FLOWSTOP), g_sampling_cfg.flow_stop);

    screen_write_u16(screen_settings_addr(SCR_SUB_DELIV_HOUR), g_delivery_cfg.start_hour);
    screen_write_u16(screen_settings_addr(SCR_SUB_DELIV_MIN), g_delivery_cfg.start_min);
    screen_write_u16(screen_settings_addr(SCR_SUB_DELIV_DURATION), g_delivery_cfg.duration_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_DELIV_BACKDRAW), g_delivery_cfg.backdraw_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_DELIV_ENABLE), g_delivery_cfg.enable);

    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_MODE), g_retain_cfg.mode);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_VOLUME), g_retain_cfg.volume_ml);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_PARALLEL), g_retain_cfg.parallel_count);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_MIX), g_retain_cfg.mix_count);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_BLOWBACK), g_retain_cfg.blowback_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_ENABLE), g_retain_cfg.enable);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_ACID), g_retain_cfg.enable_acid);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_TUBEHOLD), g_retain_cfg.tube_hold_sec);
    screen_write_u16(screen_settings_addr(SCR_SUB_RETAIN_BACKDRAW), g_retain_cfg.backdraw_sec);

    screen_write_u16(screen_settings_addr(SCR_SUB_COMM_PROTOCOL), g_comm_cfg.protocol);
    screen_write_u16(screen_settings_addr(SCR_SUB_COMM_ADDR), g_comm_cfg.device_addr);
    screen_write_u16(screen_settings_addr(SCR_SUB_COMM_FLOWLOWER), g_comm_cfg.flow_ad_lower);
    screen_write_u16(screen_settings_addr(SCR_SUB_SYS_AUTORUN), g_system_setting_cfg.auto_run_mode);

    /* 同步主页状态并发送开机跳页（samplingB 使用0x0B） */
    screen_update_status();
    screen_write_u16(0x5240, (uint16_t)scheduler_get_mode());
    screen_write_u16(0x5241, (uint16_t)scheduler_get_phase());
    screen_write_u16(0x5242, scheduler_get_active_bucket());
    screen_write_u16(0x5243, (uint16_t)scheduler_get_total_cycles());
    screen_write_u16(0x5244, (uint16_t)scheduler_get_total_samples());
    screen_write_u16(0x5245, (uint16_t)scheduler_get_total_deliveries());

    screen_switch_page((uint8_t)SCR_PANEL_PAGE_BOOT_HOME);
    screen_switch_page((uint8_t)SCR_PANEL_PAGE_BOOT_HOME);
    s_scr_state.current_page = SCR_PAGE_HOME;
    screen_notify_ready();

    printf("[屏幕] 开机同步完成，已发送主页跳转(0x%02X)\r\n",
           (unsigned int)SCR_PANEL_PAGE_BOOT_HOME);
}

void screen_poll_commands(void)
{
    uint16_t addr;
    uint16_t value;

    while (s_cmd_rd != s_cmd_wr) {
        addr  = s_cmd_buf[s_cmd_rd].addr;
        value = s_cmd_buf[s_cmd_rd].value;
        s_cmd_rd = (s_cmd_rd + 1) % SCR_CMD_BUF_SIZE;
        screen_handle_command(addr, value);
    }
}

void screen_update_status(void)
{
    /* 基本状态回写 */
    screen_write_u16(SCR_ADDR_SYS_STATE, g_state.running);
    screen_write_u16(SCR_ADDR_BUCKET_A,  g_state.bucket_a_state);
    screen_write_u16(SCR_ADDR_BUCKET_B,  g_state.bucket_b_state);

    /* 扩展状态 */
    screen_write_u16(0x5226, g_state.current_bucket);
    screen_write_u16(0x5223, g_state.running ? 1 : 0);
    screen_write_u16(0x522D, g_state.water_a);
    screen_write_u16(0x5231, g_state.water_b);
    screen_write_u16(0x523A, g_state.bottle_current);
    screen_write_u16(0x523B, g_state.bottle_next);
    screen_write_u16(0x523C, g_state.bottle_empty);
}

/* ======================== 主页综合刷新(1s周期) ======================== */
void screen_refresh_home(void)
{
    uint32_t now = g_tmr4_milliseconds;

    /* 非主页不刷新 */
    if (s_scr_state.current_page != SCR_PAGE_HOME)
        return;

    /* 1秒节流 */
    if ((now - s_scr_state.home_refresh_tick) < 1000)
        return;
    s_scr_state.home_refresh_tick = now;

    /* 基本状态 */
    screen_update_status();

    /* 调度器信息 */
    screen_write_u16(0x5240, (uint16_t)scheduler_get_mode());
    screen_write_u16(0x5241, (uint16_t)scheduler_get_phase());
    screen_write_u16(0x5242, scheduler_get_active_bucket());
    screen_write_u16(0x5243, (uint16_t)scheduler_get_total_cycles());
    screen_write_u16(0x5244, (uint16_t)scheduler_get_total_samples());
    screen_write_u16(0x5245, (uint16_t)scheduler_get_total_deliveries());
}

/* ======================== 页面状态查询 ======================== */
uint8_t screen_is_on_home_page(void)
{
    return (s_scr_state.current_page == SCR_PAGE_HOME) ? 1 : 0;
}

scr_page_id_t screen_get_current_page(void)
{
    return s_scr_state.current_page;
}

uint8_t screen_is_ready(void)
{
    return s_scr_state.ready;
}

/* ======================== 外发命令队列 ======================== */
void screen_post_command(scr_out_type_t type, uint16_t addr, uint16_t value)
{
    uint8_t next = (s_out_wr + 1) % SCR_OUT_BUF_SIZE;
    if (next == s_out_rd)
        return;  /* 满了丢弃 */

    s_out_buf[s_out_wr].type  = type;
    s_out_buf[s_out_wr].addr  = addr;
    s_out_buf[s_out_wr].value = value;
    s_out_wr = next;
}

void screen_process_outgoing(void)
{
    while (s_out_rd != s_out_wr) {
        scr_out_cmd_t *c = &s_out_buf[s_out_rd];
        s_out_rd = (s_out_rd + 1) % SCR_OUT_BUF_SIZE;

        switch (c->type) {
        case SCR_OUT_PAGE_SWITCH:
            screen_switch_page((uint8_t)c->value);
            break;
        case SCR_OUT_POPUP:
        case SCR_OUT_WRITE_VAR:
            screen_write_u16(c->addr, c->value);
            break;
        default:
            break;
        }
    }
}

/* ======================== 屏幕就绪通知 ======================== */
void screen_notify_ready(void)
{
    s_scr_state.ready = 1;
    s_scr_state.ready_tick = g_tmr4_milliseconds;
    printf("[屏幕] 就绪\r\n");
}

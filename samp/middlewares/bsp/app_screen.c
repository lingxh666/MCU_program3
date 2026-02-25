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
#include "app_sampling.h"
#include "app_scheduler.h"
#include "app_record_query.h"
#include <stdio.h>

/* ======================== 前向声明 ======================== */
static void screen_isr_callback(uint8_t cmd, uint16_t addr,
                                const uint8_t *data, uint16_t data_len);
static void screen_handle_command(uint16_t addr, uint16_t value);
static void screen_handle_settings(uint8_t sub_cmd, uint16_t value);
static void screen_handle_manual(uint8_t sub_cmd, uint16_t value);
static void screen_handle_confirm(uint8_t sub_cmd, uint16_t value);
static void screen_handle_legacy(uint16_t addr, uint16_t value);

/* ======================== 命令环形缓冲区 ======================== */
static volatile scr_cmd_t s_cmd_buf[SCR_CMD_BUF_SIZE];
static volatile uint8_t   s_cmd_wr = 0;  /* ISR 写指针 */
static volatile uint8_t   s_cmd_rd = 0;  /* Task 读指针 */

/* ======================== ISR 回调 ======================== */
static void screen_isr_callback(uint8_t cmd, uint16_t addr,
                                const uint8_t *data, uint16_t data_len)
{
    uint16_t value;
    uint8_t next;

    /* 只处理写通知 (0x82) 和读应答 (0x83) */
    if (cmd != 0x82 && cmd != 0x83)
        return;
    if (data_len < 2)
        return;

    value = ((uint16_t)data[0] << 8) | data[1];

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
    switch (sub_cmd) {
    /* 采样设置 */
    case SCR_SUB_SAMP_MODE:      g_sampling_cfg.mode = (uint8_t)value; break;
    case SCR_SUB_SAMP_INTERVAL:  g_sampling_cfg.interval_min = value; break;
    case SCR_SUB_SAMP_VOLUME:    g_sampling_cfg.volume_ml = value; break;
    case SCR_SUB_SAMP_BLOWBACK:  g_sampling_cfg.blowback_sec = value; break;
    case SCR_SUB_SAMP_IMPROVE:   g_sampling_cfg.improve_sec = value; break;
    case SCR_SUB_SAMP_TUBEHOLD:  g_sampling_cfg.tube_hold_sec = value; break;
    case SCR_SUB_SAMP_CYCLETIME: g_sampling_cfg.cycle_time_min = value; break;
    case SCR_SUB_SAMP_ANALYSIS:  g_sampling_cfg.analysis_time_min = value; break;
    case SCR_SUB_SAMP_FLOWSTART: g_sampling_cfg.flow_start = value; break;
    case SCR_SUB_SAMP_FLOWSTOP:  g_sampling_cfg.flow_stop = value; break;
    /* 送样设置 */
    case SCR_SUB_DELIV_HOUR:     g_delivery_cfg.start_hour = (uint8_t)value; break;
    case SCR_SUB_DELIV_MIN:      g_delivery_cfg.start_min = (uint8_t)value; break;
    case SCR_SUB_DELIV_DURATION: g_delivery_cfg.duration_sec = value; break;
    case SCR_SUB_DELIV_BACKDRAW: g_delivery_cfg.backdraw_sec = value; break;
    case SCR_SUB_DELIV_ENABLE:   g_delivery_cfg.enable = (uint8_t)value; break;
    /* 留样设置 */
    case SCR_SUB_RETAIN_MODE:    g_retain_cfg.mode = (uint8_t)value; break;
    case SCR_SUB_RETAIN_VOLUME:  g_retain_cfg.volume_ml = value; break;
    case SCR_SUB_RETAIN_PARALLEL:g_retain_cfg.parallel_count = (uint8_t)value; break;
    case SCR_SUB_RETAIN_MIX:     g_retain_cfg.mix_count = (uint8_t)value; break;
    case SCR_SUB_RETAIN_BLOWBACK:g_retain_cfg.blowback_sec = value; break;
    case SCR_SUB_RETAIN_ENABLE:  g_retain_cfg.enable = (uint8_t)value; break;
    case SCR_SUB_RETAIN_ACID:    g_retain_cfg.enable_acid = (uint8_t)value; break;
    case SCR_SUB_RETAIN_TUBEHOLD:g_retain_cfg.tube_hold_sec = value; break;
    case SCR_SUB_RETAIN_BACKDRAW:g_retain_cfg.backdraw_sec = value; break;
    /* 通讯设置 */
    case SCR_SUB_COMM_PROTOCOL:  g_comm_cfg.protocol = (uint8_t)value; break;
    case SCR_SUB_COMM_ADDR:      g_comm_cfg.device_addr = (uint8_t)value; break;
    case SCR_SUB_COMM_FLOWLOWER: g_comm_cfg.flow_ad_lower = value; break;
    default: break;
    }
}

/* ======================== 新体系: 手动控制命令 ======================== */
static void screen_handle_manual(uint8_t sub_cmd, uint16_t value)
{
    switch (sub_cmd) {
    case 0x0A: /* A桶排水 */
        if (value) drain_start(0);
        break;
    case 0x0B: /* B桶排水 */
        if (value) drain_start(1);
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
        printf("[屏幕] 紧急停止\r\n");
        break;
    case SCR_ACT_MANUAL_SAMP:
        sampling_start(g_state.current_bucket, 1);
        break;
    case SCR_ACT_BOTTLE_RESET:
        g_state.bottle_current = 0;
        g_state.bottle_next = 1;
        printf("[屏幕] 留样瓶复位\r\n");
        break;
    case SCR_ACT_LOG_QUERY: {
        /* param: 0x71=采样 0x72=送样 0x73=留样 0x74=电源 0x75=门禁 */
        rq_type_t qt = (rq_type_t)(param - 0x71);
        if ((uint8_t)qt < RQ_TYPE_COUNT)
            record_query_init(qt);
        break;
    }
    case SCR_ACT_LOG_SAMP:
        record_query_page_nav(RQ_SAMPLING, param);
        break;
    case SCR_ACT_LOG_DELIV:
        record_query_page_nav(RQ_DELIVERY, param);
        break;
    case SCR_ACT_LOG_RETAIN:
        record_query_page_nav(RQ_RETAIN, param);
        break;
    case SCR_ACT_LOG_POWER:
        record_query_page_nav(RQ_POWER, param);
        break;
    case SCR_ACT_LOG_DOOR:
        record_query_page_nav(RQ_DOOR, param);
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

    switch (cmd_type) {
    case SCR_CMD_TYPE_SETTINGS:
        screen_handle_settings(sub_cmd, value);
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
    printf("[屏幕] 初始化完成\r\n");
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

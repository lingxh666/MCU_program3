/**
 * @file    app_screen.c
 * @brief   串口屏应用层 — 命令接收分发 + 状态刷新
 *
 * bsp_screen 的回调在 ISR 上下文中调用，这里用环形缓冲区
 * 将命令传递到 Task03 主循环中处理。
 */
#include "app_screen.h"
#include "bsp_screen.h"
#include "app_config.h"
#include "app_sampling.h"
#include "app_scheduler.h"
#include <stdio.h>

/* ======================== 命令环形缓冲区 ======================== */
static volatile scr_cmd_t s_cmd_buf[SCR_CMD_BUF_SIZE];
static volatile uint8_t   s_cmd_wr = 0;  /* ISR 写指针 */
static volatile uint8_t   s_cmd_rd = 0;  /* Task 读指针 */

/* ======================== ISR 回调 ======================== */
static void screen_isr_callback(uint8_t cmd, uint16_t addr,
                                const uint8_t *data, uint16_t data_len)
{
    /* 只处理写通知 (0x82) 和读应答 (0x83) */
    if (cmd != 0x82 && cmd != 0x83)
        return;
    if (data_len < 2)
        return;

    uint16_t value = ((uint16_t)data[0] << 8) | data[1];

    /* 写入环形缓冲区（无锁，单生产者单消费者安全） */
    uint8_t next = (s_cmd_wr + 1) % SCR_CMD_BUF_SIZE;
    if (next == s_cmd_rd)
        return;  /* 满了丢弃 */

    s_cmd_buf[s_cmd_wr].addr  = addr;
    s_cmd_buf[s_cmd_wr].value = value;
    s_cmd_wr = next;
}

/* ======================== 命令分发 ======================== */
static void screen_handle_command(uint16_t addr, uint16_t value)
{
    if (addr == SCR_ADDR_CMD_MANUAL) {
        switch (value) {
        case SCR_CMD_SAMPLING_A:
            sampling_start(0, 1);  /* A桶手动采样 */
            break;
        case SCR_CMD_SAMPLING_B:
            sampling_start(1, 1);  /* B桶手动采样 */
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

/* ======================== 公共接口 ======================== */

void screen_task_init(void)
{
    screen_init();
    screen_set_cmd_callback(screen_isr_callback);
    printf("[屏幕] 初始化完成\r\n");
}

void screen_poll_commands(void)
{
    while (s_cmd_rd != s_cmd_wr) {
        uint16_t addr  = s_cmd_buf[s_cmd_rd].addr;
        uint16_t value = s_cmd_buf[s_cmd_rd].value;
        s_cmd_rd = (s_cmd_rd + 1) % SCR_CMD_BUF_SIZE;
        screen_handle_command(addr, value);
    }
}

void screen_update_status(void)
{
    screen_write_u16(SCR_ADDR_SYS_STATE, g_state.running);
    screen_write_u16(SCR_ADDR_BUCKET_A,  g_state.bucket_a_state);
    screen_write_u16(SCR_ADDR_BUCKET_B,  g_state.bucket_b_state);
}

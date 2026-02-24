/**
 * @file    app_screen.h
 * @brief   串口屏页面逻辑 — 变量地址定义 + 命令处理接口
 */
#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================== 迪文屏变量地址定义 ======================== */
#define SCR_ADDR_PAGE_ID        0x0084  /* 当前页面ID */
#define SCR_ADDR_SYS_STATE      0x1000  /* 系统运行状态 */
#define SCR_ADDR_BUCKET_A       0x1010  /* A桶状态 */
#define SCR_ADDR_BUCKET_B       0x1020  /* B桶状态 */
#define SCR_ADDR_SAMP_MODE      0x2000  /* 采样模式设置 */
#define SCR_ADDR_SAMP_INTERVAL  0x2002  /* 采样间隔 */
#define SCR_ADDR_SAMP_VOLUME    0x2004  /* 采样量 */
#define SCR_ADDR_CMD_MANUAL     0x3000  /* 手动操作命令 */

/* ======================== 手动命令码 ======================== */
#define SCR_CMD_SAMPLING_A      0x0001  /* 手动采样A桶 */
#define SCR_CMD_SAMPLING_B      0x0002  /* 手动采样B桶 */
#define SCR_CMD_DRAIN_A         0x0010  /* 手动排水A桶 */
#define SCR_CMD_DRAIN_B         0x0011  /* 手动排水B桶 */
#define SCR_CMD_SYS_START       0x0020  /* 系统启动 */
#define SCR_CMD_SYS_STOP        0x0021  /* 系统停止 */
#define SCR_CMD_SAMP_ABORT      0x0030  /* 中止采样 */

/* ======================== 命令缓冲区（ISR → Task） ======================== */
#define SCR_CMD_BUF_SIZE  8

typedef struct {
    uint16_t addr;
    uint16_t value;
} scr_cmd_t;

/* ======================== 屏幕任务接口 ======================== */
void screen_task_init(void);
void screen_poll_commands(void);     /* Task03 轮询处理命令 */
void screen_update_status(void);     /* 周期刷新状态显示 */

#ifdef __cplusplus
}
#endif

#endif /* APP_SCREEN_H */

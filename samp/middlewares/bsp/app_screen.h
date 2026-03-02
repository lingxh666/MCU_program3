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

/* ======================== cmd_type 定义 ======================== */
#define SCR_CMD_TYPE_SETTINGS   0x50  /* 设置类命令 */
#define SCR_CMD_TYPE_CALIB      0x51  /* 校准类命令 */
#define SCR_CMD_TYPE_MANUAL     0x52  /* 手动控制命令 */
#define SCR_CMD_TYPE_CONFIRM    0x00  /* 确认/系统命令 */

/* ======================== 采样设置 sub_cmd (cmd_type=0x50) ======================== */
#define SCR_SUB_SAMP_MODE       0x10  /* 采样模式 */
#define SCR_SUB_SAMP_INTERVAL   0x11  /* 采样间隔 */
#define SCR_SUB_SAMP_VOLUME     0x12  /* 单次采样量 */
#define SCR_SUB_SAMP_BLOWBACK   0x13  /* 反吹时间 */
#define SCR_SUB_SAMP_IMPROVE    0x14  /* 提升时间 */
#define SCR_SUB_SAMP_TUBEHOLD   0x15  /* 管存时间 */
#define SCR_SUB_SAMP_CYCLETIME  0x16  /* 周期时间 */
#define SCR_SUB_SAMP_DRAINTIME  0x17  /* 桶排空时间 */
#define SCR_SUB_SAMP_ANALYSIS   0x18  /* 仪器分析时间 */
#define SCR_SUB_SAMP_FLOWSTART  0x1B  /* 流量触发值 */
#define SCR_SUB_SAMP_FLOWSTOP   0x1C  /* 流量停止值 */

/* ======================== 送样设置 sub_cmd ======================== */
#define SCR_SUB_DELIV_HOUR      0x40  /* 送样小时 */
#define SCR_SUB_DELIV_MIN       0x41  /* 送样分钟 */
#define SCR_SUB_DELIV_DURATION  0x42  /* 送样时长 */
#define SCR_SUB_DELIV_BACKDRAW  0x43  /* 回抽间隔 */
#define SCR_SUB_DELIV_ENABLE    0x46  /* 定时启动 */

/* ======================== 留样设置 sub_cmd ======================== */
#define SCR_SUB_RETAIN_MODE     0x60  /* 留样模式 */
#define SCR_SUB_RETAIN_VOLUME   0x61  /* 留样量 */
#define SCR_SUB_RETAIN_PARALLEL 0x62  /* 平行样数 */
#define SCR_SUB_RETAIN_MIX      0x63  /* 混样次数 */
#define SCR_SUB_RETAIN_BLOWBACK 0x64  /* 留样反吹 */
#define SCR_SUB_RETAIN_ENABLE   0x65  /* 是否留样 */
#define SCR_SUB_RETAIN_ACID     0x66  /* 是否加酸 */
#define SCR_SUB_RETAIN_TUBEHOLD 0x68  /* 留样管存 */
#define SCR_SUB_RETAIN_BACKDRAW 0x69  /* 留样回抽 */

/* ======================== 通讯设置 sub_cmd ======================== */
#define SCR_SUB_COMM_PROTOCOL   0xB0  /* 通讯协议 */
#define SCR_SUB_COMM_ADDR       0xB1  /* 设备地址 */
#define SCR_SUB_COMM_FLOWLOWER  0xB2  /* 流量AD下限 */

/* ======================== 系统命令 sub_cmd ======================== */
#define SCR_SUB_SYS_AUTORUN     0xBF  /* 自动运行模式 */
#define SCR_SUB_SYS_WATER_STATION 0xD4  /* 水站模式 */

/* ======================== 确认命令 action 值 ======================== */
#define SCR_ACT_RESET           0x00  /* 系统复位 */
#define SCR_ACT_START           0x02  /* 系统启动(value=0x02) */
#define SCR_ACT_ESTOP           0x03  /* 紧急停止 */
#define SCR_ACT_MANUAL_SAMP     0x23  /* 手动采样执行 */
#define SCR_ACT_MANUAL_DELIV    0x21  /* 手动送样执行 */
#define SCR_ACT_MANUAL_RETAIN   0x25  /* 手动留样执行 */
#define SCR_ACT_BOTTLE_RESET    0x14  /* 留样瓶复位 */
#define SCR_ACT_LOGIN_CONFIRM   0x37  /* 登录确认 */
/* 记录查询 */
#define SCR_ACT_LOG_QUERY       0x81  /* 查询初始化 */
#define SCR_ACT_LOG_SAMP        0x71  /* 采样记录翻页 */
#define SCR_ACT_LOG_DELIV       0x72  /* 送样记录翻页 */
#define SCR_ACT_LOG_RETAIN      0x73  /* 留样记录翻页 */
#define SCR_ACT_LOG_POWER       0x74  /* 电源记录翻页 */
#define SCR_ACT_LOG_DOOR        0x75  /* 门禁记录翻页 */

/* ======================== 状态页回写地址 (0x5200起) ======================== */
#define SCR_STATUS_BASE         0x5200
#define SCR_STATUS_FRAME_LEN    128   /* 状态帧长度 */

/* ======================== 命令缓冲区（ISR → Task） ======================== */
#define SCR_CMD_BUF_SIZE  32

typedef struct {
    uint16_t addr;
    uint16_t value;
} scr_cmd_t;

/* ======================== 页面ID定义 ======================== */
typedef enum {
    SCR_PAGE_HOME     = 0,   /* 主页 */
    SCR_PAGE_SETTINGS = 1,   /* 设置页 */
    SCR_PAGE_MANUAL   = 2,   /* 手动操作页 */
    SCR_PAGE_RECORDS  = 3,   /* 记录查询页 */
    SCR_PAGE_STATUS   = 4,   /* 状态详情页 */
    SCR_PAGE_UNKNOWN  = 0xFF
} scr_page_id_t;

/* samplingB 页面字定义：用于开机后强制跳页 */
#define SCR_PANEL_PAGE_BOOT_HOME  0x0B
#define SCR_PANEL_PAGE_MAIN_HOME  0x15
#define SCR_PANEL_PAGE_ADMIN      0x6F
#define SCR_PANEL_PAGE_OPERATOR   0x97
#define SCR_PANEL_PAGE_SAMPLER    0x99
#define SCR_PANEL_PAGE_LOGIN_FAIL 0x1B

/* ======================== 屏幕状态跟踪 ======================== */
typedef struct {
    scr_page_id_t current_page;    /* 当前页面 */
    uint8_t       ready;           /* 屏幕就绪标志 */
    uint32_t      home_refresh_tick; /* 主页上次刷新时间(ms) */
    uint32_t      ready_tick;      /* 就绪时间戳 */
} scr_state_t;

/* ======================== 外发命令队列 ======================== */
typedef enum {
    SCR_OUT_NONE       = 0,
    SCR_OUT_PAGE_SWITCH,     /* 切换页面 */
    SCR_OUT_POPUP,           /* 弹窗提示 */
    SCR_OUT_WRITE_VAR        /* 写变量 */
} scr_out_type_t;

typedef struct {
    scr_out_type_t type;
    uint16_t addr;
    uint16_t value;
} scr_out_cmd_t;

#define SCR_OUT_BUF_SIZE  8

/* ======================== 屏幕任务接口 ======================== */
void screen_task_init(void);
void screen_bootstrap_on_powerup(void); /* 开机同步配置并跳转主页 */
void screen_poll_commands(void);     /* Task03 轮询处理命令 */
void screen_update_status(void);     /* 周期刷新状态显示 */
void screen_refresh_home(void);      /* 主页综合刷新(1s周期) */

/* 页面状态查询 */
uint8_t screen_is_on_home_page(void);
scr_page_id_t screen_get_current_page(void);
uint8_t screen_is_ready(void);

/* 外发命令 */
void screen_post_command(scr_out_type_t type, uint16_t addr, uint16_t value);
void screen_process_outgoing(void);  /* Task03 轮询发送 */

/* 屏幕就绪通知(ISR安全) */
void screen_notify_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCREEN_H */

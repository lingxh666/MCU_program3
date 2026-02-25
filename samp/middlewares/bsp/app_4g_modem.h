/**
 * @file    app_4g_modem.h
 * @brief   4G模组AT指令引擎 — 初始化序列、信号查询、时间同步
 */
#ifndef APP_4G_MODEM_H
#define APP_4G_MODEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 模组状态 */
typedef enum {
    MODEM_STATE_OFF = 0,     /* 未上电 */
    MODEM_STATE_BOOTING,     /* 上电等待 */
    MODEM_STATE_AT_CHECK,    /* AT握手 */
    MODEM_STATE_SIM_CHECK,   /* SIM卡检测 */
    MODEM_STATE_NET_REG,     /* 网络注册 */
    MODEM_STATE_READY,       /* 就绪 */
    MODEM_STATE_ERROR        /* 错误 */
} modem_state_t;

/* AT指令应答结果 */
typedef enum {
    AT_RESULT_NONE = 0,
    AT_RESULT_OK,
    AT_RESULT_ERROR,
    AT_RESULT_TIMEOUT
} at_result_t;

/* 模组信息 */
typedef struct {
    modem_state_t state;
    uint8_t  signal_quality;   /* CSQ值 0-31 */
    uint8_t  net_registered;   /* 网络已注册 */
    uint8_t  init_retry;       /* 初始化重试次数 */
    uint32_t last_csq_tick;    /* 上次信号查询时间 */
    uint32_t last_error_tick;  /* 上次错误时间 */
} modem_info_t;

extern modem_info_t g_modem;

/* AT指令收发缓冲区大小 */
#define AT_BUF_SIZE  512

/* 初始化 */
void modem_init(void);

/* 非阻塞轮询(Task05主循环调用) */
void modem_poll(void);

/* AT指令发送+等待应答(阻塞，带超时+重试) */
at_result_t modem_send_at(const char *cmd, const char *expect,
                          uint8_t retry, uint16_t timeout_ms);

/* 获取AT应答缓冲区(用于解析) */
const char *modem_get_response(void);

/* 信号质量查询 */
uint8_t modem_get_csq(void);

/* 时间同步(AT+CCLK? → RTC校准) */
uint8_t modem_sync_time(void);

/* 模组复位 */
void modem_reset(void);

/* 状态查询 */
modem_state_t modem_get_state(void);
uint8_t modem_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_4G_MODEM_H */

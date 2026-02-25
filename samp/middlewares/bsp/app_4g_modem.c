/**
 * @file    app_4g_modem.c
 * @brief   4G模组AT指令引擎 — 初始化序列、信号查询、时间同步
 */
#include "app_4g_modem.h"
#include "bsp_uart.h"
#include "bsp_rtc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* 外部定时器 */
extern volatile uint32_t g_tmr4_milliseconds;

/* 全局模组信息 */
modem_info_t g_modem;

/* AT收发缓冲区 */
static char s_at_buf[AT_BUF_SIZE];
static uint16_t s_at_len;

/* 时间常量(ms) */
#define CSQ_INTERVAL_MS    60000u   /* 信号查询间隔 */
#define ERROR_RETRY_MS     60000u   /* 错误后重试间隔 */
#define BOOT_WAIT_MS       3000u    /* 上电等待时间 */
/* 初始化最大重试 */
#define INIT_MAX_RETRY     3

/* ======================== AT指令收发 ======================== */

at_result_t modem_send_at(const char *cmd, const char *expect,
                          uint8_t retry, uint16_t timeout_ms)
{
    uint8_t rx_tmp[AT_BUF_SIZE];
    uint16_t len;
    uint32_t start;

    while (retry > 0) {
        memset(s_at_buf, 0, sizeof(s_at_buf));
        s_at_len = 0;

        /* 发送AT指令 */
        bsp_uart_send(UART_PORT_4G,
                      (const uint8_t *)cmd, (uint16_t)strlen(cmd));

        /* 等待应答 */
        start = g_tmr4_milliseconds;
        while ((g_tmr4_milliseconds - start) < timeout_ms) {
            if (bsp_uart_rx_available(UART_PORT_4G)) {
                len = bsp_uart_get_rxdata(UART_PORT_4G,
                                          rx_tmp, sizeof(rx_tmp) - 1);
                if (len > 0 && (s_at_len + len) < AT_BUF_SIZE) {
                    memcpy(&s_at_buf[s_at_len], rx_tmp, len);
                    s_at_len += len;
                    s_at_buf[s_at_len] = '\0';
                }
                /* 检查期望字符串 */
                if (expect && strstr(s_at_buf, expect))
                    return AT_RESULT_OK;
                if (strstr(s_at_buf, "ERROR"))
                    return AT_RESULT_ERROR;
            }
        }
        retry--;
    }
    return AT_RESULT_TIMEOUT;
}

const char *modem_get_response(void)
{
    return s_at_buf;
}

/* ======================== 模组复位 ======================== */
void modem_reset(void)
{
    bsp_uart_send(UART_PORT_4G,
                  (const uint8_t *)"AT+CFUN=1,1\r\n", 13);
    g_modem.state = MODEM_STATE_BOOTING;
    printf("[4G] 模组复位\r\n");
}

/* ======================== 初始化 ======================== */
void modem_init(void)
{
    memset(&g_modem, 0, sizeof(g_modem));
    g_modem.state = MODEM_STATE_BOOTING;
    s_at_len = 0;
    printf("[4G] 初始化开始\r\n");
}

/* ======================== 信号质量查询 ======================== */
uint8_t modem_get_csq(void)
{
    const char *p;
    if (modem_send_at("AT+CSQ\r\n", "+CSQ:", 2, 2000) != AT_RESULT_OK)
        return 0;

    p = strstr(s_at_buf, "+CSQ:");
    if (p) {
        p += 5;
        while (*p == ' ') p++;
        g_modem.signal_quality = (uint8_t)atoi(p);
    }
    return g_modem.signal_quality;
}

/* 从字符串解析2位十进制数 */
static int parse_2digit(const char *p)
{
    return (p[0] - '0') * 10 + (p[1] - '0');
}

/* ======================== 时间同步 ======================== */
uint8_t modem_sync_time(void)
{
    const char *p;
    int year, month, day, hour, min, sec;

    if (modem_send_at("AT+CCLK?\r\n", "+CCLK:", 3, 2000) != AT_RESULT_OK)
        return 0;

    p = strstr(s_at_buf, "+CCLK:");
    if (!p) return 0;

    /* 跳到引号后: "YY/MM/DD,HH:MM:SS+ZZ" */
    p = strchr(p, '"');
    if (!p) return 0;
    p++;

    /* 解析 YY/MM/DD,HH:MM:SS */
    year  = parse_2digit(p);
    month = parse_2digit(p + 3);
    day   = parse_2digit(p + 6);
    hour  = parse_2digit(p + 9);
    min   = parse_2digit(p + 12);
    sec   = parse_2digit(p + 15);

    /* UTC+8 北京时间校正 */
    hour += 8;
    if (hour >= 24) {
        hour -= 24;
        day++;
    }

    /* 写入RTC (year为2000年偏移量) */
    rtc_set_time((uint8_t)year, (uint8_t)month, (uint8_t)day,
                 (uint8_t)hour, (uint8_t)min, (uint8_t)sec);

    printf("[4G] 时间同步: %04d-%02d-%02d %02d:%02d:%02d\r\n",
           2000+year, month, day, hour, min, sec);
    return 1;
}

/* ======================== 非阻塞轮询 ======================== */
void modem_poll(void)
{
    uint32_t now = g_tmr4_milliseconds;

    switch (g_modem.state) {
    case MODEM_STATE_BOOTING:
        /* 上电等待 */
        if ((now - g_modem.last_error_tick) < BOOT_WAIT_MS)
            return;
        g_modem.state = MODEM_STATE_AT_CHECK;
        break;

    case MODEM_STATE_AT_CHECK:
        if (modem_send_at("AT\r\n", "OK", 3, 1000) == AT_RESULT_OK) {
            printf("[4G] AT握手成功\r\n");
            g_modem.state = MODEM_STATE_SIM_CHECK;
        } else {
            g_modem.init_retry++;
            if (g_modem.init_retry >= INIT_MAX_RETRY) {
                g_modem.state = MODEM_STATE_ERROR;
                g_modem.last_error_tick = now;
                printf("[4G] AT握手失败\r\n");
            }
        }
        break;

    case MODEM_STATE_SIM_CHECK:
        if (modem_send_at("AT+CPIN?\r\n", "READY", 3, 2000)
            == AT_RESULT_OK)
        {
            printf("[4G] SIM卡就绪\r\n");
            g_modem.state = MODEM_STATE_NET_REG;
        } else {
            g_modem.state = MODEM_STATE_ERROR;
            g_modem.last_error_tick = now;
            printf("[4G] SIM卡异常\r\n");
        }
        break;

    case MODEM_STATE_NET_REG:
        if (modem_send_at("AT+CEREG?\r\n", "+CEREG: 0,1",
                          5, 2000) == AT_RESULT_OK)
        {
            printf("[4G] 网络注册成功\r\n");
            modem_get_csq();
            modem_sync_time();
            g_modem.net_registered = 1;
            g_modem.state = MODEM_STATE_READY;
            g_modem.last_csq_tick = now;
        } else {
            g_modem.state = MODEM_STATE_ERROR;
            g_modem.last_error_tick = now;
            printf("[4G] 网络注册失败\r\n");
        }
        break;

    case MODEM_STATE_READY:
        /* 周期信号查询 */
        if ((now - g_modem.last_csq_tick) >= CSQ_INTERVAL_MS) {
            modem_get_csq();
            g_modem.last_csq_tick = now;
        }
        break;

    case MODEM_STATE_ERROR:
        /* 错误后60秒重试 */
        if ((now - g_modem.last_error_tick) >= ERROR_RETRY_MS) {
            g_modem.init_retry = 0;
            g_modem.state = MODEM_STATE_BOOTING;
            g_modem.last_error_tick = now;
            printf("[4G] 重试初始化\r\n");
        }
        break;

    default:
        break;
    }
}

/* ======================== 状态查询 ======================== */
modem_state_t modem_get_state(void)
{
    return g_modem.state;
}

uint8_t modem_is_ready(void)
{
    return (g_modem.state == MODEM_STATE_READY) ? 1 : 0;
}

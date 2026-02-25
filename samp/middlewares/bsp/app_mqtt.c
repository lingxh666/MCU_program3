/**
 * @file    app_mqtt.c
 * @brief   MQTT连接状态机 — 连接/断开/重连 + 状态上报 + 远程命令
 */
#include "app_mqtt.h"
#include "app_4g_modem.h"
#include "app_config.h"
#include "app_scheduler.h"
#include <stdio.h>
#include <string.h>

/* 外部定时器 */
extern volatile uint32_t g_tmr4_milliseconds;

/* 全局MQTT信息 */
mqtt_info_t g_mqtt;

/* MQTT主题 */
#define MQTT_TOPIC_DATA  "CYJDAT"
#define MQTT_TOPIC_SET   "CYJSET"

/* 发送缓冲区 */
#define MQTT_MSG_SIZE  256
static char s_msg_buf[MQTT_MSG_SIZE];

/* ======================== 辅助：MQTT发布 ======================== */
static uint8_t mqtt_publish(const char *topic, const char *message)
{
    char cmd[350];
    uint16_t msg_len = (uint16_t)strlen(message);

    sprintf(cmd, "AT+MQTTPUB=0,\"%s\",0,0,0,%u,\"%s\"\r\n",
            topic, (unsigned)msg_len, message);

    return (modem_send_at(cmd, "OK", 2, 2000) == AT_RESULT_OK) ? 1 : 0;
}

/* ======================== 连接/断开 ======================== */
uint8_t mqtt_connect(void)
{
    char cmd[200];

    /* 先断开旧连接 */
    modem_send_at("AT+MQTTDISC=0\r\n", "OK", 1, 1000);

    /* 连接 */
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",1883,\"%s\"\r\n",
            g_comm_cfg.mqtt_ip, g_comm_cfg.device_id);

    if (modem_send_at(cmd, "+MQTTURC:", 3, 3000) != AT_RESULT_OK) {
        printf("[MQTT] 连接失败\r\n");
        return 0;
    }

    /* 验证状态 */
    if (modem_send_at("AT+MQTTSTATE=0\r\n", "MQTTSTATE: 2",
                      2, 2000) != AT_RESULT_OK)
    {
        printf("[MQTT] 状态验证失败\r\n");
        return 0;
    }

    g_mqtt.connected = 1;
    printf("[MQTT] 连接成功\r\n");
    return 1;
}

void mqtt_disconnect(void)
{
    modem_send_at("AT+MQTTDISC=0\r\n", "OK", 1, 1000);
    g_mqtt.connected = 0;
    g_mqtt.subscribed = 0;
    g_mqtt.state = MQTT_STATE_IDLE;
}

uint8_t mqtt_is_connected(void)
{
    return g_mqtt.connected;
}

/* ======================== 状态上报 ======================== */
void mqtt_send_status_all(void)
{
    /* 水量状态 */
    sprintf(s_msg_buf,
            "{\"ID\":\"%s\",\"T\":\"water\",\"WA\":%u,\"WB\":%u}",
            g_comm_cfg.device_id,
            (unsigned)g_state.water_a,
            (unsigned)g_state.water_b);
    mqtt_publish(MQTT_TOPIC_DATA, s_msg_buf);

    /* 采样状态 */
    sprintf(s_msg_buf,
            "{\"ID\":\"%s\",\"T\":\"samp\",\"M\":%u,\"P\":%u,\"BK\":%u}",
            g_comm_cfg.device_id,
            (unsigned)scheduler_get_mode(),
            (unsigned)scheduler_get_phase(),
            (unsigned)scheduler_get_active_bucket());
    mqtt_publish(MQTT_TOPIC_DATA, s_msg_buf);

    printf("[MQTT] 状态上报完成\r\n");
}

void mqtt_send_settings_all(void)
{
    /* 采样配置 */
    sprintf(s_msg_buf,
            "{\"ID\":\"%s\",\"T\":\"cfg_s\","
            "\"mode\":%u,\"intv\":%u,\"vol\":%u,\"cyc\":%u}",
            g_comm_cfg.device_id,
            (unsigned)g_sampling_cfg.mode,
            (unsigned)g_sampling_cfg.interval_min,
            (unsigned)g_sampling_cfg.volume_ml,
            (unsigned)g_sampling_cfg.cycle_time_min);
    mqtt_publish(MQTT_TOPIC_DATA, s_msg_buf);

    /* 送样配置 */
    sprintf(s_msg_buf,
            "{\"ID\":\"%s\",\"T\":\"cfg_d\","
            "\"hour\":%u,\"min\":%u,\"dur\":%u}",
            g_comm_cfg.device_id,
            (unsigned)g_delivery_cfg.start_hour,
            (unsigned)g_delivery_cfg.start_min,
            (unsigned)g_delivery_cfg.duration_sec);
    mqtt_publish(MQTT_TOPIC_DATA, s_msg_buf);

    printf("[MQTT] 设置上报完成\r\n");
}

/* ======================== 接收数据处理 ======================== */
void mqtt_process_rx(const char *data, uint16_t len)
{
    (void)len;
    if (!data) return;

    /* 检查是否为MQTT推送消息 */
    if (strstr(data, "+MQTTURC: \"publish\"") == NULL)
        return;

    /* 简单命令解析(后续扩展) */
    printf("[MQTT] 收到推送: %.64s\r\n", data);
}

/* ======================== 初始化 ======================== */
void mqtt_init(void)
{
    memset(&g_mqtt, 0, sizeof(g_mqtt));
    g_mqtt.state = MQTT_STATE_IDLE;
    printf("[MQTT] 初始化\r\n");
}

/* ======================== 非阻塞轮询 ======================== */
void mqtt_poll(void)
{
    uint32_t now = g_tmr4_milliseconds;

    /* 模组未就绪则不处理 */
    if (!modem_is_ready())
        return;

    switch (g_mqtt.state) {
    case MQTT_STATE_IDLE:
        g_mqtt.state = MQTT_STATE_CONNECTING;
        break;

    case MQTT_STATE_CONNECTING:
        if (mqtt_connect()) {
            g_mqtt.state = MQTT_STATE_SUBSCRIBING;
        } else {
            g_mqtt.state = MQTT_STATE_ERROR;
            g_mqtt.last_reconnect_tick = now;
        }
        break;

    case MQTT_STATE_SUBSCRIBING:
        if (modem_send_at(
                "AT+MQTTSUB=0,\"" MQTT_TOPIC_SET "\",0\r\n",
                "OK", 2, 2000) == AT_RESULT_OK)
        {
            g_mqtt.subscribed = 1;
            g_mqtt.state = MQTT_STATE_READY;
            g_mqtt.last_status_tick = now;
            g_mqtt.last_settings_tick = now;
            printf("[MQTT] 订阅成功，就绪\r\n");
        } else {
            g_mqtt.state = MQTT_STATE_ERROR;
            g_mqtt.last_reconnect_tick = now;
        }
        break;

    case MQTT_STATE_READY:
        /* 周期状态上报 */
        if ((now - g_mqtt.last_status_tick)
            >= MQTT_STATUS_INTERVAL_MS)
        {
            mqtt_send_status_all();
            g_mqtt.last_status_tick = now;
        }
        /* 周期设置上报 */
        if ((now - g_mqtt.last_settings_tick)
            >= MQTT_SETTINGS_INTERVAL_MS)
        {
            mqtt_send_settings_all();
            g_mqtt.last_settings_tick = now;
        }
        break;

    case MQTT_STATE_ERROR:
        /* 60秒后重连 */
        if ((now - g_mqtt.last_reconnect_tick)
            >= MQTT_RECONNECT_INTERVAL_MS)
        {
            g_mqtt.reconnect_count++;
            g_mqtt.state = MQTT_STATE_CONNECTING;
            printf("[MQTT] 重连 #%u\r\n",
                   (unsigned)g_mqtt.reconnect_count);
        }
        break;

    default:
        break;
    }
}

/* ======================== 状态查询 ======================== */
mqtt_state_t mqtt_get_state(void)
{
    return g_mqtt.state;
}

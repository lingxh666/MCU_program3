/**
 * @file    app_mqtt.h
 * @brief   MQTT连接状态机 — 连接/断开/重连 + 状态上报 + 远程命令
 */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MQTT连接状态 */
typedef enum {
    MQTT_STATE_IDLE = 0,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_SUBSCRIBING,
    MQTT_STATE_READY,
    MQTT_STATE_ERROR
} mqtt_state_t;

/* MQTT信息 */
typedef struct {
    mqtt_state_t state;
    uint8_t  connected;
    uint8_t  subscribed;
    uint8_t  reconnect_count;
    uint32_t last_status_tick;    /* 上次状态上报时间(ms) */
    uint32_t last_settings_tick;  /* 上次设置上报时间(ms) */
    uint32_t last_reconnect_tick; /* 上次重连时间(ms) */
} mqtt_info_t;

extern mqtt_info_t g_mqtt;

/* 上报间隔(ms) */
#define MQTT_STATUS_INTERVAL_MS    (15u * 60u * 1000u)  /* 15分钟 */
#define MQTT_SETTINGS_INTERVAL_MS  (60u * 60u * 1000u)  /* 1小时 */
#define MQTT_RECONNECT_INTERVAL_MS 60000u               /* 60秒 */

/* 初始化 */
void mqtt_init(void);

/* 非阻塞轮询(Task05主循环调用) */
void mqtt_poll(void);

/* 连接/断开 */
uint8_t mqtt_connect(void);
void    mqtt_disconnect(void);
uint8_t mqtt_is_connected(void);

/* 状态上报 */
void mqtt_send_status_all(void);
void mqtt_send_settings_all(void);

/* 接收数据处理 */
void mqtt_process_rx(const char *data, uint16_t len);

/* 状态查询 */
mqtt_state_t mqtt_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MQTT_H */

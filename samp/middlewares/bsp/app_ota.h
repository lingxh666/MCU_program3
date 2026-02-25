/**
 * @file    app_ota.h
 * @brief   OTA升级接口 — 4G MQTT OTA + USB OTA 共用状态机
 */
#ifndef APP_OTA_H
#define APP_OTA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flash布局（AT32F435 Bank1）
 *   Bootloader : 0x08000000 ~ 0x08003FFF (16KB)
 *   APP        : 0x08004000 ~ 0x0803FFFF (240KB)
 *   OTA临时区  : 0x08040000 ~ 0x0807EFFF (252KB)
 *   升级标志   : 0x0807F000 (4KB扇区)
 */
#define OTA_APP_ADDR       ((uint32_t)0x08004000)
#define OTA_TEMP_ADDR      ((uint32_t)0x08040000)
#define OTA_TEMP_SIZE      ((uint32_t)0x0003F000)  /* 252KB */
#define OTA_MAX_FW_SIZE    OTA_TEMP_SIZE

/* OTA数据包格式（与samplingB兼容） */
#define OTA_PKT_HEADER     0xAA55
#define OTA_PKT_DATA_SIZE  256
#define OTA_PKT_HEAD_SIZE  9
#define OTA_PKT_TOTAL_SIZE (OTA_PKT_HEAD_SIZE + OTA_PKT_DATA_SIZE)

/* 超时、重试与缓冲区 */
#define OTA_TIMEOUT_MS     30000u
#define OTA_MAX_RETRY      3u
#define OTA_BUF_SIZE       2048u

/* OTA状态 */
typedef enum {
    OTA_IDLE = 0,       /* 空闲 */
    OTA_INIT,           /* 初始化：擦除Flash */
    OTA_WAIT_DATA,      /* 等待数据包 */
    OTA_PROCESS,        /* 处理数据包 */
    OTA_WRITE,          /* 写入Flash */
    OTA_VERIFY,         /* 校验固件 */
    OTA_COMPLETE,       /* 升级完成 */
    OTA_ERROR           /* 错误 */
} ota_state_t;

/* OTA来源 */
typedef enum {
    OTA_SRC_NONE = 0,
    OTA_SRC_MQTT,       /* 4G MQTT远程升级 */
    OTA_SRC_USB         /* USB U盘升级 */
} ota_source_t;

/* 数据包结构 */
typedef struct {
    uint16_t header;                  /* 包头 0xAA55 */
    uint16_t packet_id;               /* 包序号 */
    uint16_t total_packets;           /* 总包数 */
    uint16_t data_len;                /* 数据长度 */
    uint8_t  checksum;                /* 校验和 */
    uint8_t  data[OTA_PKT_DATA_SIZE]; /* 有效数据 */
} __attribute__((packed)) ota_packet_t;

/* OTA控制块 */
typedef struct {
    ota_state_t  state;
    ota_source_t source;
    uint32_t expected_pkt;     /* 期望包序号 */
    uint32_t total_packets;    /* 总包数 */
    uint32_t flash_addr;       /* 当前写入地址 */
    uint8_t  buffer[OTA_BUF_SIZE]; /* 写入缓冲区 */
    uint16_t buf_pos;          /* 缓冲区位置 */
    uint32_t total_size;       /* 固件总大小 */
    uint32_t received_size;    /* 已接收大小 */
    uint32_t file_crc;         /* 期望CRC16 */
    uint32_t retry_count;      /* 重试计数 */
    uint32_t last_tick;        /* 上次接收时间戳 */
} ota_ctrl_t;

extern ota_ctrl_t g_ota;

/* ======================== 接口 ======================== */

/* 初始化 */
void ota_init(void);

/* 启动OTA（由MQTT指令或USB检测触发） */
uint8_t ota_start(ota_source_t src, uint32_t fw_size, uint32_t fw_crc);

/* 喂入一个数据包（MQTT收到后调用） */
uint8_t ota_feed_packet(const uint8_t *data, uint16_t len);

/* USB OTA：喂入原始数据块（从文件读取） */
uint8_t ota_feed_raw(const uint8_t *data, uint16_t len);

/* 状态机轮询（Task05调用） */
void ota_poll(void);

/* 查询 */
ota_state_t ota_get_state(void);
uint8_t ota_is_active(void);
uint8_t ota_get_progress(void);  /* 0~100 */

/* 中止 */
void ota_abort(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OTA_H */

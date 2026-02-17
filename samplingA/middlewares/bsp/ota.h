#ifndef __OTA_H
#define __OTA_H

#include "at32f403a_407_wk_config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "flash.h"
#include "rtc.h"
#include <stdint.h>
#include <stdbool.h>

// OTA配置参数
#define OTA_PACKET_SIZE         256      // 每个数据包的有效数据大小
#define OTA_PACKET_HEADER_SIZE  9        // 数据包头大小
#define OTA_TOTAL_PACKET_SIZE   265      // 总包大小(9+256)
#define OTA_BASE64_SIZE         356      // Base64编码后大小
#define OTA_BUFFER_SIZE         2048     // Flash写入缓冲区大小
#define OTA_MAX_RETRY_COUNT     3        // 最大重试次数
#define OTA_TIMEOUT_MS          30000    // 超时时间(30秒)
#define OTA_WRITE_ADDR          TEMPLATE_START_ADDR  // OTA临时存储区起始地址 0x08041000

// 数据包头标识
#define OTA_PACKET_HEADER       0xAA55

// OTA状态枚举
typedef enum {
    OTA_STATE_IDLE = 0,        // 空闲状态
    OTA_STATE_INIT,            // 初始化OTA流程
    OTA_STATE_WAIT_DATA,       // 等待数据包
    OTA_STATE_PROCESS_PACKET,  // 处理数据包
    OTA_STATE_WRITE_FLASH,     // 写入Flash
    OTA_STATE_VERIFY,          // 验证完整性
    OTA_STATE_COMPLETE,        // 升级完成
    OTA_STATE_ERROR            // 错误状态
} OtaState;

// 数据包结构定义（小端序）
typedef struct {
    uint16_t header;         // 包头 (0xAA55)
    uint16_t packet_id;      // 包序号
    uint16_t total_packets;  // 总包数
    uint16_t data_len;       // 数据长度
    uint8_t  checksum;       // 校验和
    uint8_t  data[OTA_PACKET_SIZE]; // 数据
} __attribute__((packed)) OtaPacket;

// OTA控制结构体
typedef struct {
    OtaState state;                    // 当前状态
    uint32_t expected_packet_id;       // 期望的包序号
    uint32_t total_packets;            // 总包数
    uint32_t flash_addr;               // Flash写入地址
    uint8_t  buffer[OTA_BUFFER_SIZE];  // 数据缓冲区
    uint16_t buffer_pos;               // 缓冲区位置
    uint32_t retry_count;              // 重试计数
    uint32_t last_tick;                // 上次接收时间戳
    uint32_t total_size;               // 总数据大小
    uint32_t received_size;            // 已接收大小
    uint32_t file_checksum;            // 文件校验和
} OtaControl;

// 主要函数声明
uint8_t OTA_EraseTemplateArea(void);
uint16_t OTA_CalculateCRC16(uint32_t start_addr, uint32_t length);
void OTA_Process(char *Buf, usart_type *usart_x);
uint8_t OTA_CheckStartCommand(char *recv_buf);
void OTA_SendAck(uint16_t packet_id, uint8_t status, usart_type *usart_x);
uint8_t OTA_ParseAndProcessData(char *buf, OtaControl *ota_ctrl);
void OTA_WriteBufferToFlash(OtaControl *ota_ctrl);
uint8_t OTA_VerifyPacket(OtaPacket *packet);
void OTA_Complete(usart_type *usart_x);
void OTA_Error(usart_type *usart_x);
uint8_t OTA_VerifyFirmware(OtaControl *ota_ctrl);
OtaState OTA_GetState(void);
uint8_t OTA_HasPendingPayload(void);

// Base64解码函数声明
int OTA_Base64Decode(const char *src, uint8_t *dst, int dst_len);

//===================== 从ml307r.h移植的函数声明 =====================

// MQTT相关函数
uint8_t MqttInit(char *Buf, usart_type *usart_x);
void MqttSend(char *Buf, usart_type *usart_x);
void MqttSendStatusAll(char *Buf, usart_type *usart_x);    // 15分钟状态上报(4条)
void MqttSendSettingsAll(char *Buf, usart_type *usart_x);  // 2小时设置上报(5条)
void MqttSendRecentEvents10m(usart_type *usart_x);         // 10分钟内事件批量上报
int TestATCommand(usart_type *usart_x, char *Buf, char *sendStr, char *recStr, uint8_t retryNum, uint32_t time);
void ResetModle(usart_type *usart_x);

// IMEI获取函数
uint8_t IMEI_GetAndSetIDSET(usart_type *usart_x, char *Buf);

// 时间相关函数
void timesyc(usart_type *usart_x, char *Buf);
void adjust_to_beijing_time(calendar_type *time_struct);
int days_in_month(int month, int year);

// IP地址解析函数
int extract_ip_from_setmcu(const char *str, char *ip_out, int max_len);

// 调试相关函数
uint8_t CheckDebugCommand(char *recv_buf);
uint8_t IsInDebugMode(void);
uint8_t ProcessDebugCache(usart_type *usart_x);
uint8_t MqttDebugSend(usart_type *usart_x);

// 工具函数
void filter_spaces(char *str);
uint32_t extract_number_from_response(char *buf, const char *prefix, const char *suffix);

// 外部变量声明
extern uint8_t debug_mode;
extern char debug_buffer[];
extern uint16_t debug_buffer_index;
extern SemaphoreHandle_t debug_mutex;

#endif /* __OTA_H */



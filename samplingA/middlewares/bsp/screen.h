#ifndef __SCREEN_H__
#define __SCREEN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f403a_407.h"
#include "FreeRTOS.h"


// UartMessage 已在 freertos_app.h 中定义

/* 子命令范围分组（集中维护，避免魔数分散） */
#define SCR_SUB_SAMPLING_BASE_MIN   0x10
#define SCR_SUB_SAMPLING_BASE_MAX   0x1C
#define SCR_SUB_SAMPLING_SLOT_MIN   0x1D
#define SCR_SUB_SAMPLING_SLOT_MAX   0x35
#define SCR_SUB_DELIVERY_MIN        0x40
#define SCR_SUB_DELIVERY_MAX        0x46
#define SCR_SUB_STORAGE_MIN         0x60
#define SCR_SUB_STORAGE_MAX         0x69
#define SCR_SUB_STORAGE_CH_MIN      0x6A
#define SCR_SUB_STORAGE_CH_MAX      0x81
#define SCR_SUB_CHANNEL_AD_MIN      0x8A
#define SCR_SUB_CHANNEL_AD_MAX      0xA1
#define SCR_SUB_COMM_MIN            0xB0
#define SCR_SUB_COMM_MAX            0xBB
#define SCR_SUB_ACCESS_BF           0xBF
#define SCR_SUB_ACCESS_D4           0xD4


/* 测试采样函数声明 */
uint8_t test_sampling_execute(void);
uint8_t test_delivery_execute(void);
uint8_t test_retention_execute(void);
uint8_t test_instant_delivery_execute(void);
uint8_t test_instant_retention_execute(void);

/* TODO: 屏幕变量同步占位接口（等待变量地址列表提供后实现） */
void screen_sync_all_vars_to_panel(void);

/* 屏幕消息分发器和处理函数 */
// 屏幕消息分发器和处理函数
void screen_message_dispatcher(void);
void screen_dispatcher_stop(void);
void screen_dispatcher_resume(void);
uint8_t screen_dispatcher_is_stopped(void);
void handle_login(UartMessage *msg);
void handle_single_command(UartMessage *msg);
void handle_sampling_settings(UartMessage *msg);

void handle_delivery_settings(UartMessage *msg);
void handle_calibration_settings(UartMessage *msg);
void handle_storage_settings(UartMessage *msg);
void handle_storage_channel(UartMessage *msg);
void handle_channel_ad(UartMessage *msg);
void handle_comm_settings(UartMessage *msg);
void handle_access_settings(UartMessage *msg);
void handle_software_version(UartMessage *msg);

// 原有函数
void login(void);
void SingleCommand(void);
void Screen_init(void);
uint8_t screen_send_notify(usart_type *usart_x, const uint8_t *buf, uint8_t len, uint8_t retryNum);

//uint8_t screen_send(usart_type *usart_x, uint8_t const *buf, uint8_t len, uint8_t retryNum);
void write_sampling_settings_page(void);
void write_delivery_settings_page(void) ;
void write_retain_settings_page(void);
void write_retain_channel_settings_page(void);
void write_retain_channel_data_page(void);
void write_retain_channel_current_page(void);
void write_retain_channel_channelCals_page(void);
void write_retain_channel_comm_page(void);
void write_retain_channel_AutoRunMode_page(void);
void write_retain_channel_WaterStationMode_page(void);
void write_retain_channel_calibration_page(void);
void write_software_version_page(void);
void write_begin_page(void);
void screen_send_time_update(void);  // 独立发送时间更新(地址5220)，不受页面状态影响
uint8_t screen_is_on_home_page(void);
uint8_t screen_is_on_channel_settings_page(void);
uint8_t screen_is_on_advanced_settings_page(void);
void Screen_begin(void);

/* 从ISR唤醒等待确认的屏幕发送任务 */
void screen_ack_notify_from_isr(BaseType_t *pxHigherPriorityTaskWoken);
#ifdef __cplusplus
}
#endif

#endif

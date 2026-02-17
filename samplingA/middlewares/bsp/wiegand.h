#ifndef __WIEGAND_H__
#define __WIEGAND_H__

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"

// 韦根协议配置
#define WIEGAND_TIMEOUT_MS          200     // 韦根数据接收超时时间(毫秒)
#define WIEGAND_MAX_BITS            64      // 最大支持位数
#define WIEGAND_COMMON_BITS         26      // 常见26位格式

// 韦根数据状态
typedef enum {
    WIEGAND_STATE_IDLE = 0,         // 空闲状态
    WIEGAND_STATE_RECEIVING,        // 正在接收数据
    WIEGAND_STATE_COMPLETE,         // 数据接收完成
    WIEGAND_STATE_ERROR             // 接收错误
} WiegandState_t;

// 韦根数据结构
typedef struct {
    uint64_t raw_data;              // 原始数据
    uint8_t bit_count;              // 接收到的位数
    uint32_t last_bit_time;         // 最后一位接收时间
    WiegandState_t state;           // 当前状态
    bool data_ready;                // 数据就绪标志
} WiegandData_t;

// 26位韦根卡号解析结果
typedef struct {
    uint32_t card_id;               // 卡号
    uint8_t facility_code;          // 设施码
    bool parity_ok;                 // 校验位正确
    bool valid;                     // 数据有效
} Wiegand26_t;

// 韦根模块函数声明
void wiegand_init(void);
void wiegand_reset(void);
bool wiegand_is_data_ready(void);
WiegandData_t wiegand_get_raw_data(void);
Wiegand26_t wiegand_parse_26bit(void);
uint32_t wiegand_get_card_id(void);
void wiegand_clear_data(void);

// 任务通知接口函数
void wiegand_register_task(TaskHandle_t task_handle);
void wiegand_unregister_task(void);
uint32_t wiegand_wait_card_notification(uint32_t timeout_ms);

// 高级接口函数(供任务调用)
bool wiegand_wait_for_card(uint32_t timeout_ms, uint32_t *card_id);
void wiegand_process_card_event(uint32_t card_id);
void wiegand_status_check(void);
void wiegand_test_interrupt(void);
void wiegand_monitor_pins(uint32_t duration_ms);

// 中断处理函数(由中断服务程序调用)
void wiegand_d0_interrupt(void);
void wiegand_d1_interrupt(void);
void wiegand_timeout_check(void);

#endif /* __WIEGAND_H__ */

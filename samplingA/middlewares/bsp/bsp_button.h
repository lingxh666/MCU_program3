#ifndef __BSP_BUTTON_H__
#define __BSP_BUTTON_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "multi_button.h"
#include <stdint.h>
#include <stdbool.h>

/* 门锁状态统计结构（对外接口） */
typedef struct {
    bool is_locked;              // 当前是否锁上
    uint32_t lock_time;          // 上锁时间戳（毫秒）
    uint32_t unlock_time;        // 开锁时间戳（毫秒）
    uint32_t locked_duration;    // 上锁持续时间（毫秒）
    uint32_t unlocked_duration;  // 开锁持续时间（毫秒）
    uint32_t lock_count;         // 上锁次数
    uint32_t unlock_count;       // 开锁次数
} DoorLockStats_t;

/* 最近一次门禁操作时间（RTC时间，供协议读取） */
typedef struct {
    uint8_t year;    // 年（2000年起）
    uint8_t month;   // 月
    uint8_t day;     // 日
    uint8_t hour;    // 时
    uint8_t minute;  // 分
    uint8_t second;  // 秒
    uint8_t event_type;  // 事件类型: 1=开门, 2=关门
} DoorLastEventTime_t;

extern DoorLastEventTime_t g_door_last_event_time;  // 最近门禁操作时间

/* 按钮初始化（仅初始化BTN01-BTN03：原点、位置、门锁） */
void BTNinit(void);

/* 门锁状态查询接口 */
void door_get_stats(DoorLockStats_t *stats);
void door_reset_stats(void);
bool door_is_locked(void);

/* 门锁事件处理接口（在任务中调用） */
void door_event_process(void);  // 处理门锁事件并写入FlashDB

/* ========== 直接GPIO读取接口 ========== 
 * BTN04-BTN09已从按钮库移除，改为直接GPIO读取
 * 避免定时器中断频繁轮询导致系统卡死
 */

// 液位检测
uint8_t read_sampling_liquid_level(void);   // PD4 采样液位
uint8_t read_reflux_liquid_level(void);     // PB5 回流液位
uint8_t read_delivery_liquid_level(void);   // PB6 送留液位

// 触发信号
uint8_t read_trigger_delivery_signal(void);  // PB7 触发送样
uint8_t read_trigger_retention_signal(void); // PE2 触发留样
uint8_t read_trigger_sampling_signal(void);  // PE3 触发采样（开关量）

/* ========== 废水排放浮子开关接口 ========== 
 * PE4 浮子开关检测，PB1 放水阀控制
 * 当 g_RetainSampleConfig.EnableVacuum = 1 时启用
 */
uint8_t read_waste_water_float_switch(void);  // PE4 废水浮子开关
void waste_water_drain_init(void);            // 废水排放模块初始化
void waste_water_drain_process(void);         // 废水排放处理（周期性调用）

static void BTN01_PRESS_DOWN_Handler(void *btn);
static void BTN02_PRESS_DOWN_Handler(void *btn);
static void BTN03_PRESS_DOWN_Handler(void *btn);
static void BTN03_PRESS_UP_Handler(void *btn);

#ifdef __cplusplus
}
#endif

#endif

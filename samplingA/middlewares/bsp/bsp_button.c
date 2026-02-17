#include "bsp_button.h"
#include "freertos_app.h"
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "multi_button.h"
#include "work.h"
#include "sampling.h"
#include "rtc.h"
#include "at32f403a_407_rtc.h"
#include "app_flashdb.h"
#include "screen.h"
#include "record_cache.h"

/* 仅在本文件使用的ISR钩子原型（实现位于 work.c，故不在 work.h 对外暴露） */
void bottle_on_position_fall(void);
void bottle_on_origin_fall(void);

/* 门锁统计全局变量（结构体定义在头文件中） */
static DoorLockStats_t g_door_stats = {0};

/* 最近一次门禁操作时间（供四川协议等读取） */
DoorLastEventTime_t g_door_last_event_time = {0};

/* 门锁事件缓存（用于在中断中缓存，在任务中写入FlashDB） */
typedef struct {
    uint8_t event_type;       // 事件类型: 0=无事件, 1=开门, 2=关门
    uint32_t timestamp;       // 时间戳（毫秒）
    uint32_t duration;        // 持续时间（毫秒）
    uint8_t pending;          // 是否待处理
} DoorEventLocal_t;

/* 门锁事件缓存（环形缓冲区，8个槽位） */
#define DOOR_EVENT_CACHE_SIZE 8
static volatile DoorEventLocal_t g_door_event_cache[DOOR_EVENT_CACHE_SIZE] = {0};
static volatile uint8_t g_cache_write_index = 0;  // 写索引（中断使用）
static volatile uint8_t g_cache_read_index = 0;   // 读索引（任务使用）
static volatile uint8_t g_cache_count = 0;        // 缓存计数（防溢出）

// ★ 只保留关键按钮（原点、位置、门锁）使用按钮库轮询
// 液位和触发信号改为直接读取GPIO，避免定时器中断频繁轮询导致系统卡死
enum Button_IDs
{
	Origin_key,//原点  PD0  01
	location_key, //位置  PD1  02
	door_key //门禁  PE12  03
};

struct Button btn01;
struct Button btn02;
struct Button btn03;





// ★ 按钮库只处理关键按钮（原点、位置、门锁）
static uint8_t read_button_GPIO(uint8_t button_id)
{
	switch (button_id)
	{
	case Origin_key:
		return gpio_input_data_bit_read(GPIOD, GPIO_PINS_0);  // 原点 PD0
	case location_key: 
		return gpio_input_data_bit_read(GPIOD, GPIO_PINS_1);  // 位置 PD1
	case door_key: 
		return gpio_input_data_bit_read(GPIOE, GPIO_PINS_12); // 门禁 PE12
	default:
		return 0;
	}
}

/* ========== 直接GPIO读取接口（替代按钮库轮询）========== */

/**
 * @brief 读取液位和触发信号GPIO状态
 * @note 这些信号不再使用按钮库轮询，避免定时器中断频繁轮询导致卡死
 */

// 采样液位 PD4
uint8_t read_sampling_liquid_level(void) {
	return gpio_input_data_bit_read(GPIOD, GPIO_PINS_4);
}

// 回流液位 PB5
uint8_t read_reflux_liquid_level(void) {
	return gpio_input_data_bit_read(GPIOB, GPIO_PINS_5);
}

// 送留液位 PB6
uint8_t read_delivery_liquid_level(void) {
	return gpio_input_data_bit_read(GPIOB, GPIO_PINS_6);
}

// 触发送样 PB7
uint8_t read_trigger_delivery_signal(void) {
	return gpio_input_data_bit_read(GPIOB, GPIO_PINS_7);
}

// 触发留样 PE2
uint8_t read_trigger_retention_signal(void) {
	return gpio_input_data_bit_read(GPIOE, GPIO_PINS_2);
}

// 触发采样（开关量）PE3
uint8_t read_trigger_sampling_signal(void) {
	return gpio_input_data_bit_read(GPIOE, GPIO_PINS_3);
}

/* ========== 废水排放浮子开关模块 ========== 
 * PE4 浮子开关检测，PB1 放水阀控制
 * 当 g_RetainSampleConfig.EnableVacuum = 1 时启用
 * 下降沿（高→低）：立即开启放水
 * 上升沿（低→高）：延时6秒后关闭放水
 */

// 废水排放状态变量
static uint8_t g_pe4_last_level = 1;           // PE4上一次电平状态（默认高电平）
static uint32_t g_waste_drain_stop_time = 0;   // 计划停止放水的时间戳（秒）
static uint8_t g_waste_drain_pending_stop = 0; // 是否有延时停止待处理

// PE4 废水浮子开关读取
uint8_t read_waste_water_float_switch(void) {
    return gpio_input_data_bit_read(GPIOE, GPIO_PINS_4);
}

// 废水排放模块初始化
void waste_water_drain_init(void)
{
    // 读取PE4初始状态
    g_pe4_last_level = read_waste_water_float_switch();
    g_waste_drain_pending_stop = 0;
    g_waste_drain_stop_time = 0;
    
    // 确保阀门初始关闭
    WasteWaterDrainClose;
    printf("[废水排放] 模块初始化完成，PE4初始电平=%d\r\n", g_pe4_last_level);
}

// 废水排放处理（周期性调用）
void waste_water_drain_process(void)
{
    // 检查是否启用自排空功能
    extern RetainSampleModeConfig g_RetainSampleConfig;
    if (g_RetainSampleConfig.EnableVacuum != 1) {
        return;
    }

    uint8_t current_level = read_waste_water_float_switch();
    uint32_t now_sec = g_tmr2_seconds;  // 使用秒级时间戳

    // 检测边沿变化
    if (current_level != g_pe4_last_level) {
        if (current_level == 0 && g_pe4_last_level == 1) {
            // 下降沿：高→低，水满，立即开启放水
            WasteWaterDrainOpen;  // gpio_bits_reset(GPIOB, GPIO_PINS_1)
            g_waste_drain_pending_stop = 0;  // 取消任何待处理的停止
            printf("[废水排放] PE4下降沿检测，开始放水\r\n");
        }
        else if (current_level == 1 && g_pe4_last_level == 0) {
            // 上升沿：低→高，水放完，延时6秒后关闭
            g_waste_drain_stop_time = now_sec + 6;  // 6秒后停止
            g_waste_drain_pending_stop = 1;
            printf("[废水排放] PE4上升沿检测，将在6秒后停止放水\r\n");
        }
        g_pe4_last_level = current_level;
    }

    // 检查延时停止
    if (g_waste_drain_pending_stop && now_sec >= g_waste_drain_stop_time) {
        WasteWaterDrainClose;  // gpio_bits_set(GPIOB, GPIO_PINS_1)
        g_waste_drain_pending_stop = 0;
        printf("[废水排放] 延时完成，停止放水\r\n");
    }
}


void BTNinit(void){
	// ★ 只初始化关键按钮（原点、位置、门禁）
	// 液位和触发信号不再使用按钮库，改为直接读取GPIO

	button_init(&btn01, read_button_GPIO, 0, Origin_key);
	button_init(&btn02, read_button_GPIO, 0, location_key);
	button_init(&btn03, read_button_GPIO, 0, door_key);

	button_attach(&btn01, PRESS_DOWN, BTN01_PRESS_DOWN_Handler);
	button_attach(&btn02, PRESS_DOWN, BTN02_PRESS_DOWN_Handler);
	button_attach(&btn03, PRESS_DOWN, BTN03_PRESS_DOWN_Handler);
	button_attach(&btn03, PRESS_UP, BTN03_PRESS_UP_Handler);  // 添加门锁开锁事件

	button_start(&btn01);
	button_start(&btn02);
	button_start(&btn03);

	// ★ 修复：MCU重启时根据当前门禁GPIO状态初始化，避免误触发门禁记录
	// PE12: HIGH=门开, LOW=门关; active_level=0 表示低电平为"按下"状态
	uint8_t current_door_level = gpio_input_data_bit_read(GPIOE, GPIO_PINS_12);
	if (current_door_level == 0) {
		// 门当前是关闭状态（PE12=LOW），设置状态机为state=1（已按下）
		// 这样不会在第一次button_ticks时误触发PRESS_DOWN事件
		btn03.state = 1;
		g_door_stats.is_locked = true;
		g_door_stats.lock_time = g_tmr3_milliseconds;
		printf("[Door] Init: door is CLOSED, state=1, skip initial event\r\n");
	} else {
		// 门当前是打开状态（PE12=HIGH），保持state=0
		g_door_stats.is_locked = false;
		g_door_stats.unlock_time = g_tmr3_milliseconds;
		printf("[Door] Init: door is OPEN, state=0\r\n");
	}
}

static void BTN01_PRESS_DOWN_Handler(void *btn)
{
/* PD0原点传感器：1号瓶专用磁铁触发 */
bottle_on_origin_fall();
    // ★ 中断中禁止打印
    // printf("原点瓶位 1号瓶\r\n");
}

static void BTN02_PRESS_DOWN_Handler(void *btn)
{
/* PD1位置传感器：所有瓶子的通用磁铁触发 */
bottle_on_position_fall();
    // ★ 中断中禁止打印
    // printf("瓶位触发\r\n");
}

static void BTN03_PRESS_DOWN_Handler(void *btn)
{
    /* 门锁关闭（锁上）事件 - PE12检测到低电平 */
    uint32_t current_time = g_tmr3_milliseconds;
    uint32_t open_duration = 0;  /* ★ 修复#60: 计算开门持续时间 */

    // ★ 调试日志：检测到PRESS_DOWN信号（低电平）
    printf("[Door-Signal] PRESS_DOWN detected (PE12=LOW), time=%u\r\n", current_time);

    // 如果之前是开锁状态，计算开锁持续时间
    if (!g_door_stats.is_locked && g_door_stats.unlock_time > 0) {
        g_door_stats.unlocked_duration = current_time - g_door_stats.unlock_time;
        open_duration = g_door_stats.unlocked_duration;  /* ★ 保存用于事件记录 */
    }

    // 更新状态
    g_door_stats.is_locked = true;
    g_door_stats.lock_time = current_time;
    g_door_stats.lock_count++;

    printf("[Door-Signal] State: LOCKED, caching event_type=2 (LOCK), duration=%u ms\r\n", open_duration);

    // 缓存关门事件（用于任务中写入TSDB）
    if (g_cache_count < DOOR_EVENT_CACHE_SIZE) {
        uint8_t write_idx = g_cache_write_index;
        g_door_event_cache[write_idx].event_type = 2;  // 2=关门事件
        g_door_event_cache[write_idx].timestamp = current_time;
        g_door_event_cache[write_idx].duration = open_duration;  /* ★ 修复#60: 填充实际开门时长 */
        g_door_event_cache[write_idx].pending = 1;

        // 更新写索引（环形缓冲）
        g_cache_write_index = (write_idx + 1) % DOOR_EVENT_CACHE_SIZE;
        g_cache_count++;
    }
    // 缓存满时丢弃事件（避免覆盖未处理事件）
}

static void BTN03_PRESS_UP_Handler(void *btn)
{
    /* 门锁打开（开锁）事件 - PE12检测到高电平 */
    uint32_t current_time = g_tmr3_milliseconds;
    
    // ★ 调试日志：检测到PRESS_UP信号（高电平）
    printf("[Door-Signal] PRESS_UP detected (PE12=HIGH), time=%u\r\n", current_time);

    // 如果之前是锁上状态，计算锁上持续时间
    if (g_door_stats.is_locked && g_door_stats.lock_time > 0) {
        g_door_stats.locked_duration = current_time - g_door_stats.lock_time;
    }

    // 更新状态
    g_door_stats.is_locked = false;
    g_door_stats.unlock_time = current_time;
    g_door_stats.unlock_count++;
    
    printf("[Door-Signal] State: UNLOCKED, caching event_type=1 (UNLOCK)\r\n");

    // ★ 立即缓存开门事件（用于任务中写入TSDB）
    if (g_cache_count < DOOR_EVENT_CACHE_SIZE) {
        uint8_t write_idx = g_cache_write_index;
        g_door_event_cache[write_idx].event_type = 1;  // 1=开门事件
        g_door_event_cache[write_idx].timestamp = current_time;
        g_door_event_cache[write_idx].duration = 0;  // 开门时持续时间未知，关门时计算
        g_door_event_cache[write_idx].pending = 1;

        // 更新写索引（环形缓冲）
        g_cache_write_index = (write_idx + 1) % DOOR_EVENT_CACHE_SIZE;
        g_cache_count++;
    }
    // 缓存满时丢弃事件（避免覆盖未处理事件）
}

/* ========== BTN04-BTN09已移除 ========== 
 * 原因：定时器中断频繁轮询导致系统卡死（尤其是持续触发时）
 * 解决方案：改为直接GPIO读取接口，需要时才读取
 * 
 * 如果需要液位和触发信号的事件通知，请在相应任务中调用：
 * - read_sampling_liquid_level()  // PD4 采样液位
 * - read_reflux_liquid_level()    // PB5 回流液位
 * - read_delivery_liquid_level()  // PB6 送留液位
 * - read_trigger_delivery_signal() // PB7 触发送样
 * - read_trigger_retention_signal() // PE2 触发留样
 * - read_trigger_sampling_signal()  // PE3 触发采样（开关量）
 */


/* ========== 门锁状态查询接口实现 ========== */

/**
 * @brief 获取门锁统计数据
 * @param stats 输出参数，存储统计数据
 */
void door_get_stats(DoorLockStats_t *stats)
{
    if (stats != NULL) {
        taskENTER_CRITICAL();
        *stats = g_door_stats;
        taskEXIT_CRITICAL();
    }
}

/**
 * @brief 重置门锁统计数据
 */
void door_reset_stats(void)
{
    taskENTER_CRITICAL();
    g_door_stats.lock_count = 0;
    g_door_stats.unlock_count = 0;
    g_door_stats.locked_duration = 0;
    g_door_stats.unlocked_duration = 0;
    // 保留当前状态和时间戳
    taskEXIT_CRITICAL();
    printf("[门禁] 统计已重置\r\n");
}

/**
 * @brief 查询门锁当前状态
 * @return true=锁上, false=开锁
 */
bool door_is_locked(void)
{
    bool locked;
    taskENTER_CRITICAL();
    locked = g_door_stats.is_locked;
    taskEXIT_CRITICAL();
    return locked;
}

/**
 * @brief 处理门锁事件并写入FlashDB（在任务中调用）
 * @note 此函数用于处理中断中缓存的门锁事件，避免在中断中进行FlashDB操作
 *       使用TSDB锁保护，防止与其他任务的TSDB操作冲突
 */
void door_event_process(void)
{
    // 检查是否有待处理的事件
    if (g_cache_count == 0) {
        return;
    }

    // 处理所有待处理的事件
    while (g_cache_count > 0) {
        // 读取事件（原子操作保护）
        taskENTER_CRITICAL();
        DoorEventLocal_t event = g_door_event_cache[g_cache_read_index];
        g_door_event_cache[g_cache_read_index].pending = 0;
        g_cache_read_index = (g_cache_read_index + 1) % DOOR_EVENT_CACHE_SIZE;
        g_cache_count--;
        taskEXIT_CRITICAL();

        if (event.pending && event.event_type != 0) {
            // 准备门禁事件数据
            DoorEvent_t door_event;
            /* TSDB事件体时间戳统一为Unix秒（1970基准） */
            {
                uint32_t now_ms = g_tmr3_milliseconds;
                uint32_t now_ts = rtc_counter_get();
                uint32_t delta_ms = now_ms - event.timestamp; // unsigned自动处理回绕
                uint32_t delta_s = delta_ms / 1000u;
                door_event.timestamp = (delta_s > 0u && now_ts >= delta_s) ? (now_ts - delta_s) : now_ts;
            }
            door_event.duration = event.duration;

            // ★ 更新最近门禁操作时间（供四川协议40015-40020读取）
            rtc_time_get();  // 获取当前RTC时间
            g_door_last_event_time.year = calendar.year - 2000;
            g_door_last_event_time.month = calendar.month;
            g_door_last_event_time.day = calendar.date;
            g_door_last_event_time.hour = calendar.hour;
            g_door_last_event_time.minute = calendar.min;
            g_door_last_event_time.second = calendar.sec;
            g_door_last_event_time.event_type = event.event_type;

            // 使用TSDB锁保护写入操作
            if (tsdb_is_ready()) {
                if (event.event_type == 1) {
                    // 开门事件
                    tsdb_event_append(DOOR_EVT_UNLOCKED, &door_event, sizeof(door_event));
                    cache_add_door(DOOR_EVT_UNLOCKED, door_event.timestamp);  // 更新缓存
                    printf("[Door] Unlock event logged, ts=%u\r\n", (unsigned)door_event.timestamp);

                    // ★ 发送开门信号到串口屏 (5A A5 05 82 52 0C 00 01)
                    {
                        uint8_t door_open_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x0C, 0x00, 0x01};
                        screen_send_notify(USART_SCREEN, door_open_buf, 8, 3);
                        printf("[Door] 已发送开门信号到串口屏\r\n");
                    }
                } else if (event.event_type == 2) {
                    // 关门事件
                    tsdb_event_append(DOOR_EVT_LOCKED, &door_event, sizeof(door_event));
                    cache_add_door(DOOR_EVT_LOCKED, door_event.timestamp);  // 更新缓存
                    printf("[Door] Lock event logged, ts=%u\r\n", (unsigned)door_event.timestamp);

                    // ★ 发送关门信号到串口屏 (5A A5 05 82 52 0C 00 00)
                    {
                        uint8_t door_close_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x0C, 0x00, 0x00};
                        screen_send_notify(USART_SCREEN, door_close_buf, 8, 3);
                        printf("[Door] 已发送关门信号到串口屏\r\n");
                    }
                }
            }
        }
    }
}

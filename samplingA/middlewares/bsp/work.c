#include "work.h"
#include <stdlib.h>
#include <stdint.h>
#include "math.h"
#include "at32f403a_407_wk_config.h"
#include "wk_system.h"
#include "FreeRTOS.h"
#include "app_flashdb.h"
#include "sampling_time.h"
#include "retain_judge.h"
#include "rtc.h"

// 声明外部函数
extern uint32_t rtc_counter_get(void);
#include "Commtrigger.h"

// 前向声明
static uint16_t calc_time_proportional(uint16_t target_ml,
                                       uint16_t times[],
                                       uint16_t volumes[],
                                       int count);

uint8_t cmd_lock_all[] = {0x0A, 0x44, 0x30, 0x30, 0x4B, 0x31, 0x0D};                    // 设置广播方式让所有泵面板锁定
uint8_t cmd_stop_pump[] = {0x0A, 0x44, 0x30, 0x32, 0x43, 0x30, 0x0D};                   // 停止地址为02的泵运行
uint8_t cmd_set_direction[] = {0x0A, 0x44, 0x32, 0x32, 0x42, 0x31, 0x0D};               // 设置地址为02的泵运行方向为正向
uint8_t cmd_set_speed[] = {0x0A, 0x44, 0x30, 0x32, 0x41, 0x31, 0x33, 0x30, 0x30, 0x0D}; // 设置地址为02的泵转速为130.0rpm
uint8_t cmd_start_pump[] = {0x0A, 0x44, 0x30, 0x32, 0x43, 0x31, 0x0D};                  // 启动地址为02的泵运行


static __IO uint32_t dwt_fac_us;
static __IO uint32_t dwt_fac_ms;

/* ========== 瓶位系统实现 ==========
// 系统启动时初始化
bottle_auto_find_origin(100, 30000);  // 100rpm，30秒超时

// 查询当前瓶号
uint8_t current = bottle_query_current_at_origin();

// 移动到5号瓶
bottle_move_to(5, 150, 20000);  // 150rpm，20秒超时

// 回零到1号瓶
bottle_home_to_1(100, 15000);

*/

// 全局瓶号管理：表示“当前工作中的留样瓶编号”（1..BOTTLE_COUNT）??
// 该变量用于界面与流程间的信息同步（例如移动到目标瓶后更新）??
uint8_t g_current_bottle_number = 1; // 当前使用的瓶号，??号开??

// 留样瓶位状态（KVDB持久化）：在 settings_init_load 中会加载/回写
// - usedMask: 1=该瓶已使用（??对应????
// - currentBottle: 建议下次优先选择的瓶位（滚动更新??
RetainBottleState g_RetainBottleState = {1u, 0u}; // currentBottle=1, usedMask=0

// 大岳协议命令状态反馈全局变量
DayueCommandStatus_t g_dayue_cmd_status = {0};

// 留样瓶系统故障标志
volatile uint8_t g_bottle_system_fault = BOTTLE_FAULT_NONE;
volatile uint8_t g_bottle_fault_retry_count = 0;

// ★ 留样瓶首次初始化标志（开机后第一次留样时初始化）
volatile uint8_t g_bottle_first_init_done = 0;

/* ===== 工具：位图判定与瓶位遍历（公共函数） ===== */

/**
 * @brief 检查指定瓶位是否已使用
 * @param usedMask 瓶位占用位图（bit0=1号瓶, bit23=24号瓶）
 * @param bottle 瓶号（1-24）
 * @return 1=已使用；0=未使用
 */
uint8_t is_bottle_used(uint32_t usedMask, uint8_t bottle)
{
    // 非法瓶号一律视为"已用"（保护机制）
    if (bottle < 1 || bottle > BOTTLE_COUNT)
        return 1u;
    uint32_t bit = 1u << (uint32_t)(bottle - 1u);
    return (usedMask & bit) ? 1u : 0u;
}

/**
 * @brief 标记指定瓶位为已使用
 * @param usedMask 瓶位占用位图指针
 * @param bottle 瓶号（1-24）
 */
void mark_bottle_used(uint32_t *usedMask, uint8_t bottle)
{
    // 将指定瓶位标记为已使用
    if (!usedMask)
        return;
    if (bottle < 1 || bottle > BOTTLE_COUNT)
        return;
    uint32_t bit = 1u << (uint32_t)(bottle - 1u);
    *usedMask |= bit;
}

/**
 * @brief 清除指定瓶位的已使用标记（用于瓶排空后重新使用）
 * @param usedMask 瓶位占用位图指针
 * @param bottle 瓶号（1-24）
 */
void clear_bottle_used(uint32_t *usedMask, uint8_t bottle)
{
    // 清除指定瓶位的已使用标记
    if (!usedMask)
        return;
    if (bottle < 1 || bottle > BOTTLE_COUNT)
        return;
    uint32_t bit = 1u << (uint32_t)(bottle - 1u);
    *usedMask &= ~bit;
}

/**
 * @brief 从指定位置开始环形查找下一个空瓶
 * @param start_from 起始瓶号（1-24）
 * @param usedMask 瓶位占用位图
 * @param out 输出参数：找到的空瓶号
 * @return 1=找到空瓶；0=全满
 */
uint8_t find_next_empty_bottle(uint8_t start_from, uint32_t usedMask, uint8_t *out)
{
    // 从start_from开始"环形扫描"寻找第一个空瓶；找到则*out=瓶号并返回1，否则返回0
    if (!out)
        return 0u;
    uint8_t pos = (start_from >= 1 && start_from <= BOTTLE_COUNT) ? start_from : 1;
    for (uint8_t i = 0; i < BOTTLE_COUNT; ++i)
    {
        uint8_t b = (uint8_t)(((pos - 1u + i) % BOTTLE_COUNT) + 1u);
        if (!is_bottle_used(usedMask, b))
        {
            *out = b;
            return 1u;
        }
    }
    return 0u; // 全满
}

/* ===== 留样瓶系统故障管理函数 ===== */

/**
 * @brief 设置留样瓶系统故障标志
 * @param fault_code 故障码
 * @note 非阻塞、容错设计，不影响程序运行
 */
void bottle_set_fault(uint8_t fault_code)
{
    if (g_bottle_system_fault != fault_code)
    {
        g_bottle_system_fault = fault_code;
        printf("[留样瓶] 故障标志已设置: 0x%02X\r\n", fault_code);
        // 故障信息通过TSDB事件记录，由MQTT定期发送
        // 即使MQTT未连接，也只记录本地日志，不影响程序运行
    }
}

/**
 * @brief 清除留样瓶系统故障标志
 */
void bottle_clear_fault(void)
{
    if (g_bottle_system_fault != BOTTLE_FAULT_NONE)
    {
        printf("[留样瓶] 故障标志已清除 (原故障: 0x%02X)\r\n", g_bottle_system_fault);
        g_bottle_system_fault = BOTTLE_FAULT_NONE;
        g_bottle_fault_retry_count = 0;  // 重置恢复尝试次数
    }
}

/**
 * @brief 获取当前故障码
 * @return 故障码
 */
uint8_t bottle_get_fault(void)
{
    return g_bottle_system_fault;
}

/**
 * @brief 检查是否有故障
 * @return 1=有故障，0=无故障
 */
uint8_t bottle_is_fault_active(void)
{
    return (g_bottle_system_fault != BOTTLE_FAULT_NONE) ? 1 : 0;
}

/**
 * @brief 确保留样瓶系统已初始化（首次留样时调用）
 * @return 1=成功, 0=失败
 * @note 如果是首次调用，会执行阻塞式归零操作
 */
uint8_t bottle_ensure_initialized(void)
{
    // 已经初始化过，直接返回成功
    if (g_bottle_first_init_done)
    {
        return 1;
    }

    printf("[留样瓶] 首次留样，开始初始化...\r\n");

    // 读取KVDB中保存的瓶号
    uint8_t target_bottle = g_RetainSampleConfig.bottleNumber;
    if (target_bottle < 1 || target_bottle > 24)
    {
        target_bottle = 1;
    }

    // 执行阻塞式归零（超时120秒）
    printf("[留样瓶] 启动瓶盘归零...\r\n");
    if (!bottle_home_to_1(100, 120000))
    {
        printf("[留样瓶] 瓶盘归零失败\r\n");
        bottle_set_fault(BOTTLE_FAULT_INIT_TIMEOUT);
        return 0;
    }
    printf("[留样瓶] 瓶盘归零成功\r\n");
    bottle_clear_fault();

    // 如果目标瓶号不是1号，移动到目标位置
    if (target_bottle != 1)
    {
        printf("[留样瓶] 移动到目标瓶号%d...\r\n", target_bottle);
        if (!bottle_move_to(target_bottle, 100, 60000))
        {
            printf("[留样瓶] 移动到目标瓶号失败，使用1号瓶\r\n");
            bottle_set_fault(BOTTLE_FAULT_MOVE_TIMEOUT);
            g_current_bottle_number = 1;
            g_RetainBottleState.currentBottle = 1;
        }
        else
        {
            printf("[留样瓶] 移动到目标瓶号%d成功\r\n", target_bottle);
            g_current_bottle_number = target_bottle;
            g_RetainBottleState.currentBottle = target_bottle;
        }
    }
    else
    {
        g_current_bottle_number = 1;
        g_RetainBottleState.currentBottle = 1;
    }

    // 标记初始化完成
    g_bottle_first_init_done = 1;
    printf("[留样瓶] 初始化完成，当前瓶号=%d\r\n", g_current_bottle_number);

    return 1;
}

static volatile uint8_t s_current_bottle_at_origin = 1; // 当前零点位置的瓶号 (1-24)
static volatile uint8_t s_is_initialized = 0;           // 是否已通过PD0确认1号瓶位置
static volatile uint8_t s_is_finding_origin = 0;        // 是否正在寻找原点

/* ISR钩子函数：由 bsp_button.c 的回调调用 */
void bottle_on_origin_fall(void)
{
    // PD0触发 = 1号瓶经过零点
    s_current_bottle_at_origin = 1;
    s_is_initialized = 1;
    // ★ 中断中禁止打印，否则会死锁
    // printf("1号瓶到达零点\r\n");
}

void bottle_on_position_fall(void)
{
    // PD1触发 = 任意瓶子经过位置传感器
    if (!s_is_initialized && !s_is_finding_origin)
        return; // 未初始化时忽略位置变化

    s_current_bottle_at_origin++;
    if (s_current_bottle_at_origin > BOTTLE_COUNT)
        s_current_bottle_at_origin = 1;
    // ★ 中断中禁止打印，否则会死锁
    // printf("瓶位位置变化，当前零点瓶号: %d\r\n", s_current_bottle_at_origin);
}

/* 对外API函数 */
uint8_t bottle_query_current_at_origin(void)
{
    return s_current_bottle_at_origin;
}

uint8_t bottle_auto_find_origin(uint16_t rpm, uint32_t timeout_ms)
{
    extern EventGroupHandle_t event_handle;

    // ★ 检查是否已在原点，需要先离开再回来
    uint8_t need_leave = 0;
    if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_0) == RESET)
    {
        need_leave = 1;
        printf("[留样瓶] 阻塞寻原点：检测到已在原点，需先离开\r\n");
    }

    s_is_initialized = 0;
    s_is_finding_origin = 1; // 标记正在寻找原点
    set_motor_rpm(1);        // 顺时针旋转寻找1号瓶

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        // 如果需要先离开原点，等待PD0变高
        if (need_leave)
        {
            if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_0) == SET)
            {
                need_leave = 0;
                printf("[留样瓶] 阻塞寻原点：已离开原点\r\n");
            }
        }
        else if (s_is_initialized && s_current_bottle_at_origin == 1)
        {
            set_motor_rpm(0); // 停止
            s_is_finding_origin = 0;
            return 1; // 找到1号瓶
        }

        // 喂狗:防止WDT超时 (TASK3=bit1, TASK4=bit2, 同时喂两个任务)
        xEventGroupSetBits(event_handle, (1 << 1) | (1 << 2));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_motor_rpm(0);
    s_is_finding_origin = 0;
    return 0; // 超时
}

uint8_t bottle_home_to_1(uint16_t rpm, uint32_t timeout_ms)
{
    extern EventGroupHandle_t event_handle;

    if (!s_is_initialized)
    {
        return bottle_auto_find_origin(rpm, timeout_ms);
    }

    if (s_current_bottle_at_origin == 1)
        return 1; // 已经在1号位置

    set_motor_rpm(1); // 顺时针旋转到1号瓶
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        if (s_current_bottle_at_origin == 1)
        {
            set_motor_rpm(0);
            return 1;
        }

        // 喂狗:防止WDT超时 (TASK3=bit1, TASK4=bit2, 同时喂两个任务)
        xEventGroupSetBits(event_handle, (1 << 1) | (1 << 2));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_motor_rpm(0);
    return 0;
}

// 非阻塞瓶位复位状态机
static struct
{
    uint8_t is_active;        // 是否正在进行复位
    uint32_t start_time;      // 开始时间
    uint32_t timeout_ms;      // 超时时间
    uint16_t rpm;             // 转速
    uint8_t result;           // 结果 (0=进行中, 1=成功, 2=超时/失败)
    uint8_t need_leave_origin; // 需要先离开原点（开机时已在1号瓶位置）
} s_bottle_reset_fsm = {0, 0, 0, 0, 0, 0};

// 非阻塞瓶位移动状态机
static struct
{
    uint8_t is_active;      // 是否正在进行移动
    uint32_t start_time;    // 开始时间
    uint32_t timeout_ms;    // 超时时间
    uint16_t rpm;           // 转速
    uint8_t target_bottle;  // 目标瓶号
    uint8_t result;         // 结果 (0=进行中, 1=成功, 2=超时/失败)
} s_bottle_move_fsm = {0, 0, 0, 0, 0, 0};

/* 内部辅助函数声明 */
static uint16_t calc_time_with_selected_points(uint16_t target_ml,
                                               uint16_t times[],
                                               uint16_t volumes[],
                                               int count);

/**
 * @brief 启动非阻塞留样瓶复位到1号瓶
 * @param rpm 转速
 * @param timeout_ms 超时时间(毫秒)
 * @return 0=启动成功, 1=已在进行中
 */
uint8_t bottle_home_to_1_start(uint16_t rpm, uint32_t timeout_ms)
{
    if (s_bottle_reset_fsm.is_active)
    {
        return 1; // 已在进行中
    }

    // 启动复位
    s_bottle_reset_fsm.is_active = 1;
    s_bottle_reset_fsm.start_time = xTaskGetTickCount();
    s_bottle_reset_fsm.timeout_ms = timeout_ms;
    s_bottle_reset_fsm.rpm = rpm;
    s_bottle_reset_fsm.result = 0; // 进行中

    // ★ 检查PD0传感器当前状态，如果已经在1号瓶位置，需要先离开再回来
    // 解决开机时已在1号瓶导致按钮库无法检测到边沿触发的问题
    if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_0) == RESET)
    {
        // PD0为低电平，说明当前已在1号瓶位置，需要先离开
        s_bottle_reset_fsm.need_leave_origin = 1;
        printf("[留样瓶] 检测到已在原点，需先离开再重新定位\r\n");
    }
    else
    {
        s_bottle_reset_fsm.need_leave_origin = 0;
    }

    // 清除初始化标志，强制重新寻找原点
    s_is_initialized = 0;
    s_is_finding_origin = 1;

    set_motor_rpm(1); // 顺时针旋转
    printf("[留样瓶] 非阻塞复位已启动，rpm=%u, timeout=%lu ms\r\n", rpm, timeout_ms);

    return 0; // 启动成功
}

/**
 * @brief 检查非阻塞留样瓶复位状态
 * @return 0=进行中, 1=成功, 2=超时/失败
 */
uint8_t bottle_home_to_1_check(void)
{
    extern EventGroupHandle_t event_handle;

    if (!s_bottle_reset_fsm.is_active)
    {
        return s_bottle_reset_fsm.result; // 未启动，返回当前结果
    }

    // ★ 如果需要先离开原点，检查PD0是否已变为高电平
    if (s_bottle_reset_fsm.need_leave_origin)
    {
        if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_0) == SET)
        {
            // PD0变为高电平，已离开原点，清除标志
            s_bottle_reset_fsm.need_leave_origin = 0;
            printf("[留样瓶] 已离开原点，继续寻找1号瓶\r\n");
        }
        // 还在原点，继续等待离开
    }

    // 检查是否成功（只有离开原点后才检查）
    if (!s_bottle_reset_fsm.need_leave_origin &&
        s_is_initialized && s_current_bottle_at_origin == 1)
    {
        set_motor_rpm(0);
        s_bottle_reset_fsm.is_active = 0;
        s_bottle_reset_fsm.result = 1; // 成功
        s_is_finding_origin = 0;
        printf("[留样瓶] 复位成功\r\n");
        return 1;
    }

    // 检查是否超时
    if ((xTaskGetTickCount() - s_bottle_reset_fsm.start_time) >= pdMS_TO_TICKS(s_bottle_reset_fsm.timeout_ms))
    {
        set_motor_rpm(0);
        s_is_finding_origin = 0;
        s_bottle_reset_fsm.is_active = 0;
        s_bottle_reset_fsm.need_leave_origin = 0;
        s_bottle_reset_fsm.result = 2; // 超时/失败
        printf("[留样瓶] 复位超时失败\r\n");
        return 2;
    }

    // 喂狗:防止WDT超时
    xEventGroupSetBits(event_handle, (1 << 1) | (1 << 2));

    return 0; // 进行中
}

/**
 * @brief 停止非阻塞留样瓶复位
 */
void bottle_home_to_1_stop(void)
{
    if (s_bottle_reset_fsm.is_active)
    {
        set_motor_rpm(0);
        s_is_finding_origin = 0;
        s_bottle_reset_fsm.is_active = 0;
        s_bottle_reset_fsm.result = 2; // 标记为失败
        printf("[留样瓶] 复位被停止\r\n");
    }
}

/**
 * @brief 检查留样瓶传感器是否工作正常
 * @return 1=正常, 0=异常/缺失
 */
uint8_t bottle_sensor_is_working(void)
{
    // 检查初始化状态和最近是否有传感器信号
    static uint32_t last_sensor_time = 0;
    uint32_t current_time = xTaskGetTickCount();

    // 如果从未初始化，且超过30秒没有传感器信号，认为传感器异常
    if (!s_is_initialized && (current_time - last_sensor_time) > pdMS_TO_TICKS(30000))
    {
        return 0;
    }

    // 如果有传感器信号，更新时间戳
    if (s_is_initialized)
    {
        last_sensor_time = current_time;
    }

    return s_is_initialized;
}

/**
 * @brief 重置留样瓶传感器检测状态
 */
void bottle_sensor_reset_detection(void)
{
    s_is_initialized = 0;
    printf("[留样瓶] 传感器检测状态已重置\r\n");
}

uint8_t bottle_move_to(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms)
{
    extern EventGroupHandle_t event_handle;

    if (!s_is_initialized)
    {
        if (!bottle_auto_find_origin(rpm, timeout_ms))
        {
            return 0;
        }
    }

    if (target_bottle < 1 || target_bottle > BOTTLE_COUNT)
    {
        return 0;
    }

    if (s_current_bottle_at_origin == target_bottle)
    {
        return 1;
    }

    set_motor_rpm(1);

    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms))
    {
        if (s_current_bottle_at_origin == target_bottle)
        {
            set_motor_rpm(0);
            return 1;
        }

        // 喂狗:防止WDT超时 (TASK3=bit1, TASK4=bit2, 同时喂两个任务)
        xEventGroupSetBits(event_handle, (1 << 1) | (1 << 2));
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    set_motor_rpm(0);
    return 0;
}

/**
 * @brief 启动非阻塞瓶位移动
 * @param target_bottle 目标瓶号 (1-24)
 * @param rpm 转速
 * @param timeout_ms 超时时间(毫秒)
 * @return 0=启动成功, 1=已在进行中, 2=参数错误, 3=已在目标位置
 */
uint8_t bottle_move_to_start(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms)
{
    if (s_bottle_move_fsm.is_active)
    {
        return 1; // 已在进行中
    }

    if (target_bottle < 1 || target_bottle > BOTTLE_COUNT)
    {
        return 2; // 参数错误
    }

    // 检查是否已经在目标位置
    if (s_is_initialized && s_current_bottle_at_origin == target_bottle)
    {
        s_bottle_move_fsm.result = 1; // 成功
        s_bottle_move_fsm.target_bottle = target_bottle;
        printf("[留样瓶] 已在目标位置%d，无需移动\r\n", target_bottle);
        return 3; // 已在目标位置
    }

    // 如果未初始化，先尝试自动寻找原点
    if (!s_is_initialized)
    {
        if (!bottle_auto_find_origin(rpm, timeout_ms))
        {
            s_bottle_move_fsm.result = 2; // 失败
            printf("[留样瓶] 自动寻找原点失败\r\n");
            return 2;
        }
    }

    // 启动移动
    s_bottle_move_fsm.is_active = 1;
    s_bottle_move_fsm.start_time = xTaskGetTickCount();
    s_bottle_move_fsm.timeout_ms = timeout_ms;
    s_bottle_move_fsm.rpm = rpm;
    s_bottle_move_fsm.target_bottle = target_bottle;
    s_bottle_move_fsm.result = 0; // 进行中

    set_motor_rpm(1); // 顺时针旋转
    printf("[留样瓶] 非阻塞移动已启动，目标=%d，rpm=%u，timeout=%lu ms\r\n", 
           target_bottle, rpm, timeout_ms);

    return 0; // 启动成功
}

/**
 * @brief 检查非阻塞瓶位移动状态
 * @return 0=进行中, 1=成功, 2=超时/失败
 */
uint8_t bottle_move_to_check(void)
{
    extern EventGroupHandle_t event_handle;

    if (!s_bottle_move_fsm.is_active)
    {
        return s_bottle_move_fsm.result; // 未启动，返回当前结果
    }

    // 检查是否成功
    if (s_current_bottle_at_origin == s_bottle_move_fsm.target_bottle)
    {
        set_motor_rpm(0);
        s_bottle_move_fsm.is_active = 0;
        s_bottle_move_fsm.result = 1; // 成功
        printf("[留样瓶] 移动成功，已到达%d号瓶\r\n", s_bottle_move_fsm.target_bottle);
        return 1;
    }

    // 检查是否超时
    if ((xTaskGetTickCount() - s_bottle_move_fsm.start_time) >= pdMS_TO_TICKS(s_bottle_move_fsm.timeout_ms))
    {
        set_motor_rpm(0);
        s_bottle_move_fsm.is_active = 0;
        s_bottle_move_fsm.result = 2; // 超时/失败
        printf("[留样瓶] 移动超时失败，目标=%d\r\n", s_bottle_move_fsm.target_bottle);
        return 2;
    }

    // 喂狗:防止WDT超时
    xEventGroupSetBits(event_handle, (1 << 1) | (1 << 2));

    return 0; // 进行中
}

/**
 * @brief 停止非阻塞瓶位移动
 */
void bottle_move_to_stop(void)
{
    if (s_bottle_move_fsm.is_active)
    {
        set_motor_rpm(0);
        s_bottle_move_fsm.is_active = 0;
        s_bottle_move_fsm.result = 2; // 标记为失败
        printf("[留样瓶] 移动被停止\r\n");
    }
}

/**
 * @brief 获取非阻塞移动的目标瓶号
 * @return 目标瓶号
 */
uint8_t bottle_move_to_get_target(void)
{
    return s_bottle_move_fsm.target_bottle;
}

/**
 * @brief 检查非阻塞移动是否正在进行
 * @return 1=正在进行, 0=未进行
 */
uint8_t bottle_move_to_is_active(void)
{
    return s_bottle_move_fsm.is_active;
}

uint8_t emptybottle(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms)
{
    bottle_move_to(target_bottle, rpm, timeout_ms);
    gpio_bits_set(GPIOB, GPIO_PINS_5); // 控制继电器弹出顶杆 检测电流不对返回0 记录故障 PB0 PC5
    gpio_bits_reset(GPIOC, GPIO_PINS_0);
    vTaskDelay(10000);
    vTaskDelay(10000);

    // 延时到留样瓶水样排空
    gpio_bits_reset(GPIOB, GPIO_PINS_5);
    gpio_bits_set(GPIOC, GPIO_PINS_0); // 控制继电器收回顶杆 检测电流不对返回0 记录故障
    // 延时到收回到位
    vTaskDelay(10000);
    gpio_bits_set(GPIOB, GPIO_PINS_5);
    gpio_bits_set(GPIOC, GPIO_PINS_0); // 继电器断电
    return 1;
}

uint8_t emptybottleall(uint16_t rpm, uint32_t timeout_ms)
{
    uint8_t i;

    for (i = 1; i < 25; i++)
    {
        emptybottle(i, rpm, timeout_ms);
    }
    return 1;
}


uint8_t Modbus(usart_type *usart_x, QueueHandle_t queue, uint8_t *buf, uint8_t Sendlen, uint8_t retryNum)
{
    UartMessage Modbusdata;
    while (retryNum > 0)
    {
        memset(Modbusdata.data, 0, 100);
        vSendData(usart_x, buf, Sendlen);
        vTaskDelay(150);

        if (xQueueReceive(queue, &Modbusdata, pdMS_TO_TICKS(200)) == pdPASS)
        { // 消息队列modbus_handle
            if (vCrc16Check(Modbusdata.data, Modbusdata.len))
            {
                return 1; // 成功，直接返回
            }
            else
            {
                retryNum--;
                // 收到消息但CRC校验失败或长度不对，继续重试
            }
        }
        else
        {
            retryNum--;
            printf("重试次数=%d\r\n", retryNum);
        }
    }

    // 重试次数用完，返回失败
    printf("错误\r\n");
    return 0;
}

uint16_t CRC16_MODBUS(uint8_t *data, uint16_t length)
{
    uint32_t index = 0;
    crc_data_reset();
    for (index = 0; index < length; index++)
    {
        (*(uint8_t *)&CRC->dt) = data[index];
    }
    return (CRC->dt);
}

uint8_t vCrc16Check(uint8_t *data, uint16_t length)
{
    // 最小Modbus帧: 地址(1) + 功能码(1) + CRC(2) = 4字节
    if (length < 4) {
        return 0;
    }
    crc_data_reset();
    uint16_t calculatedCrc = CRC16_MODBUS(data, length - 2);
    //    printf("calculatedCrc=%d\r\n",calculatedCrc);
    uint16_t receivedCrc = (data[length - 1] << 8) | data[length - 2];

    //    printf("receivedCrc=%d\r\n",receivedCrc);
    if (calculatedCrc == receivedCrc)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void SendData(usart_type *usart_x, char *buf, uint8_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
    {
        while (usart_flag_get(usart_x, USART_TDBE_FLAG) == RESET)
        {
        }

        usart_data_transmit(usart_x, buf[i]);
    }
}

void vSendData(usart_type *usart_x, const uint8_t *buf, uint8_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++)
    {
        while (usart_flag_get(usart_x, USART_TDBE_FLAG) == RESET)
        {
        }

        usart_data_transmit(usart_x, buf[i]);
    }
}

void FindString(char *pcBuf, char *left, char *right, char *text)
{
    if (pcBuf == NULL || left == NULL || right == NULL || text == NULL)
    {
        printf("字符串名称未找到!\n");
        return;
    }

    char *search = pcBuf;
    char *best_left = NULL;
    char *best_right = NULL;

    /* 选择最后一个 left 且后面存在 right 的成对区间 */
    while (1)
    {
        char *p = strstr(search, left);
        if (p == NULL)
            break;
        char *q = strstr(p + strlen(left), right);
        if (q == NULL)
            break;
        best_left = p;
        best_right = q;
        search = p + 1;
    }

    if (best_left == NULL || best_right == NULL || best_left > best_right)
    {
        printf("字符串名称未找到!\n");
        return;
    }

    /* 取内容并做首尾空白裁剪 */
    char *start = best_left + strlen(left);
    char *end = best_right;

    /* 去掉前导空白/分隔符 */
    while (start < end && (*start == ' ' || *start == '\r' || *start == '\n' || *start == '\t' || *start == ':' || *start == ','))
        start++;
    /* 去掉尾部空白/分隔符 */
    while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\r' || *(end - 1) == '\n' || *(end - 1) == '\t' || *(end - 1) == ':' || *(end - 1) == ','))
        end--;

    size_t copy_len = (size_t)(end - start);
    if (copy_len > 0)
    {
        memcpy(text, start, copy_len);
        text[copy_len] = '\0';
    }
    else
    {
        /* 空内容也返回空串 */
        text[0] = '\0';
    }
}

uint32_t FindNum(char *pcBuf, char *left, char *right)
{
    if (pcBuf == NULL || left == NULL || right == NULL)
    {
        printf("字符串名称未找到!\n");
        return 0;
    }

    char *search = pcBuf;
    char *best_left = NULL;
    char *best_right = NULL;

    /* 寻找最后一个满足有 right 跟随的 left */
    while (1)
    {
        char *p = strstr(search, left);
        if (p == NULL)
            break;
        char *q = strstr(p + strlen(left), right);
        if (q == NULL)
            break; /* 没有 right 则停止在最后一个完整对之前 */
        best_left = p;
        best_right = q;
        search = p + 1; /* 继续向后搜索，取最后一对 */
    }

    if (best_left == NULL || best_right == NULL || best_left > best_right)
    {
        printf("字符串名称未找到!\n");
        return 0;
    }

    /* 在 (best_left .. best_right) 区间内解析十进制数字 */
    char *s = best_left + strlen(left);
    /* 跳过空白与分隔符 */
    while (s < best_right && (*s == ' ' || *s == '\r' || *s == '\n' || *s == '\t' || *s == ':' || *s == ','))
        s++;
    /* 跳过非数字直到第一个数字 */
    while (s < best_right && (*s < '0' || *s > '9'))
        s++;
    if (s >= best_right || *s < '0' || *s > '9')
    {
        printf("字符串名称未找到!\n");
        return 0;
    }

    uint32_t val = 0;
    while (s < best_right && (*s >= '0' && *s <= '9'))
    {
        uint32_t digit = (uint32_t)(*s - '0');
        /* 简单的溢出保护：若将要溢出则停止并返回当前累计值 */
        if (val > (UINT32_MAX - digit) / 10u)
            break;
        val = val * 10u + digit;
        s++;
    }
    return val;
}

void extract_numbers_irq(const char *str, uint32_t *crc1, uint32_t *crc2)
{
    *crc1 = 0;
    *crc2 = 0;

    // 查找"begin"位置
    while (*str && (*str != 'b' || str[1] != 'e' || str[2] != 'g' ||
                    str[3] != 'i' || str[4] != 'n'))
    {
        str++;
    }

    if (*str)
    {
        str += 5;  // 跳过"begin"

        // 查找"ZG"分隔符
        char *zg_pos = strstr(str, "ZG");
        if (zg_pos)
        {
            // 提取CRC1（begin到ZG之间）
            char crc_str[9] = {0};
            int len = zg_pos - str;
            if (len > 8) len = 8;
            if (len > 0)
            {
                strncpy(crc_str, str, len);
                *crc1 = strtoul(crc_str, NULL, 16);
            }

            // 提取CRC2（ZG后8个字符）
            str = zg_pos + 2;  // 跳过"ZG"
            if (strlen(str) >= 8)
            {
                strncpy(crc_str, str, 8);
                crc_str[8] = '\0';
                *crc2 = strtoul(crc_str, NULL, 16);
            }
        }
    }
}

void delay_init(void)
{
    crm_clocks_freq_type crm_clocks_freq_struct = {0};
    crm_clocks_freq_get(&crm_clocks_freq_struct);
    dwt_fac_us = crm_clocks_freq_struct.sclk_freq / (1000000U);
    dwt_fac_ms = dwt_fac_us * (1000U);
}

void delay_us(uint32_t nus)
{
    uint32_t temp;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0x00;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    temp = DWT->CYCCNT;

    while ((DWT->CYCCNT - temp) < nus * dwt_fac_us)
        ;

    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0x00;
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
}

void delay_ms(uint16_t nms)
{
    uint32_t temp;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0x00;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    temp = DWT->CYCCNT;

    while ((DWT->CYCCNT - temp) < nms * dwt_fac_ms)
        ;

    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0x00;
    CoreDebug->DEMCR &= ~CoreDebug_DEMCR_TRCENA_Msk;
}

void delay_sec(uint16_t sec)
{
    uint16_t index;

    for (index = 0; index < sec; index++)
    {
        delay_ms(1000);
    }
}

void set_motor_rpm(uint8_t dir)
{
    if (dir == 0)
    {
        tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, 0);
        tmr_counter_enable(TMR1, FALSE);
        return;
    }

    tmr_counter_enable(TMR1, TRUE);
    tmr_channel_value_set(TMR1, TMR_SELECT_CHANNEL_1, 10);
}

void MotorRun(uint8_t Snum, uint8_t Dir, uint16_t Speed)
{
    // 广播锁面板
    vSendData(USART_MOTOR, cmd_lock_all, 7);
    vTaskDelay(100);

    // 地址两位 ASCII
    uint8_t tens = (uint8_t)('0' + (Snum / 10) % 10);
    uint8_t ones = (uint8_t)('0' + (Snum % 10));

    // 设置方向命令：地址[2],[3]；方向在[5]
    cmd_set_direction[2] = tens;
    cmd_set_direction[3] = ones;
    cmd_set_direction[5] = (uint8_t)('0' + (Dir ? 1 : 0));
    vSendData(USART_MOTOR, cmd_set_direction, 7);
    vTaskDelay(100);

    // 设置速度命令：地址[2],[3]；速度四位ASCII在[5..8]
    int v = (int)Speed * 10; // 170 -> 1700 (170.0rpm)
    if (v < 0)
        v = 0;
    if (v > 9999)
        v = 9999;
    cmd_set_speed[2] = tens;
    cmd_set_speed[3] = ones;
    cmd_set_speed[5] = (uint8_t)('0' + ((v / 1000) % 10));
    cmd_set_speed[6] = (uint8_t)('0' + ((v / 100) % 10));
    cmd_set_speed[7] = (uint8_t)('0' + ((v / 10) % 10));
    cmd_set_speed[8] = (uint8_t)('0' + (v % 10));
    vSendData(USART_MOTOR, cmd_set_speed, 10);
    vTaskDelay(100);

    // 启动命令：地址[2],[3]
    cmd_start_pump[2] = tens;
    cmd_start_pump[3] = ones;
    vSendData(USART_MOTOR, cmd_start_pump, 7);
    vTaskDelay(100);
}

void MotorStop(uint8_t Snum)
{
    vSendData(USART_MOTOR, cmd_lock_all, 7);
    vTaskDelay(100);
    uint8_t tens = (uint8_t)('0' + (Snum / 10) % 10);
    uint8_t ones = (uint8_t)('0' + (Snum % 10));
    cmd_stop_pump[2] = tens;
    cmd_stop_pump[3] = ones;
    vSendData(USART_MOTOR, cmd_stop_pump, 7);
    vTaskDelay(100);
}

// 辅助函数：使用选定的校准点计算时间
static uint16_t calc_time_with_selected_points(uint16_t target_ml,
                                               uint16_t times[],
                                               uint16_t volumes[],
                                               int count)
{
    // printf("[时间计算] ========== 组合计算 ==========\r\n");

    if (count == 1)
    {
        // 只有一个校准点
        uint16_t repeat_count = (target_ml + volumes[0] - 1) / volumes[0];
        uint32_t total_time = (uint32_t)repeat_count * times[0];
        // printf("[时间计算] 单点重复: %u次 x %u秒 = %lu秒\r\n",
        //        repeat_count, times[0], total_time);
        return (total_time > 0xFFFFu) ? 0xFFFFu : (uint16_t)total_time;
    }

    // 两个校准点的组合优化
    // 按水量排序（大的在前）
    int idx_large = 0, idx_small = 1;
    if (volumes[0] < volumes[1])
    {
        idx_large = 1;
        idx_small = 0;
    }

    uint32_t best_time = 0xFFFFFFFFu;
    // uint32_t best_volume = 0;  // 注释：仅用于调试打印
    // uint16_t best_count_large = 0, best_count_small = 0;  // 注释：仅用于调试打印

    // 限制搜索范围
    uint16_t max_rep_large = (target_ml / volumes[idx_large]) + 2;
    if (max_rep_large > 20)
        max_rep_large = 20;

    // 搜索最优组合
    for (uint16_t c_large = 0; c_large <= max_rep_large; c_large++)
    {
        uint32_t vol_large = (uint32_t)c_large * volumes[idx_large];
        if (vol_large > target_ml * 1.2)
            break; // 超过20%就停止

        uint16_t remaining = (vol_large >= target_ml) ? 0 : (target_ml - vol_large);
        uint16_t c_small = (remaining > 0) ? ((remaining + volumes[idx_small] - 1) / volumes[idx_small]) : 0;

        uint32_t total_volume = vol_large + (uint32_t)c_small * volumes[idx_small];
        uint32_t total_time = (uint32_t)c_large * times[idx_large] +
                              (uint32_t)c_small * times[idx_small];

        // 计算误差
        uint32_t diff = (total_volume > target_ml) ? (total_volume - target_ml) : (target_ml - total_volume);

        // 选择最优解（优先误差小，其次时间短）
        if (diff < 10 || // 误差小于10ml就接受
            (total_time < best_time && diff < 50))
        { // 或者误差可接受但时间更短
            best_time = total_time;
            // best_volume = total_volume;  // 注释：仅用于调试打印
            // best_count_large = c_large;  // 注释：仅用于调试打印
            // best_count_small = c_small;  // 注释：仅用于调试打印

            if (diff == 0)
                break; // 完美匹配
        }
    }

    // printf("[时间计算] 最优组合:\r\n");
    // if (best_count_large > 0)
    // {
    //     printf("[时间计算]   校准点%d: %u次 x %u秒/%uml\r\n",
    //            idx_large + 2, best_count_large, times[idx_large], volumes[idx_large]);
    // }
    // if (best_count_small > 0)
    // {
    //     printf("[时间计算]   校准点%d: %u次 x %u秒/%uml\r\n",
    //            idx_small + 2, best_count_small, times[idx_small], volumes[idx_small]);
    // }
    // printf("[时间计算] 总水量: %lu ml (误差: %ld ml)\r\n",
    //        best_volume, (int32_t)best_volume - target_ml);
    // printf("[时间计算] 总时间: %lu 秒\r\n", best_time);

    // 如果没有找到合适的组合，使用最近校准点按比例计算
    if (best_time == 0xFFFFFFFFu)
    {
        // printf("[时间计算] ========== 按比例计算 ==========\r\n");

        // 找到最接近的校准点
        uint16_t nearest_time = 0;
        uint16_t nearest_volume = 0;
        uint32_t min_diff = 0xFFFFFFFFu;

        for (int i = 0; i < count; i++)
        {
            uint32_t diff = (volumes[i] > target_ml) ? (volumes[i] - target_ml) : (target_ml - volumes[i]);
            if (diff < min_diff)
            {
                min_diff = diff;
                nearest_time = times[i];
                nearest_volume = volumes[i];
            }
            else if (diff == min_diff)
            {
                // 如果距离相同，选择体积更大的
                if (volumes[i] > nearest_volume)
                {
                    nearest_time = times[i];
                    nearest_volume = volumes[i];
                }
            }
        }

        if (nearest_volume > 0)
        {
            // 按比例计算时间
            uint32_t proportional_time = ((uint32_t)target_ml * nearest_time) / nearest_volume;

            // printf("[时间计算] 选择最接近校准点: %u 秒 = %u ml\r\n", nearest_time, nearest_volume);
            // printf("[时间计算] 按比例计算: %u ml × %u秒 / %u ml = %lu 秒\r\n",
            //        target_ml, nearest_time, nearest_volume, proportional_time);

            best_time = proportional_time;
        }
    }

    return (best_time > 0xFFFFu) ? 0xFFFFu : (uint16_t)best_time;
}

// 根据送样量校准点计算送样时间（单位：秒）
// 使用 retainSampleCalib 作为送样泵的时间-体积校准源（送样和留样使用同一个蠕动泵）
uint16_t calc_delivery_time_by_volume(uint16_t target_ml)
{
    printf("[送样计算] ========== 计算送样时间 ==========\r\n");
    printf("[送样计算] 目标水量: %u ml\r\n", target_ml);

    // 获取送样校准点（使用retainSampleCalib）
    uint16_t t[3] = {
        g_CalibrationParams.retainSampleCalib.time1,
        g_CalibrationParams.retainSampleCalib.time2,
        g_CalibrationParams.retainSampleCalib.time3};
    uint16_t v[3] = {
        g_CalibrationParams.retainSampleCalib.realValue1,
        g_CalibrationParams.retainSampleCalib.realValue2,
        g_CalibrationParams.retainSampleCalib.realValue3};

    printf("[送样计算] 校准点:\r\n");
    printf("[送样计算]   点1: %u 秒 = %u ml\r\n", t[0], v[0]);
    printf("[送样计算]   点2: %u 秒 = %u ml\r\n", t[1], v[1]);
    printf("[送样计算]   点3: %u 秒 = %u ml\r\n", t[2], v[2]);

    if (target_ml == 0)
        return 0;

    // 送样通常使用大水量校准点，优先使用校准点2和3
    uint16_t usable_t[2], usable_v[2];
    int usable_count = 0;

    // 收集有效校准点（跳过小水量的测试点）
    for (int i = 0; i < 3; i++)
    {
        if (t[i] > 0 && v[i] > 0 && v[i] >= 100)
        { // 跳过小于100ml的校准点
            usable_t[usable_count] = t[i];
            usable_v[usable_count] = v[i];
            usable_count++;
            printf("[送样计算] 使用校准点%d: %u 秒 = %u ml\r\n", i + 1, t[i], v[i]);
            // 防止数组越界：usable_t/usable_v只有2个元素
            if (usable_count >= 2) break;
        }
    }

    if (usable_count > 0)
    {
        return calc_time_with_selected_points(target_ml, usable_t, usable_v, usable_count);
    }
    else
    {
        printf("[送样计算] 没有有效的校准点\r\n");
        return 0;
    }
}
// 根据留样量校准点计算留样时间（单位：秒）
uint16_t calc_retain_time_by_volume(uint16_t target_ml)
{
    printf("[留样计算] ========== 计算留样时间 ==========\r\n");
    printf("[留样计算] 目标水量: %u ml\r\n", target_ml);

    // 获取留样校准点（使用retainSampleCalib）
    uint16_t t[3] = {
        g_CalibrationParams.retainSampleCalib.time1,
        g_CalibrationParams.retainSampleCalib.time2,
        g_CalibrationParams.retainSampleCalib.time3};
    uint16_t v[3] = {
        g_CalibrationParams.retainSampleCalib.realValue1,
        g_CalibrationParams.retainSampleCalib.realValue2,
        g_CalibrationParams.retainSampleCalib.realValue3};

    printf("[留样计算] 校准点:\r\n");
    printf("[留样计算]   点1: %u 秒 = %u ml (测试点)\r\n", t[0], v[0]);
    printf("[留样计算]   点2: %u 秒 = %u ml (小水量)\r\n", t[1], v[1]);
    printf("[留样计算]   点3: %u 秒 = %u ml (大水量)\r\n", t[2], v[2]);

    if (target_ml == 0)
        return 0;

    // 策略1: 测试场景 (<= 20ml) - 只使用测试校准点1
    if (target_ml <= 20)
    {
        if (t[0] > 0 && v[0] > 0)
        {
            // 使用校准点1按比例计算
            uint32_t proportional_time = ((uint32_t)target_ml * t[0]) / v[0];
            printf("[留样计算] 测试模式：使用校准点1按比例计算\r\n");
            printf("[留样计算] 按比例计算: %u ml × %u秒 / %u ml = %lu 秒\r\n",
                   target_ml, t[0], v[0], proportional_time);
            printf("[留样计算] 结果: %u 秒\r\n", (uint16_t)proportional_time);
            return (proportional_time > 0xFFFFu) ? 0xFFFFu : (uint16_t)proportional_time;
        }
        else
        {
            printf("[留样计算] 测试校准点1无效，使用其他校准点\r\n");
        }
    }

    // 策略2: 正常留样 - 使用校准点2和3
    uint16_t usable_t[2], usable_v[2];
    int usable_count = 0;

    // 检查校准点2 (100-200ml范围)
    if (t[1] > 0 && v[1] > 0 && v[1] >= 100 && v[1] <= 200)
    {
        usable_t[usable_count] = t[1];
        usable_v[usable_count] = v[1];
        usable_count++;
        printf("[留样计算] 使用校准点2: %u 秒 = %u ml\r\n", t[1], v[1]);
    }

    // 检查校准点3 (500-1000ml范围)
    if (t[2] > 0 && v[2] > 0 && v[2] >= 500 && v[2] <= 1000)
    {
        usable_t[usable_count] = t[2];
        usable_v[usable_count] = v[2];
        usable_count++;
        printf("[留样计算] 使用校准点3: %u 秒 = %u ml\r\n", t[2], v[2]);
    }

    // 如果没有找到合适的校准点，使用所有有效的校准点
    if (usable_count == 0)
    {
        for (int i = 0; i < 3; i++)
        {
            if (t[i] > 0 && v[i] > 0)
            {
                usable_t[usable_count] = t[i];
                usable_v[usable_count] = v[i];
                usable_count++;
                if (usable_count >= 2)
                    break;
            }
        }
    }

    if (usable_count > 0)
    {
        return calc_time_proportional(target_ml, usable_t, usable_v, usable_count);
    }
    else
    {
        printf("[留样计算] 没有有效的校准点\r\n");
        return 0;
    }
}
// 根据留样时间反向计算体积（用于送样流程中的体积估算）
// 使用 retainSampleCalib 校准参数
uint16_t calc_volume_by_retain_time(uint16_t time_seconds)
{
    if (time_seconds == 0)
        return 0;

    // 使用校准参数：36秒 = 100ml
    uint16_t calib_time = g_CalibrationParams.retainSampleCalib.time3;
    uint16_t calib_volume = g_CalibrationParams.retainSampleCalib.realValue3;

    if (calib_time == 0 || calib_volume == 0)
        return 0;

    // 按比例计算：volume = time * calib_volume / calib_time
    uint32_t result = ((uint32_t)time_seconds * calib_volume) / calib_time;
    return (result > 0xFFFFu) ? 0xFFFFu : (uint16_t)result;
}

// 根据校准点按比例计算时间（单位：秒）
// 逻辑：找到最接近的校准点，按比例计算时间
// 如果与两个校准点距离相同，选择更大的那个
static uint16_t calc_time_proportional(uint16_t target_ml,
                                       uint16_t times[],
                                       uint16_t volumes[],
                                       int count)
{
    // printf("[时间计算] ========== 按比例计算 ==========\r\n");
    // printf("[时间计算] 目标水量: %u ml\r\n", target_ml);

    if (target_ml == 0 || count == 0)
        return 0;

    // 找到最接近的校准点
    uint16_t nearest_time = 0;
    uint16_t nearest_volume = 0;
    uint32_t min_diff = 0xFFFFFFFFu;

    for (int i = 0; i < count; i++)
    {
        uint32_t diff = (volumes[i] > target_ml) ? (volumes[i] - target_ml) : (target_ml - volumes[i]);
        if (diff < min_diff)
        {
            min_diff = diff;
            nearest_time = times[i];
            nearest_volume = volumes[i];
        }
        else if (diff == min_diff)
        {
            // 如果距离相同，选择体积更大的
            if (volumes[i] > nearest_volume)
            {
                nearest_time = times[i];
                nearest_volume = volumes[i];
            }
        }
    }

    if (nearest_volume > 0)
    {
        // 按比例计算时间
        uint32_t proportional_time = ((uint32_t)target_ml * nearest_time) / nearest_volume;

        // printf("[时间计算] 选择最接近校准点: %u 秒 = %u ml\r\n", nearest_time, nearest_volume);
        // printf("[时间计算] 按比例计算: %u ml × %u秒 / %u ml = %lu 秒\r\n",
        //        target_ml, nearest_time, nearest_volume, proportional_time);

        return (proportional_time > 0xFFFFu) ? 0xFFFFu : (uint16_t)proportional_time;
    }

    return 0;
}

// 根据校准点计算采样时间（单位：秒）
// 逻辑：优先使用组合优化，如果没有合适组合则使用按比例计算
uint16_t calc_sampling_time_by_volume(uint16_t target_ml)
{
    // printf("[采样计算] ========== 计算采样时间 ==========\r\n");
    // printf("[采样计算] 目标水量: %u ml\r\n", target_ml);

    // 获取全部校准点
    uint16_t t[3] = {
        g_CalibrationParams.samplingCalib.time1,
        g_CalibrationParams.samplingCalib.time2,
        g_CalibrationParams.samplingCalib.time3};
    uint16_t v[3] = {
        g_CalibrationParams.samplingCalib.realValue1,
        g_CalibrationParams.samplingCalib.realValue2,
        g_CalibrationParams.samplingCalib.realValue3};

    // printf("[采样计算] 原始校准点:\r\n");
    // printf("[采样计算]   点1: %u 秒 = %u ml (测试点)\r\n", t[0], v[0]);
    // printf("[采样计算]   点2: %u 秒 = %u ml (小水量)\r\n", t[1], v[1]);
    // printf("[采样计算]   点3: %u 秒 = %u ml (大水量)\r\n", t[2], v[2]);

    // 根据目标水量选择计算策略
    if (target_ml == 0)
    {
        // printf("[采样计算] 目标水量为0，返回0\r\n");
        return 0;
    }

    // 策略1: 测试场景 (<= 20ml) - 只使用测试校准点1
    if (target_ml <= 20)
    {
        if (t[0] > 0 && v[0] > 0)
        {
            uint16_t count = (target_ml + v[0] - 1) / v[0]; // 向上取整
            uint32_t total_time = (uint32_t)count * t[0];
            // printf("[采样计算] 测试模式：使用校准点1，重复%d次\r\n", count);
            // printf("[采样计算] 结果: %u 秒\r\n", (uint16_t)total_time);
            return (total_time > 0xFFFFu) ? 0xFFFFu : (uint16_t)total_time;
        }
        else
        {
            // printf("[采样计算] 测试校准点1无效，无法计算\r\n");
            return 0;
        }
    }

    // 策略2: 小水量采样 (21ml - 250ml) - 使用校准点2和3
    else if (target_ml <= 250)
    {
        // 收集有效的大校准点（点2和点3）
        uint16_t usable_t[2], usable_v[2];
        int usable_count = 0;

        // 检查校准点2
        if (t[1] > 0 && v[1] > 0 && v[1] >= 100 && v[1] <= 200)
        {
            usable_t[usable_count] = t[1];
            usable_v[usable_count] = v[1];
            usable_count++;
            // printf("[采样计算] 使用校准点2: %u 秒 = %u ml\r\n", t[1], v[1]);
        }

        // 检查校准点3
        if (t[2] > 0 && v[2] > 0 && v[2] >= 500 && v[2] <= 1000)
        {
            usable_t[usable_count] = t[2];
            usable_v[usable_count] = v[2];
            usable_count++;
            // printf("[采样计算] 使用校准点3: %u 秒 = %u ml\r\n", t[2], v[2]);
        }

        if (usable_count == 0)
        {
            // printf("[采样计算] 没有可用的大校准点，尝试使用校准点2\r\n");
            if (t[1] > 0 && v[1] > 0)
            {
                usable_t[0] = t[1];
                usable_v[0] = v[1];
                usable_count = 1;
            }
        }

        // 使用按比例计算
        if (usable_count > 0)
        {
            return calc_time_proportional(target_ml, usable_t, usable_v, usable_count);
        }
        else
        {
            // printf("[采样计算] 没有有效的校准点\r\n");
            return 0;
        }
    }

    // 策略3: 大水量采样 (> 250ml) - 使用校准点2和3的组合优化
    else
    {
        // 收集有效的大校准点
        uint16_t usable_t[2], usable_v[2];
        int usable_count = 0;

        // 检查校准点2
        if (t[1] > 0 && v[1] > 0)
        {
            usable_t[usable_count] = t[1];
            usable_v[usable_count] = v[1];
            usable_count++;
            // printf("[采样计算] 使用校准点2: %u 秒 = %u ml\r\n", t[1], v[1]);
        }

        // 检查校准点3
        if (t[2] > 0 && v[2] > 0)
        {
            usable_t[usable_count] = t[2];
            usable_v[usable_count] = v[2];
            usable_count++;
            // printf("[采样计算] 使用校准点3: %u 秒 = %u ml\r\n", t[2], v[2]);
        }

        if (usable_count > 0)
        {
            // 对于大水量，直接使用按比例计算
            return calc_time_proportional(target_ml, usable_t, usable_v, usable_count);
        }
        else
        {
            // printf("[采样计算] 没有有效的大水量校准点\r\n");
            return 0;
        }
    }
}

float convertToFloatH(uint8_t data3, uint8_t data2, uint8_t data1, uint8_t data0)
{
    //	float convertToFloat( uint8_t data1, uint8_t data0, uint8_t data3, uint8_t data2 ) {
    int sign;
    int exponent;
    uint32_t mantissa;
    float result;

    sign = (data3 & 0x80) >> 7;

    exponent = ((data3 & 0x7F) << 1) | ((data2 & 0x80) >> 7);
    exponent -= 127;

    mantissa = ((data2 & 0x7F) << 16) | (data1 << 8) | data0;

    float fractional_mantissa = 1.0;

    for (int i = 0; i < 23; i++)
    {
        if (mantissa & (1 << (22 - i)))
        {
            fractional_mantissa += pow(2, -(i + 1));
        }
    }

    result = fractional_mantissa * pow(2, exponent);

    if (sign)
    {
        result = -result;
    }

    return result;
}

/**
 * @brief 将浮点数转换为IEEE 754大端序字节数组
 * @param value 输入浮点数
 * @param bytes 输出字节数组（至少4字节）
 * @note bytes[0]=高字节, bytes[1], bytes[2], bytes[3]=低字节
 * @note 这是convertToFloatH()的逆运算
 */
void convertFloatToBytes(float value, uint8_t *bytes)
{
    typedef union
    {
        float f;
        uint8_t b[4];
    } FloatBytes;

    FloatBytes converter;
    converter.f = value;

    // 转换为大端序（STM32是小端序，需要字节序转换）
    bytes[0] = converter.b[3]; // 高字节
    bytes[1] = converter.b[2];
    bytes[2] = converter.b[1];
    bytes[3] = converter.b[0]; // 低字节
}

void OutletThreeWayValveB(void)
{ // 送样出水三通阀-B桶
    gpio_bits_reset(GPIOD, GPIO_PINS_12);
    gpio_bits_set(GPIOD, GPIO_PINS_13);
}

void OutletThreeWayValveA(void)
{ // 送样出水三通阀-A桶
    gpio_bits_set(GPIOD, GPIO_PINS_12);
    gpio_bits_reset(GPIOD, GPIO_PINS_13);
}

void OutletThreeWayValveClose(void)
{ // 送样出水三通阀-关闭
    gpio_bits_reset(GPIOD, GPIO_PINS_12);
    gpio_bits_reset(GPIOD, GPIO_PINS_13);
}

void BottleEmpty(void)
{ // 留样瓶排空 //PB0 PC5
    gpio_bits_set(GPIOC, GPIO_PINS_5);
    gpio_bits_reset(GPIOB, GPIO_PINS_0); // 瓶排空电机正转

    vTaskDelay(10000); // 等待排空
    gpio_bits_reset(GPIOC, GPIO_PINS_5);
    gpio_bits_set(GPIOB, GPIO_PINS_0); // 瓶排空电机反转
    vTaskDelay(1500);                  // 等待伸缩电机回收

    gpio_bits_set(GPIOC, GPIO_PINS_5);
    gpio_bits_set(GPIOB, GPIO_PINS_0); // 瓶排空电机断电
}

//=============================================================================
// 瞬时留样功能
//=============================================================================

/**
 * @brief 瞬时留样执行函数（通讯/手动触发）
 * 水样不经过AB桶，直接从进水口留样到指定留样瓶
 * @param start_bottle 起始瓶号（0=自主查找空瓶）
 * @param bottle_count 瓶数量
 * @param trigger_source 触发来源（0=手动, 1=通讯）
 * @return 1=成功, 0=失败
 */
uint8_t instant_retention_execute(uint8_t start_bottle, uint8_t bottle_count, uint8_t trigger_source)
{
    printf("[瞬时留样] 开始: 起始瓶=%d, 瓶数=%d, 触发=%s\r\n",
           start_bottle, bottle_count, trigger_source ? "通讯" : "手动");

    // ★ 首次留样时初始化留样瓶系统
    if (!bottle_ensure_initialized())
    {
        printf("[瞬时留样] 留样瓶初始化失败，跳过瞬时留样\r\n");
        return 0;
    }

    // ★ 留样瓶故障检查
    if (bottle_is_fault_active())
    {
        uint8_t fault_code = bottle_get_fault();
        printf("[瞬时留样] 留样瓶系统故障(0x%02X)，跳过瞬时留样\r\n", fault_code);
        return 0;
    }

    // 1. 确定目标瓶号
    uint8_t target_bottle = start_bottle;
    if (start_bottle == 0)
    {
        // 自主查找空瓶
        if (!find_next_empty_bottle(g_RetainBottleState.currentBottle,
                                    g_RetainBottleState.usedMask, &target_bottle))
        {
            printf("[瞬时留样] 错误: 无空瓶可用\r\n");
            return 0;
        }
        printf("[瞬时留样] 自主查找到空瓶: %d\r\n", target_bottle);
    }

    // 验证瓶号有效性
    if (target_bottle < 1 || target_bottle > 24)
    {
        printf("[瞬时留样] 错误: 瓶号无效(%d)\r\n", target_bottle);
        return 0;
    }

    // 2. 获取留样参数
    uint16_t retention_volume = g_RetainSampleConfig.SampleVolume;
    uint16_t blowback_time = g_RetainSampleConfig.BlowbackTime;
    if (blowback_time > 36000)
        blowback_time = 36000; // 限制最大值

    uint16_t retention_time = calc_retain_time_by_volume(retention_volume);
    if (retention_time == 0)
    {
        printf("[瞬时留样] 错误: 计算留样时间失败(体积=%d)\r\n", retention_volume);
        return 0;
    }
    uint16_t rpm = g_SystemSettingConfig.Motorspeed;

    printf("[瞬时留样] 参数: 瓶号=%d, 体积=%dml, 时间=%ds, 反吹=%ds, 转速=%d\r\n",
           target_bottle, retention_volume, retention_time, blowback_time, rpm);

    // 3. 记录TSDB开始
    extern void log_retain_record(const RetainLogRecord *record);
    RetainLogRecord log = {0};
    log.retain_mode = 4;                        // 瞬时留样模式
    log.retain_reason = trigger_source ? 9 : 8; // 9=通讯触发, 8=手动触发
    log.start_time = rtc_counter_get();
    log.bottle_number = target_bottle;
    log.retain_volume = retention_volume;
    log.delivery_time = 0; // 瞬时留样无对应送样

    // 4. 执行留样流程
    uint32_t t0;

    // 阶段1：留样瓶定位
    printf("[瞬时留样] 阶段1: 瓶定位\r\n");
    if (!bottle_move_to(target_bottle, 40, 50000))
    {
        printf("[瞬时留样] 错误: 瓶定位失败\r\n");
        log.result = 0;
        log.error_code = 1; // 定位失败
        log.end_time = rtc_counter_get();
        log_retain_record(&log);

        // 更新瞬时留样信息（失败）
        rtc_time_get();
        g_LastInstantRetainInfo.year = calendar.year;
        g_LastInstantRetainInfo.month = calendar.month;
        g_LastInstantRetainInfo.day = calendar.date;
        g_LastInstantRetainInfo.hour = calendar.hour;
        g_LastInstantRetainInfo.minute = calendar.min;
        g_LastInstantRetainInfo.second = calendar.sec;
        g_LastInstantRetainInfo.result = 0;     // 失败
        g_LastInstantRetainInfo.failReason = 4; // 瓶定位故障
        g_LastInstantRetainInfo.startBottle = target_bottle;
        g_LastInstantRetainInfo.bottleCount = bottle_count;
        g_LastInstantRetainInfo.volume = retention_volume;
        return 0;
    }
    printf("[瞬时留样] 瓶定位完成\r\n");

    // 阶段2：阀门设置 - 瞬时留样路径
    printf("[瞬时留样] 阶段2: 阀门设置\r\n");
    OutletThreeWayValveClose();  // 出水阀关闭（不使用AB桶）
    InstantThreeWayValveInstant; // 瞬时模式
    SampleThreeWayValveSTAY;     // 留样路径

    // 阶段3：反吹清线
    printf("[瞬时留样] 阶段3: 反吹清线 %ds\r\n", blowback_time);
    t0 = g_tmr2_seconds;
    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    printf("[瞬时留样] 反吹完成\r\n");

    // 延时500ms：反转→正转方向切换保护
    vTaskDelay(pdMS_TO_TICKS(500));

    // 阶段4：瞬时留样
    printf("[瞬时留样] 阶段4: 留样 %ds\r\n", retention_time);
    t0 = g_tmr2_seconds;
    MotorRun(2, 1, rpm); // 送样电机正转
    while ((g_tmr2_seconds - t0) < retention_time)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        MotorRun(2, 1, rpm);
    }
    MotorStop(2);
    printf("[瞬时留样] 留样完成\r\n");

    // 延时500ms：正转→反转方向切换保护
    vTaskDelay(pdMS_TO_TICKS(500));

    // 阶段5：反抽清理
    printf("[瞬时留样] 阶段5: 反抽清理 %ds\r\n", blowback_time);
    t0 = g_tmr2_seconds;
    MotorRun(2, 0, rpm); // 送样电机反转
    while ((g_tmr2_seconds - t0) < blowback_time)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        MotorRun(2, 0, rpm);
    }
    MotorStop(2);
    printf("[瞬时留样] 反抽完成\r\n");

    // 阶段6：复位阀门
    printf("[瞬时留样] 阶段6: 复位阀门\r\n");
    InstantThreeWayValveDirect; // 恢复直通模式
    SampleThreeWayValveSample;  // 恢复送样模式

    // 5. 标记瓶位已使用
    mark_bottle_used(&g_RetainBottleState.usedMask, target_bottle);
    g_RetainBottleState.currentBottle = target_bottle;
    printf("[瞬时留样] 瓶位%d已标记为已使用\r\n", target_bottle);

    // 6. 记录TSDB完成
    log.end_time = rtc_counter_get();
    log.result = 1; // 成功
    log.error_code = 0;
    log_retain_record(&log);

    // 7. 更新瞬时留样信息（供Modbus读取）
    rtc_time_get(); // 刷新calendar
    g_LastInstantRetainInfo.year = calendar.year;
    g_LastInstantRetainInfo.month = calendar.month;
    g_LastInstantRetainInfo.day = calendar.date;
    g_LastInstantRetainInfo.hour = calendar.hour;
    g_LastInstantRetainInfo.minute = calendar.min;
    g_LastInstantRetainInfo.second = calendar.sec;
    g_LastInstantRetainInfo.result = 1; // 成功
    g_LastInstantRetainInfo.failReason = 0;
    g_LastInstantRetainInfo.startBottle = target_bottle;
    g_LastInstantRetainInfo.bottleCount = bottle_count;
    g_LastInstantRetainInfo.volume = retention_volume;
    g_LastInstantRetainInfo.mode = trigger_source ? RETAIN_MODE_MODBUS : RETAIN_MODE_DIRECT;
    g_LastInstantRetainInfo.trigger = trigger_source ? 9 : 8;
    g_LastInstantRetainInfo.addAcid = g_RetainSampleConfig.EnableAcid ? 1 : 0;
    g_LastInstantRetainInfo.acidType = 0;
    g_LastInstantRetainInfo.acidRatio = 0;

    printf("[瞬时留样] 成功完成: 瓶=%d, 体积=%dml\r\n", target_bottle, retention_volume);

    // 如果需要留多瓶，递归调用
    if (bottle_count > 1)
    {
        printf("[瞬时留样] 继续下一瓶 (剩余%d瓶)\r\n", bottle_count - 1);
        return instant_retention_execute(0, bottle_count - 1, trigger_source);
    }

    return 1;
}

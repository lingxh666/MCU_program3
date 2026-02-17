#include "wiegand.h"
#include "freertos_app.h"
#include "FreeRTOS.h"
#include "task.h"

// 全局韦根数据结构
static volatile WiegandData_t g_wiegand_data = {0};

// 任务通知相关
static TaskHandle_t g_wiegand_task_handle = NULL;

// 初始化韦根模块
void wiegand_init(void)
{
    wiegand_reset();
}

// 重置韦根数据
void wiegand_reset(void)
{
    g_wiegand_data.raw_data = 0;
    g_wiegand_data.bit_count = 0;
    g_wiegand_data.last_bit_time = 0;
    g_wiegand_data.state = WIEGAND_STATE_IDLE;
    g_wiegand_data.data_ready = false;
}

// 检查数据是否就绪
bool wiegand_is_data_ready(void)
{
    bool ready;
    taskENTER_CRITICAL();
    ready = g_wiegand_data.data_ready;
    taskEXIT_CRITICAL();
    return ready;
}

// 获取原始数据
WiegandData_t wiegand_get_raw_data(void)
{
    WiegandData_t data;
    taskENTER_CRITICAL();
    data = g_wiegand_data;
    taskEXIT_CRITICAL();
    return data;
}

// 解析26位韦根数据
Wiegand26_t wiegand_parse_26bit(void)
{
    Wiegand26_t result = {0};
    
    if (!g_wiegand_data.data_ready || g_wiegand_data.bit_count != 26) {
        printf("解析失败: data_ready=%d, bit_count=%d\r\n", 
               g_wiegand_data.data_ready, g_wiegand_data.bit_count);
        result.valid = false;
        return result;
    }
    
    uint32_t data = (uint32_t)(g_wiegand_data.raw_data & 0x3FFFFFF); // 取低26位
    printf("\r\n=== 26位Wiegand数据解析 ===\r\n");
    printf("原始26位数据: 0x%06X (%u)\r\n", data, data);
    
    // 显示所有可能的解析方式
    printf("\n[可能的卡号解读]\r\n");
    printf("1. 原始/2:                  %u\r\n", data / 2);
    printf("2. 原始 >> 1:              %u\r\n", data >> 1);
    printf("3. (原始 >> 1) & 0xFFFFFF: %u\r\n", (data >> 1) & 0xFFFFFF);
    printf("4. (原始 >> 1) & 0xFFFFF:  %u\r\n", (data >> 1) & 0xFFFFF);
    printf("5. 原始 & 0xFFFFFF:        %u\r\n", data & 0xFFFFFF);
    printf("6. (原始 >> 2) & 0xFFFFFF: %u\r\n", (data >> 2) & 0xFFFFFF);
    
    // 提取各部分数据
    uint8_t even_parity = (data >> 25) & 0x01;     // 最高位：偶校验位
    uint8_t facility_code = (data >> 17) & 0xFF;   // 位24-17：设施码
    uint16_t card_number = (data >> 1) & 0xFFFF;   // 位16-1：卡号
    uint8_t odd_parity = data & 0x01;              // 最低位：奇校验位
    
    printf("\n[标准26位Wiegand解析]\r\n");
    printf("偶校验: %d, 厂商码: %d, 卡号: %d, 奇校验: %d\r\n",
           even_parity, facility_code, card_number, odd_parity);
    
    // 显示更多组合方式
    printf("\n[厂商码 + 卡号 组合]\r\n");
    printf("7. 厂商码*65536 + 卡号:  %u\r\n", facility_code * 65536 + card_number);
    printf("8. 厂商码*100000 + 卡号: %u\r\n", facility_code * 100000 + card_number);
    printf("9. 厂商码*50000 + 卡号:  %u\r\n", facility_code * 50000 + card_number);
    
    // 显示二进制位序列
    printf("\n[二进制表示]\r\n");
    printf("二进制: ");
    for (int i = 25; i >= 0; i--) {
        printf("%d", (data >> i) & 1);
        if (i > 0 && i % 4 == 0) printf(" ");
    }
    printf("\r\n");
    printf("===================================\r\n\n");
    
    // 计算偶校验位(前13位)
    uint8_t even_calc = 0;
    uint32_t temp = data >> 13;
    for (int i = 0; i < 13; i++) {
        if (temp & (1 << i)) {
            even_calc++;
        }
    }
    even_calc = (even_calc % 2) == 0 ? 0 : 1;
    
    // 计算奇校验位(后13位)
    uint8_t odd_calc = 0;
    temp = (data >> 1) & 0x1FFF;
    for (int i = 0; i < 13; i++) {
        if (temp & (1 << i)) {
            odd_calc++;
        }
    }
    odd_calc = (odd_calc % 2) == 0 ? 0 : 1;
    
    printf("校验结果: 偶校验计算=%d, 奇校验计算=%d\r\n", even_calc, odd_calc);
    
    // 填充结果
    result.facility_code = facility_code;
    // 使用正确的公式：卡号 = Facility * 65536 + Card Number
    result.card_id = (uint32_t)facility_code * 65536 + card_number;
    result.parity_ok = (even_parity == even_calc) && (odd_parity == odd_calc);
    result.valid = true;
    
    printf("校验结果: %s\r\n", result.parity_ok ? "通过" : "失败");
    printf("\n[最终卡号] = %u (厂商码*65536 + 卡号)\r\n", result.card_id);
    
    return result;
}

// 获取卡号(简化接口)
uint32_t wiegand_get_card_id(void)
{
    uint32_t card_id = 0;
    
    if (!g_wiegand_data.data_ready) {
        return 0;
    }
    
    // 根据位数选择不同的解析方式
    if (g_wiegand_data.bit_count == 26) {
        // 标准26位韦根格式
        Wiegand26_t parsed = wiegand_parse_26bit();
        if (parsed.valid) {
            // 即使校验失败也返回卡号（某些卡可能使用非标准校验）
            card_id = parsed.card_id;
            if (!parsed.parity_ok) {
                printf("[WIEGAND] Warning: Parity check failed, but returning card_id=%u\r\n", card_id);
            }
        }
    } else if (g_wiegand_data.bit_count >= 2 && g_wiegand_data.bit_count <= 64) {
        // 非标准格式，直接返回原始数据作为卡号
        card_id = (uint32_t)(g_wiegand_data.raw_data & 0xFFFFFFFF);
        printf("[WIEGAND] Non-standard: %d bits, card_id=%u (0x%X)\r\n", 
               g_wiegand_data.bit_count, card_id, card_id);
    }
    
    // 读取完成后立即清除数据，避免累积
    wiegand_clear_data();
    
    return card_id;
}

// 清除数据
void wiegand_clear_data(void)
{
    taskENTER_CRITICAL();
    g_wiegand_data.raw_data = 0;
    g_wiegand_data.bit_count = 0;
    g_wiegand_data.last_bit_time = 0;
    g_wiegand_data.data_ready = false;
    g_wiegand_data.state = WIEGAND_STATE_IDLE;
    taskEXIT_CRITICAL();
}

// D0中断处理(接收到0位)
void wiegand_d0_interrupt(void)
{
    uint32_t current_time = g_tmr3_milliseconds; // 直接使用毫秒
    
    // 检查超时
    if (g_wiegand_data.state == WIEGAND_STATE_RECEIVING) {
        if ((current_time - g_wiegand_data.last_bit_time) > WIEGAND_TIMEOUT_MS) {
            // 超时，重新开始
            g_wiegand_data.raw_data = 0;
            g_wiegand_data.bit_count = 0;
            g_wiegand_data.state = WIEGAND_STATE_IDLE;
        }
    }
    
    // 检查是否超过最大位数
    if (g_wiegand_data.bit_count >= WIEGAND_MAX_BITS) {
        g_wiegand_data.state = WIEGAND_STATE_ERROR;
        return;
    }
    
    // 接收0位(左移，不设置最低位)
    g_wiegand_data.raw_data <<= 1;
    g_wiegand_data.bit_count++;
    g_wiegand_data.last_bit_time = current_time;
    g_wiegand_data.state = WIEGAND_STATE_RECEIVING;
    g_wiegand_data.data_ready = false;
}

// D1中断处理(接收到1位)
void wiegand_d1_interrupt(void)
{
    uint32_t current_time = g_tmr3_milliseconds; // 直接使用毫秒
    
    // 检查超时
    if (g_wiegand_data.state == WIEGAND_STATE_RECEIVING) {
        if ((current_time - g_wiegand_data.last_bit_time) > WIEGAND_TIMEOUT_MS) {
            // 超时，重新开始
            g_wiegand_data.raw_data = 0;
            g_wiegand_data.bit_count = 0;
            g_wiegand_data.state = WIEGAND_STATE_IDLE;
        }
    }
    
    // 检查是否超过最大位数
    if (g_wiegand_data.bit_count >= WIEGAND_MAX_BITS) {
        g_wiegand_data.state = WIEGAND_STATE_ERROR;
        return;
    }
    
    // 接收1位(左移并设置最低位)
    g_wiegand_data.raw_data <<= 1;
    g_wiegand_data.raw_data |= 1;
    g_wiegand_data.bit_count++;
    g_wiegand_data.last_bit_time = current_time;
    g_wiegand_data.state = WIEGAND_STATE_RECEIVING;
    g_wiegand_data.data_ready = false;
}

// 超时检查(需要定期调用)
void wiegand_timeout_check(void)
{
    if (g_wiegand_data.state != WIEGAND_STATE_RECEIVING) {
        return;
    }
    
    uint32_t current_time = g_tmr3_milliseconds; // 直接使用毫秒
    
    if ((current_time - g_wiegand_data.last_bit_time) > WIEGAND_TIMEOUT_MS) {
        // 接收完成
        if (g_wiegand_data.bit_count > 0) {
            g_wiegand_data.state = WIEGAND_STATE_COMPLETE;
            g_wiegand_data.data_ready = true;

            // ★ ISR中禁止printf，改为设置标志位，由任务上下文打印调试信息
            // printf调用已移除，避免ISR阻塞

            // 发送任务通知（不在这里读取卡号，让上层应用读取）
            if (g_wiegand_task_handle != NULL) {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//                vTaskNotifyGiveFromISR(g_wiegand_task_handle, &xHigherPriorityTaskWoken);
							  xTaskNotifyFromISR(g_wiegand_task_handle, 0x02, eSetBits, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
                // ★ ISR中禁止printf
            }
            // else: 未注册任务句柄，静默忽略
        } else {
            g_wiegand_data.state = WIEGAND_STATE_IDLE;
        }
    }
}

// 等待门禁卡(带超时)
bool wiegand_wait_for_card(uint32_t timeout_ms, uint32_t *card_id)
{
    uint32_t start_time = g_tmr3_milliseconds;
    uint32_t current_time;
    
    // 清除之前的数据
    wiegand_clear_data();
    
    while (1) {
        current_time = g_tmr3_milliseconds;
        
        // 检查超时
        if ((current_time - start_time) >= timeout_ms) {
            return false;
        }
        
        // 检查是否有新的卡数据
        if (wiegand_is_data_ready()) {
            uint32_t id = wiegand_get_card_id();
            if (id != 0) {
                *card_id = id;
                wiegand_clear_data();
                return true;
            }
            wiegand_clear_data();
        }
        
        // 短暂延时
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// 注册任务句柄(用于接收通知)
void wiegand_register_task(TaskHandle_t task_handle)
{
    taskENTER_CRITICAL();
    g_wiegand_task_handle = task_handle;
    taskEXIT_CRITICAL();
}

// 取消注册任务句柄
void wiegand_unregister_task(void)
{
    taskENTER_CRITICAL();
    g_wiegand_task_handle = NULL;
    taskEXIT_CRITICAL();
}

// 等待门禁卡通知
uint32_t wiegand_wait_card_notification(uint32_t timeout_ms)
{
    // 等待任务通知
    uint32_t notification_value = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
    
    if (notification_value > 0) {
        // 收到通知，获取卡号
        uint32_t card_id = wiegand_get_card_id();
        wiegand_clear_data();
        return card_id;
    }
    
    return 0; // 超时或无效
}

// 处理门禁卡事件
void wiegand_process_card_event(uint32_t card_id)
{
    // 这里可以添加门禁卡处理逻辑
    // 例如：记录日志、验证权限、触发相应动作等
    
    // 示例：打印卡号(实际使用时可以根据需要修改)
    printf("卡片: ID=%d\r\n", card_id);
    
    // 可以在这里添加具体的业务逻辑
    // 例如：
    // - 验证卡号是否有效
    // - 记录刷卡日志到TSDB
    // - 触发采样或其他操作
    // - 更新系统状态等
}

// 韦根模块状态检查函数
void wiegand_status_check(void)
{
    printf("\r\n=== Wiegand状态检查 ===\r\n");
    printf("当前状态: %d\r\n", g_wiegand_data.state);
    printf("位数: %d\r\n", g_wiegand_data.bit_count);
    printf("原始数据: 0x%llX\r\n", g_wiegand_data.raw_data);
    printf("数据就绪: %s\r\n", g_wiegand_data.data_ready ? "是" : "否");
    printf("最后一位时间: %lu ms\r\n", g_wiegand_data.last_bit_time);
    printf("当前时间: %lu ms\r\n", g_tmr3_milliseconds);
    printf("任务句柄已注册: %s\r\n", g_wiegand_task_handle ? "是" : "否");
    
    // 读取GPIO引脚状态
    printf("PD2 (D0) 电平: %s\r\n", gpio_input_data_bit_read(GPIOD, GPIO_PINS_2) ? "高" : "低");
    printf("PD3 (D1) 电平: %s\r\n", gpio_input_data_bit_read(GPIOD, GPIO_PINS_3) ? "高" : "低");
    
    // 检查中断使能状态
    printf("EXINT2 使能: %s\r\n", (EXINT->inten & EXINT_LINE_2) ? "是" : "否");
    printf("EXINT3 使能: %s\r\n", (EXINT->inten & EXINT_LINE_3) ? "是" : "否");
    
    // 诊断D0D1始终为低电平的问题
    if (!gpio_input_data_bit_read(GPIOD, GPIO_PINS_2) && !gpio_input_data_bit_read(GPIOD, GPIO_PINS_3)) {
        printf("\r\n*** 严重问题 ***\r\n");
        printf("D0 与 D1 同为低电平 - 错误状态!\r\n");
        printf("Wiegand 协议要求空闲时 D0/D1 为高电平。\r\n");
        printf("根因分析:\r\n");
        printf("1. 读卡器未上电（+24V缺失？）\r\n");
        printf("2. 读卡器 D0/D1 输出异常\r\n");
        printf("3. 光耦电路接线错误\r\n");
        printf("4. IN1/IN2 线路短接到地\r\n");
        printf("\r\n重要: 修复硬件后再期待卡片检测！\r\n");
        printf("*******************\r\n");
    }
    
    printf("============================\r\n\r\n");
}

// 手动测试中断触发
void wiegand_test_interrupt(void)
{
    printf("测试中断触发...\r\n");
    printf("手动调用 D0 中断...\r\n");
    wiegand_d0_interrupt();
    printf("手动调用 D1 中断...\r\n");
    wiegand_d1_interrupt();
    printf("测试完成。\r\n");
}

// 实时监控引脚状态
void wiegand_monitor_pins(uint32_t duration_ms)
{
    printf("监控 D0/D1 引脚 %lu ms...\r\n", duration_ms);
    printf("监控期间请刷卡...\r\n");
    
    uint32_t start_time = g_tmr3_milliseconds;
    uint8_t last_d0 = gpio_input_data_bit_read(GPIOD, GPIO_PINS_2);
    uint8_t last_d1 = gpio_input_data_bit_read(GPIOD, GPIO_PINS_3);
    
    printf("初始: D0=%s, D1=%s\r\n", last_d0 ? "高" : "低", last_d1 ? "高" : "低");
    
    while ((g_tmr3_milliseconds - start_time) < duration_ms) {
        uint8_t current_d0 = gpio_input_data_bit_read(GPIOD, GPIO_PINS_2);
        uint8_t current_d1 = gpio_input_data_bit_read(GPIOD, GPIO_PINS_3);
        
        if (current_d0 != last_d0) {
            printf("D0 变化: %s -> %s 于 %lu ms\r\n", 
                   last_d0 ? "高" : "低", 
                   current_d0 ? "高" : "低", 
                   g_tmr3_milliseconds);
            last_d0 = current_d0;
        }
        
        if (current_d1 != last_d1) {
            printf("D1 变化: %s -> %s 于 %lu ms\r\n", 
                   last_d1 ? "高" : "低", 
                   current_d1 ? "高" : "低", 
                   g_tmr3_milliseconds);
            last_d1 = current_d1;
        }
        
        // 短暂延时避免占用太多CPU
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    printf("监控完成。\r\n");
}

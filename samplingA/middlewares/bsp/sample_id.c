/**
 * @file sample_id.c
 * @brief 样本ID生成器实现
 */

#include "sample_id.h"
#include "rtc.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

// 全局生成器实例
static SampleIdGenerator g_id_gen = {0};

// 外部RTC日历变量
extern calendar_type calendar;

/**
 * @brief 初始化样本ID生成器
 */
uint8_t sample_id_generator_init(void)
{
    // 创建互斥锁
    g_id_gen.mutex = xSemaphoreCreateMutex();
    if (g_id_gen.mutex == NULL)
    {
        return 0; // 创建失败
    }

    // 初始化序列号和时间戳
    g_id_gen.last_seq = 0;
    g_id_gen.last_timestamp = 0;

    return 1; // 成功
}

/**
 * @brief 生成唯一样本ID
 */
uint8_t generate_sample_id(char *out_id, size_t buf_size)
{
    // 参数检查
    if (out_id == NULL || buf_size < 18)
    {
        return 0; // 缓冲区太小
    }

    // 获取互斥锁（最多等待10ms）
    if (xSemaphoreTake((SemaphoreHandle_t)g_id_gen.mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        return 0; // 获取锁失败
    }

    // 获取当前时间戳（秒，Unix基准）
    uint32_t now_sec = rtc_counter_get();

    uint16_t seq;

    // 判断是否在同一秒内
    if (now_sec == g_id_gen.last_timestamp)
    {
        // 同一秒内，序列号递增
        g_id_gen.last_seq++;
        seq = g_id_gen.last_seq;

        // 检查序列号溢出（每秒最多1000个样本）
        if (seq >= 1000)
        {
            xSemaphoreGive((SemaphoreHandle_t)g_id_gen.mutex);
            return 0; // 序列号溢出，生成失败
        }
    }
    else
    {
        // 新的一秒，序列号重置
        g_id_gen.last_timestamp = now_sec;
        g_id_gen.last_seq = 0;
        seq = 0;
    }

    // 格式化样本ID：YYYYMMDDHHmmss-SSS
    snprintf(out_id, buf_size, "%04u%02u%02u%02u%02u%02u-%03u",
             calendar.year,
             calendar.month,
             calendar.date,
             calendar.hour,
             calendar.min,
             calendar.sec,
             seq);

    // 释放互斥锁
    xSemaphoreGive((SemaphoreHandle_t)g_id_gen.mutex);

    return 1; // 成功
}

/**
 * @brief 获取当前序列号（测试用）
 */
uint16_t sample_id_get_current_seq(void)
{
    uint16_t seq;

    if (xSemaphoreTake((SemaphoreHandle_t)g_id_gen.mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        seq = g_id_gen.last_seq;
        xSemaphoreGive((SemaphoreHandle_t)g_id_gen.mutex);
    }
    else
    {
        seq = 0xFFFF; // 获取锁失败
    }

    return seq;
}

/**
 * @brief 重置生成器（测试用）
 */
void sample_id_generator_reset(void)
{
    if (xSemaphoreTake((SemaphoreHandle_t)g_id_gen.mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        g_id_gen.last_seq = 0;
        g_id_gen.last_timestamp = 0;
        xSemaphoreGive((SemaphoreHandle_t)g_id_gen.mutex);
    }
}

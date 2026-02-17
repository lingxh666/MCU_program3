/**
 * @file sample_id.h
 * @brief 样本ID生成器（线程安全）
 * @details 生成格式：YYYYMMDDHHmmss-SSS（17字符+'\0'=18字节）
 *          支持每秒最多1000个样本（序列号000-999）
 */

#ifndef __SAMPLE_ID_H__
#define __SAMPLE_ID_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdint.h>
#include <stddef.h>

/**
 * @brief 样本ID生成器结构体
 */
typedef struct
{
    uint16_t last_seq;          // 上一秒的序列号
    uint32_t last_timestamp;    // 上一次时间戳（秒）
    void *mutex;                // 互斥锁句柄（FreeRTOS SemaphoreHandle_t）
} SampleIdGenerator;

/**
 * @brief 初始化样本ID生成器
 * @note 系统启动时调用一次
 * @return 1-成功 0-失败
 */
uint8_t sample_id_generator_init(void);

/**
 * @brief 生成唯一样本ID
 * @param out_id 输出缓冲区（至少18字节）
 * @param buf_size 缓冲区大小
 * @return 1-成功 0-失败
 *
 * @note 线程安全，使用互斥锁保护
 * @note 同一秒内序列号递增（000-999）
 * @note 跨秒时序列号重置为000
 *
 * @example
 * char sample_id[18];
 * if (generate_sample_id(sample_id, sizeof(sample_id))) {
 *     printf("Generated ID: %s\n", sample_id); // "20251222153045-001"
 * }
 */
uint8_t generate_sample_id(char *out_id, size_t buf_size);

/**
 * @brief 获取当前序列号（测试用）
 * @return 当前序列号
 */
uint16_t sample_id_get_current_seq(void);

/**
 * @brief 重置生成器（测试用）
 */
void sample_id_generator_reset(void);

#ifdef __cplusplus
}
#endif

#endif // __SAMPLE_ID_H__

#ifndef LBS_LOCATION_H
#define LBS_LOCATION_H

#include <stdint.h>
#include "at32f403a_407.h"

#ifdef __cplusplus
extern "C" {
#endif

/* LBS定位状态 */
typedef enum {
    LBS_STATE_IDLE = 0,       // 空闲（未启动或已完成）
    LBS_STATE_WAIT_RESP,      // 等待+MLBSLOC响应
    LBS_STATE_DONE,           // 完成（成功或失败）
} LBS_State_t;

/* LocationInfo_t 定义在 app_flashdb.h 中 */

/**
 * @brief 启动LBS定位初始化（非阻塞）
 * @param usart_x 串口指针（USART6）
 * @note  发送配置指令和获取位置指令，然后进入等待状态
 *        只执行一次，重复调用无效
 */
void LBS_StartInit(usart_type *usart_x);

/**
 * @brief 检查LBS响应（在task02主循环中调用）
 * @param buf 串口接收缓冲区
 * @return 0=未完成继续等待，1=已完成（成功或超时）
 */
uint8_t LBS_CheckResponse(const char *buf);

/**
 * @brief 获取当前LBS状态
 * @return LBS_State_t 状态值
 */
LBS_State_t LBS_GetState(void);

/**
 * @brief 检查LBS是否已初始化完成（无论成功与否）
 * @return 1=已完成，0=未完成
 */
uint8_t LBS_IsInitialized(void);

#ifdef __cplusplus
}
#endif

#endif /* LBS_LOCATION_H */

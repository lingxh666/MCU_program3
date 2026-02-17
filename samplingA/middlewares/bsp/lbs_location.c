#include "lbs_location.h"
#include "flashDB/app_flashdb.h"
#include "work.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern uint32_t rtc_counter_get(void);

/* 状态变量 */
static LBS_State_t s_lbs_state = LBS_STATE_IDLE;
static uint8_t s_lbs_initialized = 0;      // 定位成功标志（成功后不再重试）
static uint32_t s_lbs_send_tick = 0;       // 发送AT+MLBSLOC的时间戳
static uint8_t s_lbs_retry_count = 0;      // 当前重试次数
static usart_type *s_lbs_usart = NULL;     // 保存串口指针用于重试

/* 配置 */
#define LBS_RESP_TIMEOUT_MS  15000         // 15秒超时
#define LBS_MAX_RETRY        10            // 最大重试次数

/**
 * @brief 内部函数：发送获取位置指令
 */
static void LBS_SendLocationRequest(usart_type *usart_x)
{
    char *cmd = "AT+MLBSLOC\r\n";
    SendData(usart_x, cmd, strlen(cmd));
    printf("[LBS] 发送: AT+MLBSLOC (第%d次，超时%dms)\r\n", s_lbs_retry_count + 1, LBS_RESP_TIMEOUT_MS);
    
    s_lbs_send_tick = xTaskGetTickCount();
    s_lbs_state = LBS_STATE_WAIT_RESP;
}

/**
 * @brief 启动LBS定位初始化（非阻塞）
 */
void LBS_StartInit(usart_type *usart_x)
{
    if (s_lbs_initialized) {
        printf("[LBS] 已定位成功，跳过\r\n");
        return;
    }
    
    // 如果正在等待响应，不重复启动
    if (s_lbs_state == LBS_STATE_WAIT_RESP) {
        printf("[LBS] 正在等待响应，跳过\r\n");
        return;
    }
    
    printf("[LBS] ========== 开始定位初始化 ==========\r\n");
    
    // 保存串口指针用于重试
    s_lbs_usart = usart_x;
    s_lbs_retry_count = 0;
    
    // 配置指令1：设置LBS方法为OneOS定位服务
    char *cmd1 = "AT+MLBSCFG=\"method\",40\r\n";
    SendData(usart_x, cmd1, strlen(cmd1));
    printf("[LBS] 发送: AT+MLBSCFG=\"method\",40\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 配置指令2：启用邻区信息参与定位
    char *cmd2 = "AT+MLBSCFG=\"nearbtsen\",1\r\n";
    SendData(usart_x, cmd2, strlen(cmd2));
    printf("[LBS] 发送: AT+MLBSCFG=\"nearbtsen\",1\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 发送获取位置指令
    LBS_SendLocationRequest(usart_x);
}

/**
 * @brief 解析LBS响应
 * @param buf 响应字符串
 * @param loc 输出的位置信息
 * @return 1=解析成功，0=失败
 */
static uint8_t LBS_ParseResponse_Internal(const char *buf, LocationInfo_t *loc)
{
    // 查找 "+MLBSLOC:" 响应
    char *p = strstr(buf, "+MLBSLOC:");
    if (!p) return 0;
    
    // 解析格式: +MLBSLOC: 100,106.49899330,29.61772980
    int code = 0;
    float lon = 0.0f, lat = 0.0f;
    
    // 跳过 "+MLBSLOC: "
    p += 9;
    while (*p == ' ') p++;
    
    // 解析错误码
    code = atoi(p);
    
    // 查找第一个逗号后的经度
    p = strchr(p, ',');
    if (!p) return 0;
    p++;
    lon = (float)atof(p);
    
    // 查找第二个逗号后的纬度
    p = strchr(p, ',');
    if (!p) return 0;
    p++;
    lat = (float)atof(p);
    
    printf("[LBS] 解析结果: code=%d, lon=%.6f, lat=%.6f\r\n", code, lon, lat);
    
    if (code == 100) {
        // 保留2位小数（四舍五入）
        loc->longitude = ((int)(lon * 100.0f + 0.5f)) / 100.0f;
        loc->latitude = ((int)(lat * 100.0f + 0.5f)) / 100.0f;
        loc->valid = 1;
        loc->timestamp = rtc_counter_get();
        return 1;
    }
    
    printf("[LBS] 定位错误码: %d\r\n", code);
    return 0;
}

/**
 * @brief 内部函数：触发重试
 * @return 1=已触发重试，0=达到最大次数放弃
 */
static uint8_t LBS_TriggerRetry(void)
{
    s_lbs_retry_count++;
    
    if (s_lbs_retry_count >= LBS_MAX_RETRY) {
        printf("[LBS] 已达最大重试次数(%d)，放弃定位\r\n", LBS_MAX_RETRY);
        s_lbs_state = LBS_STATE_DONE;
        // 不设置s_lbs_initialized，允许MQTT重连后再次尝试
        return 0;
    }
    
    printf("[LBS] 准备第%d次重试...\r\n", s_lbs_retry_count + 1);
    
    // 延迟2秒后重试
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    if (s_lbs_usart) {
        LBS_SendLocationRequest(s_lbs_usart);
    }
    return 1;
}

/**
 * @brief 检查LBS响应（在task02主循环中调用）
 */
uint8_t LBS_CheckResponse(const char *buf)
{
    if (s_lbs_state != LBS_STATE_WAIT_RESP) {
        return 1;  // 不在等待状态，视为已完成
    }
    
    // 检查超时
    uint32_t elapsed = (xTaskGetTickCount() - s_lbs_send_tick) * portTICK_PERIOD_MS;
    if (elapsed > LBS_RESP_TIMEOUT_MS) {
        printf("[LBS] 定位超时(%ums)\r\n", (unsigned int)elapsed);
        
        // 尝试重试
        if (!LBS_TriggerRetry()) {
            return 1;  // 放弃
        }
        return 0;  // 继续等待重试结果
    }
    
    // 检查响应内容
    if (buf && strstr(buf, "+MLBSLOC:") != NULL) {
        LocationInfo_t loc = {0};
        
        if (LBS_ParseResponse_Internal(buf, &loc)) {
            // 定位成功，保存到KVDB
            if (cfg_save_location(&loc)) {
                printf("[LBS] ★ 定位成功并保存: 经度=%.2f, 纬度=%.2f\r\n", 
                       loc.longitude, loc.latitude);
            } else {
                printf("[LBS] 定位成功但保存失败: 经度=%.2f, 纬度=%.2f\r\n", 
                       loc.longitude, loc.latitude);
            }
            s_lbs_state = LBS_STATE_DONE;
            s_lbs_initialized = 1;  // 成功后标记，不再尝试
            return 1;
        } else {
            // 解析失败（可能是错误码非100），尝试重试
            printf("[LBS] 定位响应解析失败\r\n");
            if (!LBS_TriggerRetry()) {
                return 1;  // 放弃
            }
            return 0;  // 继续等待重试结果
        }
    }
    
    return 0;  // 继续等待
}

/**
 * @brief 获取当前LBS状态
 */
LBS_State_t LBS_GetState(void)
{
    return s_lbs_state;
}

/**
 * @brief 检查LBS是否已初始化完成
 */
uint8_t LBS_IsInitialized(void)
{
    return s_lbs_initialized;
}

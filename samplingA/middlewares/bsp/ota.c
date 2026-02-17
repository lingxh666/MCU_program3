#include "ota.h"
#include "commtrigger.h"
#include "freertos_app.h"
#include "flash.h"
#include "sampling.h"
#include "work.h"
#include "app_flashdb.h"
#include "retain_judge.h"
#include "at32f403a_407_wdt.h"
#include "record_cache.h"
#include "at32f403a_407_rtc.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// OTA专用校验函数（16位累加和，不使用硬件CRC，避免影响485通信）
static uint16_t OTA_Checksum16(uint8_t *data, uint32_t length)
{
    uint32_t sum = 0;
    uint32_t index;

    for (index = 0; index < length; index++)
    {
        sum += data[index];
    }

    // 返回低16位作为校验和
    return (uint16_t)(sum & 0xFFFF);
}

// OTA 延时：使用已经在 main 初始化的 DWT 延时
#define OTA_DELAY_MS(ms) delay_ms((uint16_t)(ms))

// 引入全局配置变量
extern CommSettingConfig g_CommSettingConfig;

// OTA文件地址定义
#define OTA_START_ADDR TEMPLATE_START_ADDR

// 调试相关 - MQTT伪透传
uint8_t debug_mode = 0;               // 0:关闭调试, 1:开启调试
char debug_buffer[1024];              // 调试消息缓冲区
uint16_t debug_buffer_index = 0;      // 缓冲区索引
SemaphoreHandle_t debug_mutex = NULL; // 调试缓冲区互斥锁

char *Reset = "AT+MREBOOT=0\r\n";

// Base64解码表
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 全局OTA控制结构体
static OtaControl g_ota_ctrl = {OTA_STATE_IDLE, 0, 0, 0, 0, NULL, 0, {0}};
static uint8_t g_ota_payload_pending = 0;
static uint8_t g_ota_parse_fail_count = 0;

static size_t ota_strnlen(const char *s, size_t maxlen)
{
    size_t len = 0;
    if (s == NULL)
    {
        return 0;
    }
    while (len < maxlen && s[len] != '\0')
    {
        len++;
    }
    return len;
}

/**
 * @brief 获取当前OTA状态
 * @return OTA状态
 */
OtaState OTA_GetState(void)
{
    return g_ota_ctrl.state;
}

/**
 * @brief 擦除整个OTA临时存储区(252K)
 * @return 1=成功, 0=失败
 */
uint8_t OTA_EraseTemplateArea(void)
{
    uint32_t addr = OTA_WRITE_ADDR;     // 0x08041000
    uint32_t end_addr = 0x08080000;     // TEMPLATE区域结束地址(开区间)
    uint32_t sector_count = 0;

    printf("[OTA] 开始擦除OTA存储区 (0x%08lX - 0x%08lX)...\r\n", addr, end_addr - 1);

    flash_unlock();

    while (addr < end_addr)
    {
        flash_sector_erase(addr);
        addr += 2048;  // 2K扇区
        sector_count++;

        // 每擦除10个扇区喂狗并打印进度
        if (sector_count % 10 == 0)
        {
            wdt_counter_reload();  // 喂狗
            printf("[OTA] 已擦除 %lu 个扇区...\r\n", sector_count);
        }
    }

    flash_lock();

    printf("[OTA] 擦除完成，共擦除 %lu 个扇区 (%lu KB)\r\n", sector_count, sector_count * 2);
    return 1;
}

/**
 * @brief 计算Flash区域的校验和（16位累加和）
 * @param start_addr 起始地址
 * @param length 数据长度（支持最大252KB）
 * @return 16位校验和
 */
uint16_t OTA_CalculateCRC16(uint32_t start_addr, uint32_t length)
{
    return OTA_Checksum16((uint8_t *)start_addr, length);
}

/**
 * @brief 检查是否收到OTA升级指令
 * @param recv_buf 接收到的数据缓冲区
 * @return 1=收到升级指令, 0=非升级指令
 * @note 新格式: ML307OTA_{设备ID}_{CRC16}_{固件大小}
 *       例如: ML307OTA_CYJ_2512121H1001B_A1B2_65536
 */
uint8_t OTA_CheckStartCommand(char *recv_buf)
{
    char ota_cmd[100];
    snprintf(ota_cmd, sizeof(ota_cmd), "ML307OTA_%s_", g_CommSettingConfig.IDSET);

    char *pos = strstr(recv_buf, ota_cmd);
    if (pos != NULL)
    {
        printf("[OTA] 收到升级指令\r\n");

        // 解析CRC16和固件大小
        char *crc_start = pos + strlen(ota_cmd);
        char *size_start = strchr(crc_start, '_');
        if (size_start != NULL)
        {
            // 临时复制CRC16字符串
            char crc_str[16];
            int crc_len = size_start - crc_start;
            if (crc_len > 0 && crc_len < 16)
            {
                strncpy(crc_str, crc_start, crc_len);
                crc_str[crc_len] = '\0';

                size_start++;  // 跳过'_'

                g_ota_ctrl.file_checksum = strtoul(crc_str, NULL, 16);
                g_ota_ctrl.total_size = strtoul(size_start, NULL, 10);

                // 校验解析结果有效性
                if (g_ota_ctrl.total_size == 0) {
                    printf("[OTA] 解析失败: 固件大小为0\r\n");
                    g_ota_ctrl.file_checksum = 0;
                    return 0;
                }

                printf("[OTA] 期望CRC16=0x%04lX, 固件大小=%lu字节\r\n",
                       g_ota_ctrl.file_checksum, g_ota_ctrl.total_size);
                return 1;
            }
        }
        // 解析失败，清零并返回错误
        printf("[OTA] 解析失败: 命令格式错误\r\n");
        g_ota_ctrl.file_checksum = 0;
        g_ota_ctrl.total_size = 0;
        return 0;
    }
    return 0;
}

/**
 * @brief OTA主处理函数
 * @param Buf 数据缓冲区
 * @param usart_x 串口指针
 */
void OTA_Process(char *Buf, usart_type *usart_x)
{
    // 检查是否收到OTA升级指令
    if (g_ota_ctrl.state == OTA_STATE_IDLE && OTA_CheckStartCommand(Buf))
    {
        g_ota_ctrl.state = OTA_STATE_INIT;
        printf("[OTA] 开始OTA初始化...\r\n");
        memset(Buf, 0, 512); // 清空缓冲区
        g_ota_payload_pending = 0;
        g_ota_parse_fail_count = 0;
    }

    // 状态机处理
    while (1)
    {
        switch (g_ota_ctrl.state)
        {
    case OTA_STATE_INIT:
    {
        // 1. 擦除整个OTA存储区（任务挂起与电机停止由任务2处理）
        OTA_EraseTemplateArea();

        // 2. 订阅CYJOTA主题
        printf("[OTA] 订阅CYJOTA主题...\r\n");
        char *cmd_sub = "AT+MQTTSUB=0,\"CYJOTA\",0\r\n";
        SendData(usart_x, (char *)cmd_sub, strlen(cmd_sub));
        // 简单延时等待
        OTA_DELAY_MS(1000);

        // 3. 擦除完成后发送ACK_0_OK
        char message[50];
        char Sendmessage[200];
        sprintf(message, "ACK_0_OK");
        sprintf(Sendmessage, "AT+MQTTPUB=0,\"CYJOTA\",0,0,0,%d,\"%s\"\r\n", strlen(message), message);
        printf("[OTA] 发送ACK: %s", Sendmessage);
        SendData(usart_x, Sendmessage, strlen(Sendmessage));

        // 4. 初始化状态（total_size和file_checksum已在CheckStartCommand中解析）
        g_ota_ctrl.state = OTA_STATE_WAIT_DATA;
        g_ota_ctrl.expected_packet_id = 1;
        g_ota_ctrl.buffer_pos = 0;
        g_ota_ctrl.received_size = 0;
        g_ota_ctrl.flash_addr = OTA_WRITE_ADDR;
        g_ota_ctrl.last_tick = xTaskGetTickCount();
        g_ota_ctrl.retry_count = 0;
        g_ota_payload_pending = 0;
        g_ota_parse_fail_count = 0;
        printf("[OTA] 初始化完成，等待数据包...\r\n");
        memset(Buf, 0, 512);
        break;
    }

    case OTA_STATE_WAIT_DATA:
        // 检查超时
        if ((xTaskGetTickCount() - g_ota_ctrl.last_tick) > pdMS_TO_TICKS(OTA_TIMEOUT_MS))
        {
            printf("[OTA] 等待数据超时\r\n");
            if (g_ota_ctrl.retry_count < OTA_MAX_RETRY_COUNT)
            {
                g_ota_ctrl.retry_count++;
                g_ota_ctrl.last_tick = xTaskGetTickCount();
                printf("[OTA] 重试 %lu/%lu\r\n", g_ota_ctrl.retry_count, OTA_MAX_RETRY_COUNT);
                // 请求重发上一个包
                OTA_SendAck(g_ota_ctrl.expected_packet_id - 1, 1, usart_x);
            }
            else
            {
                printf("[OTA] 超过最大重试次数，升级失败\r\n");
                g_ota_ctrl.state = OTA_STATE_ERROR;
            }
            break;
        }

        // 直接处理串口收到的MQTT数据
        if (g_ota_payload_pending ||
            strstr(Buf, "RXCV:") != NULL ||
            strstr(Buf, "URC: \"publish\"") != NULL)
        {
            g_ota_ctrl.state = OTA_STATE_PROCESS_PACKET;
            g_ota_ctrl.last_tick = xTaskGetTickCount();
            continue;
        }
        // 任务已挂起，使用简单延时
        OTA_DELAY_MS(100);
        break;

    case OTA_STATE_PROCESS_PACKET:
    {
        // 解析Base64数据并处理数据包
        uint8_t parse_result = OTA_ParseAndProcessData(Buf, &g_ota_ctrl);
        if (parse_result == 1)
        {
            // 清空缓冲区，准备接收下一个包
            memset(Buf, 0, 512);
            g_ota_ctrl.state = OTA_STATE_WAIT_DATA;
            g_ota_ctrl.retry_count = 0; // 重置重试计数
            g_ota_parse_fail_count = 0;

            // 检查是否完成
            if (g_ota_ctrl.expected_packet_id > g_ota_ctrl.total_packets)
            {
                g_ota_ctrl.state = OTA_STATE_VERIFY;
            }
        }
        else if (parse_result == 2)
        {
            g_ota_ctrl.state = OTA_STATE_WAIT_DATA;
        }
        else
        {
            // 数据包处理失败，请求重传
            g_ota_parse_fail_count++;
            if (g_ota_parse_fail_count >= 10)
            {
                printf("[OTA] 连续重传失败，升级终止\r\n");
                OTA_Error(usart_x);
                break;
            }
            printf("[OTA] 数据包处理失败，请求重传(%d/10)\r\n", g_ota_parse_fail_count);
            OTA_SendAck(g_ota_ctrl.expected_packet_id - 1, 1, usart_x);
            memset(Buf, 0, 512);
            g_ota_ctrl.state = OTA_STATE_WAIT_DATA;
            g_ota_ctrl.last_tick = xTaskGetTickCount();
        }
        break;
    }

    case OTA_STATE_VERIFY:
        // 验证完整性
        if (OTA_VerifyFirmware(&g_ota_ctrl))
        {
            printf("[OTA] 固件验证成功\r\n");
            OTA_Complete(usart_x);
        }
        else
        {
            printf("[OTA] 固件验证失败\r\n");
            OTA_Error(usart_x);
        }
        break;

    case OTA_STATE_COMPLETE:
        // 已经完成，保持状态
        break;

    case OTA_STATE_ERROR:
        printf("[OTA] 升级失败，清理资源\r\n");
        OTA_Error(usart_x);
        break;

    default:
        break;
        }
        break;
    }
}

/**
 * @brief 发送ACK确认
 * @param packet_id 包序号
 * @param status 状态 (0=OK, 1=错误/请求重传)
 * @param usart_x 串口指针
 */
void OTA_SendAck(uint16_t packet_id, uint8_t status, usart_type *usart_x)
{
    char message[50];
    char sendmsg[200];

    if (packet_id == 65535)
    {
        // 完成或错误标志
        sprintf(message, "ACK_65535_%s", status == 0 ? "OK" : "ERROR");
    }
    else if (status == 0)
    {
        sprintf(message, "ACK_%u_OK", packet_id);
    }
    else
    {
        sprintf(message, "ACK_%u_ERROR", packet_id);
    }

    sprintf(sendmsg, "AT+MQTTPUB=0,\"CYJOTA\",0,0,0,%d,\"%s\"\r\n",strlen(message), message);

    // 使用全局的USART6发送
    SendData(USART6, sendmsg, strlen(sendmsg));
    printf("[OTA] 发送ACK: %s\r\n", message);

    // 【关键修改】添加短延时，确保AT命令被4G模组正确处理后再继续
    // 避免后续操作（如Flash写入）干扰ACK发送
    OTA_DELAY_MS(50);
}

static uint8_t ota_parse_mqttrx_line(char *line, uint32_t *payload_len, char **payload_start)
{
    char *line_end = strpbrk(line, "\r\n");
    if (line_end == NULL)
    {
        line_end = line + strlen(line);
    }

    char *p = strchr(line, ':');
    if (p == NULL || p >= line_end)
    {
        return 0;
    }
    p++;
    while (p < line_end && *p == ' ')
    {
        p++;
    }

    char *c1 = strchr(p, ',');
    if (c1 == NULL || c1 >= line_end)
    {
        return 0;
    }
    char *c2 = strchr(c1 + 1, ',');
    if (c2 == NULL || c2 >= line_end)
    {
        return 0;
    }
    char *c3 = strchr(c2 + 1, ',');
    if (c3 == NULL || c3 >= line_end)
    {
        return 0;
    }
    char *c4 = strchr(c3 + 1, ',');
    if (c4 == NULL || c4 >= line_end)
    {
        return 0;
    }

    uint32_t len = 0;
    for (char *s = c3 + 1; s < c4; s++)
    {
        if (*s >= '0' && *s <= '9')
        {
            len = len * 10u + (uint32_t)(*s - '0');
        }
    }

    char *start = c4 + 1;
    if (start >= line_end)
    {
        return 0;
    }
    if (*start == '"')
    {
        start++;
        char *quote_end = strchr(start, '"');
        if (quote_end == NULL || quote_end > line_end)
        {
            return 0;
        }
        line_end = quote_end;
    }

    *payload_len = len;
    *payload_start = start;
    if ((uint32_t)(line_end - start) < len)
    {
        return 2;
    }
    return 1;
}

static uint8_t ota_parse_mqtturc_publish_line(char *line, uint32_t *payload_len, char **payload_start)
{
    char *line_end = strpbrk(line, "\r\n");
    if (line_end == NULL)
    {
        line_end = line + strlen(line);
    }

    char *p = strchr(line, ':');
    if (p == NULL || p >= line_end)
    {
        return 0;
    }
    p++;
    while (p < line_end && *p == ' ')
    {
        p++;
    }
    if (line_end - p < 9 || strncmp(p, "\"publish\"", 9) != 0)
    {
        return 0;
    }

    char *c1 = strchr(p, ',');
    if (c1 == NULL || c1 >= line_end)
    {
        return 0;
    }
    char *c2 = strchr(c1 + 1, ',');
    if (c2 == NULL || c2 >= line_end)
    {
        return 0;
    }
    char *c3 = strchr(c2 + 1, ',');
    if (c3 == NULL || c3 >= line_end)
    {
        return 0;
    }

    char *topic_start = c3 + 1;
    if (topic_start >= line_end)
    {
        return 0;
    }
    char *c4 = NULL;
    if (*topic_start == '"')
    {
        char *topic_end = strchr(topic_start + 1, '"');
        if (topic_end == NULL || topic_end >= line_end)
        {
            return 0;
        }
        c4 = strchr(topic_end, ',');
    }
    else
    {
        c4 = strchr(topic_start, ',');
    }
    if (c4 == NULL || c4 >= line_end)
    {
        return 0;
    }

    uint32_t len = 0;
    char *s = c4 + 1;
    while (s < line_end && *s >= '0' && *s <= '9')
    {
        len = len * 10u + (uint32_t)(*s - '0');
        s++;
    }
    char *c5 = strchr(s, ',');
    if (c5 == NULL || c5 >= line_end)
    {
        return 0;
    }
    char *c6 = strchr(c5 + 1, ',');
    if (c6 == NULL || c6 >= line_end)
    {
        return 0;
    }

    char *start = c6 + 1;
    if (start >= line_end)
    {
        return 0;
    }
    if (*start == '"')
    {
        start++;
        char *quote_end = strchr(start, '"');
        if (quote_end == NULL || quote_end > line_end)
        {
            return 0;
        }
        line_end = quote_end;
    }

    *payload_len = len;
    *payload_start = start;
    if ((uint32_t)(line_end - start) < len)
    {
        return 2;
    }
    return 1;
}

static uint8_t ota_parse_mqtt_payload(char *buf, uint32_t *payload_len, char **payload_start, uint8_t *pending)
{
    char *scan = buf;
    *pending = 0;
    while ((scan = strstr(scan, "URC: \"publish\"")) != NULL)
    {
        uint8_t ret = ota_parse_mqtturc_publish_line(scan, payload_len, payload_start);
        if (ret == 1)
        {
            if (*payload_len >= OTA_BASE64_SIZE)
            {
                return 1;
            }
        }
        else if (ret == 2)
        {
            if (*payload_len >= OTA_BASE64_SIZE)
            {
                *pending = 1;
                return 0;
            }
        }
        char *line_end = strpbrk(scan, "\r\n");
        if (line_end == NULL)
        {
            break;
        }
        scan = line_end;
    }

    scan = buf;
    while ((scan = strstr(scan, "RXCV:")) != NULL)
    {
        uint8_t ret = ota_parse_mqttrx_line(scan, payload_len, payload_start);
        if (ret == 1)
        {
            return 1;
        }
        if (ret == 2)
        {
            *pending = 1;
            return 0;
        }
        char *line_end = strpbrk(scan, "\r\n");
        if (line_end == NULL)
        {
            break;
        }
        scan = line_end;
    }

    return 0;
}

static uint8_t ota_collect_mqtt_payload(char *buf, uint16_t buf_size, uint32_t *payload_len, char **payload_start)
{
    static char cache[512];
    static uint16_t cache_len = 0;

    size_t add_len = ota_strnlen(buf, buf_size);
    if (add_len == 0)
    {
        return 0;
    }

    if (cache_len + add_len >= sizeof(cache))
    {
        cache_len = 0;
        cache[0] = '\0';
    }

    memcpy(cache + cache_len, buf, add_len);
    cache_len += (uint16_t)add_len;
    cache[cache_len] = '\0';

    uint8_t pending = 0;
    if (ota_parse_mqtt_payload(cache, payload_len, payload_start, &pending))
    {
        cache_len = 0;
        cache[0] = '\0';
        g_ota_payload_pending = 0;
        return 1;
    }

    if (pending)
    {
        g_ota_payload_pending = 1;
        return 2;
    }

    cache_len = 0;
    cache[0] = '\0';
    g_ota_payload_pending = 0;
    return 0;
}

uint8_t OTA_HasPendingPayload(void)
{
    return g_ota_payload_pending;
}

/**
 * @brief Base64解码函数
 * @param src Base64编码的字符串
 * @param dst 解码后的二进制数据缓冲区
 * @param dst_len 缓冲区最大长度
 * @return 解码后的数据长度
 */
static int OTA_Base64DecodeLen(const char *src, int src_len, uint8_t *dst, int dst_len)
{
    int i, j, k;
    uint32_t val = 0;
    int decoded_len = 0;

    for (i = 0; i < src_len; i += 4)
    {
        val = 0;
        for (j = 0; j < 4; j++)
        {
            if (i + j < src_len)
            {
                char c = src[i + j];
                if (c == '=')
                {
                    val = val << 6;
                }
                else
                {
                    char *p = strchr(base64_table, c);
                    if (p)
                    {
                        val = (val << 6) | (p - base64_table);
                    }
                    else
                    {
                        return -1; // 无效字符
                    }
                }
            }
            else
            {
                val = val << 6;
            }
        }

        // 生成3字节输出
        for (k = 2; k >= 0; k--)
        {
            if (decoded_len < dst_len)
            {
                dst[decoded_len] = (val >> (8 * k)) & 0xFF;
                decoded_len++;
            }
            else
            {
                // 缓冲区溢出，返回错误
                return -2;
            }
        }
    }

    return decoded_len;
}

int OTA_Base64Decode(const char *src, uint8_t *dst, int dst_len)
{
    return OTA_Base64DecodeLen(src, (int)strlen(src), dst, dst_len);
}

/**
 * @brief 解析并处理Base64数据
 * @param buf 接收缓冲区
 * @param ota_ctrl OTA控制结构
 * @return 1=处理成功, 2=等待更多数据, 0=处理失败
 */
uint8_t OTA_ParseAndProcessData(char *buf, OtaControl *ota_ctrl)
{
    uint32_t base64_len = 0;
    char *data_start = NULL;
    uint8_t collect_state = ota_collect_mqtt_payload(buf, 512, &base64_len, &data_start);
    if (collect_state == 0)
    {
        return 2;
    }
    if (collect_state == 2)
    {
        return 2;
    }

    if (base64_len % 4 != 0)
    {
        printf("[OTA] Base64数据长度错误: %lu\r\n", base64_len);
        return 0;
    }

    printf("[OTA] 收到Base64数据，长度 %lu\r\n", base64_len);

    // Base64解码
    uint8_t decoded_data[2048]; // 最大支持2K数据
    int decoded_len = OTA_Base64DecodeLen(data_start, (int)base64_len, decoded_data, sizeof(decoded_data));

    if (decoded_len <= 0)
    {
        printf("[OTA] Base64解码失败\r\n");
        return 0;
    }

    printf("[OTA] Base64解码成功，解码长度 %d\r\n", decoded_len);

    // 解析数据包（可能包含多个包）
    int offset = 0;
    uint8_t packet_processed = 0;

    while (offset + OTA_TOTAL_PACKET_SIZE <= decoded_len)
    {
        // 查找包头
        uint16_t header = *((uint16_t *)(decoded_data + offset));

        // 处理小端序
        if (header == 0x55AA)
        {
            header = 0xAA55; // 转换为正确的字节序
        }

        if (header != OTA_PACKET_HEADER)
        {
            printf("[OTA] 包头错误: 0x%04X (位置: %d)\r\n", header, offset);
            offset++;
            continue; // 跳过这个字节，继续查找
        }

        // 解析数据包
        OtaPacket *packet = (OtaPacket *)(decoded_data + offset);

        // 验证包序号
        if (packet->packet_id != ota_ctrl->expected_packet_id)
        {
            // 检查是否是重复包（服务器重发）
            if (packet->packet_id == ota_ctrl->expected_packet_id - 1 && ota_ctrl->expected_packet_id > 1)
            {
                printf("[OTA] 收到重复包 %d，重发ACK\r\n", packet->packet_id);
                OTA_SendAck(packet->packet_id, 0, NULL);
                return 2;  // 返回2表示等待更多数据，不是错误
            }
            printf("[OTA] 包序号错误: 期望 %lu, 收到 %d\r\n",
                   ota_ctrl->expected_packet_id, packet->packet_id);
            return 0;
        }

        // 保存总包数（total_size已在OTA_CheckStartCommand中解析，不要覆盖）
        if (ota_ctrl->total_packets == 0)
        {
            ota_ctrl->total_packets = packet->total_packets;
            printf("[OTA] 总包数 %lu, 期望总大小 %lu字节\r\n",
                   ota_ctrl->total_packets, ota_ctrl->total_size);
        }

        // 验证数据长度
        if (packet->data_len > OTA_PACKET_SIZE)
        {
            printf("[OTA] 数据包长度超限: %d > %d\r\n", packet->data_len, OTA_PACKET_SIZE);
            return 0;
        }

        // 计算并验证校验和
        uint8_t checksum = 0;
        for (int i = 0; i < packet->data_len; i++)
        {
            checksum = (checksum + packet->data[i]) & 0xFF;
        }

        if (checksum != packet->checksum)
        {
            printf("[OTA] 校验和错误: 期望 0x%02X, 实际 0x%02X\r\n",
                   packet->checksum, checksum);
            return 0;
        }

        // 将数据添加到缓冲区
        if (ota_ctrl->buffer_pos + packet->data_len <= OTA_BUFFER_SIZE)
        {
            memcpy(&ota_ctrl->buffer[ota_ctrl->buffer_pos],
                   packet->data, packet->data_len);
            ota_ctrl->buffer_pos += packet->data_len;
            ota_ctrl->received_size += packet->data_len;
            printf("[OTA] 数据包%d 已添加到缓冲区(大小: %d, 缓冲区 %d/2048)\r\n",
                   packet->packet_id, packet->data_len, ota_ctrl->buffer_pos);
        }
        else
        {
            printf("[OTA] 缓冲区溢出错误\r\n");
            return 0;
        }

        // 【关键修改】先发送ACK，再写Flash，避免Flash写入耗时导致ACK延迟
        OTA_SendAck(packet->packet_id, 0, NULL);

        // 检查是否需要写入Flash（ACK已发送，此时写Flash不会影响ACK时序）
        if (ota_ctrl->buffer_pos == OTA_BUFFER_SIZE ||
            packet->packet_id == ota_ctrl->total_packets)
        {
            OTA_WriteBufferToFlash(ota_ctrl);
        }

        // 更新期望的包序号
        ota_ctrl->expected_packet_id++;

        offset += OTA_TOTAL_PACKET_SIZE;
        packet_processed = 1;
    }

    return packet_processed;
}

/**
 * @brief 将缓冲区数据写入Flash
 * @param ota_ctrl OTA控制结构
 */
void OTA_WriteBufferToFlash(OtaControl *ota_ctrl)
{
    // 使用 received_size 计算正确的写入地址
    // received_size 在写入前已经累加了本次数据
    // 所以需要减去当前缓冲区的数据量来得到正确的起始地址
    uint32_t write_offset = ota_ctrl->received_size - ota_ctrl->buffer_pos;
    uint32_t write_addr = ota_ctrl->flash_addr + write_offset;

    printf("[OTA] 写入Flash: 地址=0x%08lX, 大小=%d字节\r\n",
           write_addr, ota_ctrl->buffer_pos);

    // 准备数据（未满的部分用0xFF填充）
    uint8_t write_buffer[OTA_BUFFER_SIZE];
    memset(write_buffer, 0xFF, OTA_BUFFER_SIZE);
    memcpy(write_buffer, ota_ctrl->buffer, ota_ctrl->buffer_pos);

    // 写入Flash（扇区已预先擦除，无需再擦除）
    // 按半字写入
    flash_unlock();
    for (uint16_t i = 0; i < ota_ctrl->buffer_pos; i += 2)
    {
        uint16_t data = (write_buffer[i + 1] << 8) | write_buffer[i];
        flash_halfword_program(write_addr + i, data);
    }
    flash_lock();

    // 喂狗
    wdt_counter_reload();

    printf("[OTA] 写入完成\r\n");

    // 重置缓冲区
    ota_ctrl->buffer_pos = 0;
    memset(ota_ctrl->buffer, 0, OTA_BUFFER_SIZE);
}

/**
 * @brief 验证固件完整性
 * @param ota_ctrl OTA控制结构
 * @return 1=验证成功, 0=验证失败
 */
uint8_t OTA_VerifyFirmware(OtaControl *ota_ctrl)
{
    printf("[OTA] 固件验证: 接收大小=%lu字节, 期望大小=%lu字节\r\n",
           ota_ctrl->received_size, ota_ctrl->total_size);

    // 1. 验证大小
    if (ota_ctrl->received_size != ota_ctrl->total_size)
    {
        printf("[OTA] 固件大小不匹配\r\n");
        return 0;
    }

    // 2. 计算并验证CRC16
    uint16_t calc_crc = OTA_CalculateCRC16(ota_ctrl->flash_addr, ota_ctrl->total_size);
    printf("[OTA] CRC16校验: 计算值=0x%04X, 期望值=0x%04X\r\n",
           calc_crc, (uint16_t)ota_ctrl->file_checksum);

    if (calc_crc != (uint16_t)ota_ctrl->file_checksum)
    {
        printf("[OTA] CRC16校验失败\r\n");
        return 0;
    }

    printf("[OTA] 固件验证通过\r\n");
    // 发送完成ACK
    OTA_SendAck(65535, 0, NULL);

    return 1;
}

/**
 * @brief OTA升级完成处理
 * @param usart_x 串口指针
 */
void OTA_Complete(usart_type *usart_x)
{
    printf("[OTA] 升级完成，进行后续处理...\r\n");

    g_ota_ctrl.state = OTA_STATE_COMPLETE;

    // 读取写入前的标志值
    uint32_t flag_before = *(uint32_t *)OTA_UPGRADE_FLAG_ADDR;
    printf("[OTA] 写入前标志值: 0x%08lX (地址: 0x%08X)\r\n", flag_before, OTA_UPGRADE_FLAG_ADDR);

    // 设置升级标志
    printf("[OTA] 正在写入升级标志...\r\n");
    taskENTER_CRITICAL();
    write_upgrade_flag();
    taskEXIT_CRITICAL();

    // 读取写入后的标志值
    uint32_t flag_after = *(uint32_t *)OTA_UPGRADE_FLAG_ADDR;
    printf("[OTA] 写入后标志值: 0x%08lX (期望: 0x%08X)\r\n", flag_after, OTA_UPGRADE_FLAG);

    if (flag_after == OTA_UPGRADE_FLAG)
    {
        printf("[OTA] 升级标志写入成功!\r\n");
    }
    else
    {
        printf("[OTA] 警告: 升级标志写入失败!\r\n");
    }

    // 发送完成ACK
    OTA_SendAck(65535, 0, usart_x);

    // 简单延时后重启（不使用vTaskDelay，避免调度器问题）
    printf("[OTA] 准备重启MCU，Bootloader将执行固件搬运...\r\n");
    delay_ms(1000);

    // 重启MCU
    NVIC_SystemReset();
}

/**
 * @brief OTA错误处理
 * @param usart_x 串口指针
 */
void OTA_Error(usart_type *usart_x)
{
    printf("[OTA] 升级失败，准备重启...\r\n");
    g_ota_ctrl.state = OTA_STATE_ERROR;

    // 发送错误ACK（尝试发送，不等待响应）
    OTA_SendAck(65535, 1, usart_x);

    // 简单延时后直接重启（不使用vTaskDelay，避免调度器问题）
    g_ota_ctrl.state = OTA_STATE_IDLE;
    delay_ms(500);

    // 强制重启
    NVIC_SystemReset();
}

//===================== IMEI获取函数 =====================

/**
 * @brief 验证IMEI格式是否有效（15位纯数字）
 * @param imei IMEI字符串
 * @return 1=有效, 0=无效
 */
static uint8_t IMEI_Validate(const char *imei)
{
    if (imei == NULL) return 0;
    size_t len = strlen(imei);
    if (len != 15) return 0;
    for (size_t i = 0; i < len; i++) {
        if (imei[i] < '0' || imei[i] > '9') return 0;
    }
    return 1;
}

/**
 * @brief 从AT+CGSN=1响应中解析IMEI号
 * @param response AT命令响应字符串
 * @param imei_out 输出缓冲区（至少16字节）
 * @return 1=解析成功, 0=解析失败
 * @note 响应格式: +CGSN: 869663071216324\r\n\r\nOK\r\n
 */
static uint8_t IMEI_ParseResponse(const char *response, char *imei_out)
{
    if (response == NULL || imei_out == NULL) return 0;
    const char *pos = strstr(response, "+CGSN:");
    if (pos == NULL) return 0;
    pos += 6;  // 跳过 "+CGSN:"
    while (*pos == ' ') pos++;  // 跳过空格
    size_t i = 0;
    while (i < 15 && pos[i] >= '0' && pos[i] <= '9') {
        imei_out[i] = pos[i];
        i++;
    }
    imei_out[i] = '\0';
    return (i == 15) ? 1 : 0;
}

/**
 * @brief 获取4G模块IMEI号并设置为设备IDSET
 * @param usart_x 串口指针（USART6）
 * @param Buf 接收缓冲区（UART6_Buf）
 * @return 1=成功, 0=失败
 * @note 首次获取成功后保存到KVDB，后续启动直接从KVDB读取
 */
uint8_t IMEI_GetAndSetIDSET(usart_type *usart_x, char *Buf)
{
    char imei[24] = {0};
    uint8_t retry_count = 0;
    const uint8_t MAX_RETRY = 3;

    // 检查KVDB中是否已有有效IMEI
    if (IMEI_Validate(g_CommSettingConfig.IDSET)) {
        printf("[IMEI] 已有有效IMEI: %s\r\n", g_CommSettingConfig.IDSET);
        return 1;
    }

    printf("[IMEI] 开始获取4G模块IMEI...\r\n");

    while (retry_count < MAX_RETRY) {
        memset(Buf, 0, 512);
        char *cmd = "AT+CGSN=1\r\n";
        SendData(usart_x, cmd, strlen(cmd));

        uint32_t notify_value;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_value,
                           pdMS_TO_TICKS(2000)) == pdTRUE) {
            if (IMEI_ParseResponse(Buf, imei) && IMEI_Validate(imei)) {
                printf("[IMEI] 获取成功: %s\r\n", imei);
                strncpy(g_CommSettingConfig.IDSET, imei,
                       sizeof(g_CommSettingConfig.IDSET) - 1);
                g_CommSettingConfig.IDSET[sizeof(g_CommSettingConfig.IDSET) - 1] = '\0';
                cfg_save_comm(&g_CommSettingConfig);
                printf("[IMEI] 已保存到KVDB\r\n");
                return 1;
            }
        }
        retry_count++;
        printf("[IMEI] 获取失败，重试 %d/%d\r\n", retry_count, MAX_RETRY);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    printf("[IMEI] 获取失败，保持原IDSET: %s\r\n", g_CommSettingConfig.IDSET);
    return 0;
}

/**
 * @brief MQTT初始化函数
 * @param Buf 缓冲区
 * @param usart_x 串口指针
 * @return 1=成功, 0=失败
 */
uint8_t MqttInit(char *Buf, usart_type *usart_x)
{
    if (TestATCommand(usart_x, Buf, "AT+CEREG?\r\n", "+CEREG: 0,1", 200, 500) == 0)
    {
        return 0;
    }

    timesyc(usart_x, Buf);

    char *check_disconnect = "AT+MQTTDISC=0\r\n";
    memset(Buf, 0, 512);
    SendData(usart_x, check_disconnect, strlen(check_disconnect));
    vTaskDelay(500);
    char connect_cmd[200];
    sprintf(connect_cmd, "AT+MQTTCONN=0,\"%s\",1883,\"%s\"\r\n",
            g_CommSettingConfig.IPSET, g_CommSettingConfig.IDSET);

    if (TestATCommand(usart_x, Buf, connect_cmd, "+MQTTURC:", 50, 1500) == 0)
    {
        return 0;
    }

    if (TestATCommand(usart_x, Buf, "AT+MQTTSTATE=0\r\n", "MQTTSTATE: 2", 30, 2000) == 0)
    {
        return 0;
    }

    if (TestATCommand(usart_x, Buf, "AT+MQTTSUB=0,\"CYJSET\",0\r\n", "MQTT", 30, 2000) == 0)
    {
        return 0;
    }

    return 1;
}

/**
 * @brief 重置4G模组
 * @param usart_x 串口指针
 */
void ResetModle(usart_type *usart_x)
{
    SendData(usart_x, Reset, strlen(Reset));
    //    vTaskDelay(6000);
}

/**
 * @brief 发送AT命令并检查响应
 * @param usart_x 串口指针
 * @param Buf 缓冲区
 * @param sendStr 发送的命令
 * @param recStr 期望的响应
 * @param retryNum 重试次数
 * @param time 超时时间
 * @return 1=成功, 0=失败
 */
int TestATCommand(usart_type *usart_x, char *Buf, char *sendStr, char *recStr, uint8_t retryNum, uint32_t time)
{
    uint32_t len;

    while (retryNum > 0)
    {
        memset(Buf, 0, 512);
        SendData(usart_x, sendStr, strlen(sendStr));
        vTaskDelay(time);
        if (xTaskNotifyWait(0x0, 0xffffffff, &len, pdMS_TO_TICKS(2000)) == pdPASS)
        {
            if (strstr(Buf, recStr) != NULL)
            {
                return 1;
            }
            else
            {
                xEventGroupSetBits(event_handle, (1 << 0));
                retryNum--;
                vTaskDelay(200);
            }
        }
        else
        {
            ResetModle(usart_x);
            return 0;
        }
    }

    return 0;
}

/**
 * @brief MQTT发送测试函数
 * @param Buf 缓冲区
 * @param usart_x 串口指针
 */
void MqttSend(char *Buf, usart_type *usart_x)
{
    char *str6 = "AT+MQTTSTATE=0\r\n";
    char *str_6 = "MQTTSTATE: 2";
    char message[200];
    char Sendmessage[200];

    // 检查MQTT连接状态
    if (TestATCommand(usart_x, Buf, str6, str_6, 20, 1000) == 0)
        MqttInit(Buf, usart_x);

    // 测试发送到三个主题

    // 1. 发送到CYJDAT主题
    sprintf(message, "{\"ID\":\"%s\",\"State\":%d}", g_CommSettingConfig.IDSET, 123);
    sprintf(Sendmessage, "AT+MQTTPUB=0,\"CYJDAT\",0,0,0,%d,\"%s\"\r\n", strlen(message), message);
    printf("[MqttTest] 发送到CYJDAT: %s", Sendmessage);
    SendData(usart_x, Sendmessage, strlen(Sendmessage));
    vTaskDelay(pdMS_TO_TICKS(500)); // 等待500ms

    //    // 2. 发送到CYJSET主题
    //    sprintf(message, "123456");
    //    sprintf(Sendmessage, "AT+MQTTPUB=0,\"CYJSET\",0,0,0,%d,\"%s\"\r\n", strlen(message), message);
    //    printf("[MqttTest] 发送到CYJSET: %s", Sendmessage);
    //    SendData(usart_x, Sendmessage, strlen(Sendmessage));
    //    vTaskDelay(pdMS_TO_TICKS(500));  // 等待500ms

    //    // 3. 发送到CYJOTA主题
    //    sprintf(message, "123456");
    //    sprintf(Sendmessage, "AT+MQTTPUB=0,\"CYJOTA\",0,0,0,%d,\"%s\"\r\n", strlen(message), message);
    //    printf("[MqttTest] 发送到CYJOTA: %s", Sendmessage);
    //    SendData(usart_x, Sendmessage, strlen(Sendmessage));
    //    vTaskDelay(pdMS_TO_TICKS(500));  // 等待500ms

    //    printf("[MqttTest] 测试消息发送完成\r\n\r\n");

    // 清空缓冲区
    memset(message, 0, sizeof(message));
    memset(Sendmessage, 0, sizeof(Sendmessage));
}

//===================== MQTT状态/设置上报函数 =====================

/**
 * @brief 辅助函数：发送MQTT消息到CYJDAT主题
 */
static void mqtt_publish_message(usart_type *usart_x, const char *message)
{
    if (usart_x == NULL || message == NULL)
    {
        return;
    }

    const char *payload = message;
    char payload_with_id[420];

    /* Ensure every CYJDAT payload contains the device ID (g_CommSettingConfig.IDSET)
       for backend DB storage. Existing payloads that already contain "i" are kept. */
    if (message[0] == '{' && strstr(message, "\"i\":") == NULL)
    {
        /* Insert "i" as the first field: {"i":"<ID>", ...} */
        snprintf(payload_with_id, sizeof(payload_with_id),
                 "{\"i\":\"%s\",%s", g_CommSettingConfig.IDSET, message + 1);
        payload = payload_with_id;
    }
    else if (message[0] != '{')
    {
        /* Non-JSON payloads are wrapped so they still carry the device ID. */
        snprintf(payload_with_id, sizeof(payload_with_id),
                 "{\"i\":\"%s\",\"m\":\"%s\"}", g_CommSettingConfig.IDSET, message);
        payload = payload_with_id;
    }

    char sendCmd[520];
    snprintf(sendCmd, sizeof(sendCmd),
             "AT+MQTTPUB=0,\"CYJDAT\",0,0,0,%d,\"%s\"\r\n",
             (int)strlen(payload), payload);
    SendData(usart_x, sendCmd, strlen(sendCmd));
    vTaskDelay(pdMS_TO_TICKS(300));
}

/* ===================== 最近事件上报 ===================== */

static void mqtt_send_recent_sampling(usart_type *usart_x, uint32_t now, uint32_t window_sec)
{
    uint32_t last_sent_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_SAMPLING];
    uint32_t max_sent_ts = last_sent_ts;
    uint8_t page = 0;
    uint8_t found_old = 0;

    while (!found_old)
    {
        SamplingStartRecord starts[CACHE_PER_PAGE] = {0};
        SamplingCompleteRecord completes[CACHE_PER_PAGE] = {0};
        uint8_t count = 0;
        if (!cache_query_sampling(page, CACHE_PER_PAGE, starts, completes, &count))
            break;
        for (uint8_t i = 0; i < count; i++)
        {
            uint32_t ts = completes[i].end_time;

            // 已经发送过，停止遍历
            if (ts <= last_sent_ts) {
                found_old = 1;
                break;
            }

            printf("[MQTT] samp ts=%u now=%u diff=%u\n", (unsigned)ts, (unsigned)now, (unsigned)(now - ts));
            char msg[260];
            snprintf(msg, sizeof(msg),
                     "{\"t\":31,\"et\":\"samp\",\"id\":\"%.18s\",\"b\":%u,\"mode\":%u,\"seq\":%lu,"
                     "\"st\":%u,\"end\":%u,\"vol\":%u,\"res\":%u,\"err\":%u}",
                     starts[i].sample_id,
                     (unsigned)starts[i].bucket_id,
                     (unsigned)starts[i].sampling_mode,
                     (unsigned long)starts[i].sequence,
                     (unsigned)starts[i].start_time,
                     (unsigned)completes[i].end_time,
                     (unsigned)completes[i].actual_volume,
                     (unsigned)completes[i].result,
                     (unsigned)completes[i].error_code);
            mqtt_publish_message(usart_x, msg);

            // 喂狗，避免大量消息发送时看门狗超时
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (ts > max_sent_ts)
                max_sent_ts = ts;
        }
        if (count < CACHE_PER_PAGE)
            break;
        page++;
    }

    // 更新发送时间戳
    g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_SAMPLING] = max_sent_ts;
}

static void mqtt_send_recent_delivery(usart_type *usart_x, uint32_t now, uint32_t window_sec)
{
    uint32_t last_sent_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DELIVERY];
    uint32_t max_sent_ts = last_sent_ts;
    uint8_t page = 0;
    uint8_t found_old = 0;

    while (!found_old)
    {
        DeliveryStartRecord starts[CACHE_PER_PAGE] = {0};
        DeliveryCompleteRecord completes[CACHE_PER_PAGE] = {0};
        uint8_t count = 0;
        if (!cache_query_delivery(page, CACHE_PER_PAGE, starts, completes, &count))
            break;
        for (uint8_t i = 0; i < count; i++)
        {
            uint32_t ts = completes[i].end_time;
            if (ts <= last_sent_ts) {
                found_old = 1;
                break;
            }
            printf("[MQTT] deliv ts=%u now=%u diff=%u\n", (unsigned)ts, (unsigned)now, (unsigned)(now - ts));
            char msg[240];
            snprintf(msg, sizeof(msg),
                     "{\"t\":31,\"et\":\"deliv\",\"id\":\"%.18s\",\"b\":%u,\"mode\":%u,"
                     "\"st\":%u,\"end\":%u,\"vol\":%u,\"res\":%u,\"err\":%u}",
                     starts[i].sample_id,
                     (unsigned)starts[i].bucket_id,
                     (unsigned)starts[i].delivery_mode,
                     (unsigned)starts[i].start_time,
                     (unsigned)completes[i].end_time,
                     (unsigned)completes[i].delivery_volume,
                     (unsigned)completes[i].result,
                     (unsigned)completes[i].error_code);
            mqtt_publish_message(usart_x, msg);

            // 喂狗，避免大量消息发送时看门狗超时
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (ts > max_sent_ts)
                max_sent_ts = ts;
        }
        if (count < CACHE_PER_PAGE)
            break;
        page++;
    }
    g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DELIVERY] = max_sent_ts;
}

static void mqtt_send_recent_retain(usart_type *usart_x, uint32_t now, uint32_t window_sec)
{
    uint32_t last_sent_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_RETAIN];
    uint32_t max_sent_ts = last_sent_ts;
    uint8_t page = 0;
    uint8_t found_old = 0;

    while (!found_old)
    {
        RetainLogRecord records[CACHE_PER_PAGE] = {0};
        uint8_t count = 0;
        if (!cache_query_retain(page, CACHE_PER_PAGE, records, &count))
            break;
        for (uint8_t i = 0; i < count; i++)
        {
            uint32_t ts = records[i].end_time;
            if (ts <= last_sent_ts) {
                found_old = 1;
                break;
            }
            printf("[MQTT] retain ts=%u now=%u diff=%u\n", (unsigned)ts, (unsigned)now, (unsigned)(now - ts));
            char msg[260];
            snprintf(msg, sizeof(msg),
                     "{\"t\":31,\"et\":\"retain\",\"id\":\"%.18s\",\"mode\":%u,\"reason\":%u,"
                     "\"st\":%u,\"end\":%u,\"dt\":%u,\"vol\":%u,\"bottle\":%u,"
                     "\"trig\":%.1f,\"res\":%u,\"err\":%u,\"acid\":%u}",
                     records[i].sample_id,
                     (unsigned)records[i].retain_mode,
                     (unsigned)records[i].retain_reason,
                     (unsigned)records[i].start_time,
                     (unsigned)records[i].end_time,
                     (unsigned)records[i].delivery_time,
                     (unsigned)records[i].retain_volume,
                     (unsigned)records[i].bottle_number,
                     records[i].trigger_value,
                     (unsigned)records[i].result,
                     (unsigned)records[i].error_code,
                     (unsigned)records[i].acid_added);
            mqtt_publish_message(usart_x, msg);

            // 喂狗，避免大量消息发送时看门狗超时
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (ts > max_sent_ts)
                max_sent_ts = ts;
        }
        if (count < CACHE_PER_PAGE)
            break;
        page++;
    }
    g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_RETAIN] = max_sent_ts;
}

static void mqtt_send_recent_power(usart_type *usart_x, uint32_t now, uint32_t window_sec)
{
    uint32_t last_sent_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_POWER];
    uint32_t max_sent_ts = last_sent_ts;
    uint8_t page = 0;
    uint8_t found_old = 0;

    while (!found_old)
    {
        PowerEventCache_t events[CACHE_PER_PAGE] = {0};
        uint8_t count = 0;
        if (!cache_query_power(page, CACHE_PER_PAGE, events, &count))
            break;
        for (uint8_t i = 0; i < count; i++)
        {
            uint32_t ts = events[i].timestamp;
            if (ts <= last_sent_ts) {
                found_old = 1;
                break;
            }
            printf("[MQTT] power ts=%u now=%u diff=%u\n", (unsigned)ts, (unsigned)now, (unsigned)(now - ts));
            char msg[140];
            snprintf(msg, sizeof(msg),
                     "{\"t\":31,\"et\":\"power\",\"ev\":%u,\"ts\":%u}",
                     (unsigned)events[i].event_type,
                     (unsigned)events[i].timestamp);
            mqtt_publish_message(usart_x, msg);

            // 喂狗，避免大量消息发送时看门狗超时
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (ts > max_sent_ts)
                max_sent_ts = ts;
        }
        if (count < CACHE_PER_PAGE)
            break;
        page++;
    }
    g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_POWER] = max_sent_ts;
}

static void mqtt_send_recent_door(usart_type *usart_x, uint32_t now, uint32_t window_sec)
{
    uint32_t last_sent_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DOOR];
    uint32_t max_sent_ts = last_sent_ts;
    uint8_t page = 0;
    uint8_t found_old = 0;

    while (!found_old)
    {
        DoorEventCache_t events[CACHE_PER_PAGE] = {0};
        uint8_t count = 0;
        if (!cache_query_door(page, CACHE_PER_PAGE, events, &count))
            break;
        for (uint8_t i = 0; i < count; i++)
        {
            uint32_t ts = events[i].timestamp;
            if (ts <= last_sent_ts) {
                found_old = 1;
                break;
            }
            printf("[MQTT] door ts=%u now=%u diff=%u\n", (unsigned)ts, (unsigned)now, (unsigned)(now - ts));
            char msg[140];
            snprintf(msg, sizeof(msg),
                     "{\"t\":31,\"et\":\"door\",\"ev\":%u,\"ts\":%u}",
                     (unsigned)events[i].event_type,
                     (unsigned)events[i].timestamp);
            mqtt_publish_message(usart_x, msg);

            // 喂狗，避免大量消息发送时看门狗超时
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (ts > max_sent_ts)
                max_sent_ts = ts;
        }
        if (count < CACHE_PER_PAGE)
            break;
        page++;
    }
    g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DOOR] = max_sent_ts;
}

void MqttSendRecentEvents10m(usart_type *usart_x)
{
    const uint32_t window_sec = 600; // 10分钟
    uint32_t now = rtc_counter_get();
    mqtt_send_recent_sampling(usart_x, now, window_sec);
    mqtt_send_recent_delivery(usart_x, now, window_sec);
    mqtt_send_recent_retain(usart_x, now, window_sec);
    mqtt_send_recent_power(usart_x, now, window_sec);
    mqtt_send_recent_door(usart_x, now, window_sec);

    // 持久化MQTT发送状态到KVDB
    MqttSentState_t mqtt_state;
    mqtt_state.sampling_last_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_SAMPLING];
    mqtt_state.delivery_last_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DELIVERY];
    mqtt_state.retain_last_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_RETAIN];
    mqtt_state.power_last_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_POWER];
    mqtt_state.door_last_ts = g_cache_mgr.mqtt_last_sent_ts[CACHE_TYPE_DOOR];
    cfg_save_mqtt_state(&mqtt_state);
}

/**
 * @brief 辅助函数：检查MQTT连接状态
 */
static uint8_t mqtt_check_connected(char *Buf, usart_type *usart_x)
{
    char *str6 = "AT+MQTTSTATE=0\r\n";
    char *str_6 = "MQTTSTATE: 2";
    return TestATCommand(usart_x, Buf, str6, str_6, 3, 1000);
}

/**
 * @brief t=11: 发送采样设置
 */
static void mqtt_send_sample_config(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":11,\"i\":\"%s\",\"ab\":%u,\"m\":%u,\"it\":%u,\"si\":%u,\"th\":%u,\"sv\":%u,\"ct\":%u,\"bt\":%u,\"dt\":%u,\"at\":%u}",
            g_CommSettingConfig.IDSET,
            g_SampleConfig.BucketAB,
            g_SampleConfig.SamplingMode,
            g_SampleConfig.SamplingImproveTime,
            g_SampleConfig.SampleInterval,
            g_SampleConfig.TubeHoldTime,
            g_SampleConfig.SampleVolume,
            g_SampleConfig.CycleTime,
            g_SampleConfig.BlowbackTime,
            g_SampleConfig.BucketDrainTime,
            g_SampleConfig.AnalysisTime);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=12: 发送送样设置
 */
static void mqtt_send_delivery_config(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":12,\"i\":\"%s\",\"e\":%u,\"sh\":%u,\"sm\":%u,\"d\":%u,\"eh\":%u,\"em\":%u,\"iv\":%u}",
            g_CommSettingConfig.IDSET,
            g_DeliveryConfig.Enable,
            g_DeliveryConfig.StartHour,
            g_DeliveryConfig.StartMin,
            g_DeliveryConfig.Duration,
            g_DeliveryConfig.EndHour,
            g_DeliveryConfig.EndMin,
            g_DeliveryConfig.Interval);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=13: 发送留样基本设置
 */
static void mqtt_send_retain_basic(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":13,\"i\":\"%s\",\"m\":%u,\"bn\":%u,\"es\":%u,\"ea\":%u,\"ev\":%u,\"sv\":%u,\"pc\":%u,\"mc\":%u,\"th\":%u,\"bt\":%u,\"bd\":%u}",
            g_CommSettingConfig.IDSET,
            g_RetainSampleConfig.Mode,
            g_RetainSampleConfig.bottleNumber,
            g_RetainSampleConfig.EnableSample,
            g_RetainSampleConfig.EnableAcid,
            g_RetainSampleConfig.EnableVacuum,
            g_RetainSampleConfig.SampleVolume,
            g_RetainSampleConfig.ParallelCount,
            g_RetainSampleConfig.MixCount,
            g_RetainSampleConfig.TubeHoldTime,
            g_RetainSampleConfig.BlowbackTime,
            g_RetainSampleConfig.BackdrawTime);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=14: 发送通道限制配置(Hex编码)
 * 格式: 6通道 x (FactorType:1B + LowerLimit*100:2B + UpperLimit*100:2B + Enable:1B)
 */
static void mqtt_send_channel_limits(usart_type *usart_x)
{
    char message[200];
    char hexData[100];
    int idx = 0;
    int i;

    for(i = 0; i < 6; i++) {
        uint16_t lower = (uint16_t)(g_RetainSampleConfig.channelLimits[i].LowerLimit * 100);
        uint16_t upper = (uint16_t)(g_RetainSampleConfig.channelLimits[i].UpperLimit * 100);
        idx += sprintf(hexData + idx, "%02X%04X%04X%02X",
                       g_RetainSampleConfig.channelLimits[i].FactorType,
                       lower, upper,
                       g_RetainSampleConfig.channelLimits[i].Enable);
    }

    sprintf(message, "{\"t\":14,\"i\":\"%s\",\"cl\":\"%s\"}",
            g_CommSettingConfig.IDSET, hexData);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=15: 发送通道校准配置(Hex编码)
 * 格式: 8通道 x (InputAD:2B + ZeroAD:2B + CalAD:2B + CalValue*100:2B)
 */
static void mqtt_send_channel_cals(usart_type *usart_x)
{
    char message[250];
    char hexData[150];
    int idx = 0;
    int i;

    for(i = 0; i < 8; i++) {
        uint16_t calValue = (uint16_t)(g_RetainSampleConfig.channelCals[i].CalValue * 100);
        idx += sprintf(hexData + idx, "%04X%04X%04X%04X",
                       g_RetainSampleConfig.channelCals[i].InputAD,
                       g_RetainSampleConfig.channelCals[i].ZeroAD,
                       g_RetainSampleConfig.channelCals[i].CalAD,
                       calValue);
    }

    sprintf(message, "{\"t\":15,\"i\":\"%s\",\"cc\":\"%s\"}",
            g_CommSettingConfig.IDSET, hexData);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=21: 发送存水量状态
 */
static void mqtt_send_water_status(usart_type *usart_x)
{
    char message[100];
    sprintf(message, "{\"t\":21,\"i\":\"%s\",\"wa\":%u,\"wb\":%u}",
            g_CommSettingConfig.IDSET,
            g_State.SaveWarterA,
            g_State.SaveWarterB);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=22: 发送采样状态(完整西安协议)
 */
static void mqtt_send_sampling_status(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":22,\"i\":\"%s\",\"m\":%u,\"b\":%u,\"y\":%u,\"mo\":%u,\"d\":%u,\"h\":%u,\"mi\":%u,\"sq\":%u,\"v\":%u,\"r\":%u,\"as\":%u,\"bs\":%u}",
            g_CommSettingConfig.IDSET,
            g_XianSamplingLog.mode,
            g_XianSamplingLog.bucketId,
            g_XianSamplingLog.year,
            g_XianSamplingLog.month,
            g_XianSamplingLog.day,
            g_XianSamplingLog.hour,
            g_XianSamplingLog.minute,
            g_XianSamplingLog.sequence,
            g_XianSamplingLog.volume,
            g_XianSamplingLog.result,
            g_State.ABucketState,
            g_State.BBucketState);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=23: 发送送样状态(完整西安协议)
 */
static void mqtt_send_delivery_status(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":23,\"i\":\"%s\",\"m\":%u,\"b\":%u,\"y\":%u,\"mo\":%u,\"d\":%u,\"h\":%u,\"mi\":%u,\"v\":%u,\"r\":%u}",
            g_CommSettingConfig.IDSET,
            g_XianDeliveryLog.mode,
            g_XianDeliveryLog.bucketId,
            g_XianDeliveryLog.year,
            g_XianDeliveryLog.month,
            g_XianDeliveryLog.day,
            g_XianDeliveryLog.hour,
            g_XianDeliveryLog.minute,
            g_XianDeliveryLog.volume,
            g_XianDeliveryLog.result);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief t=24: 发送留样状态(完整西安协议)
 */
static void mqtt_send_retain_status(usart_type *usart_x)
{
    char message[200];
    sprintf(message, "{\"t\":24,\"i\":\"%s\",\"m\":%u,\"rs\":%u,\"y\":%u,\"mo\":%u,\"d\":%u,\"h\":%u,\"mi\":%u,\"bn\":%u,\"v\":%u,\"r\":%u}",
            g_CommSettingConfig.IDSET,
            g_XianRetainLog.mode,
            g_XianRetainLog.reason,
            g_XianRetainLog.year,
            g_XianRetainLog.month,
            g_XianRetainLog.day,
            g_XianRetainLog.hour,
            g_XianRetainLog.minute,
            g_XianRetainLog.bottleId,
            g_XianRetainLog.volume,
            g_XianRetainLog.result);
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief 15分钟状态上报(4条MQTT消息)
 */
void MqttSendStatusAll(char *Buf, usart_type *usart_x)
{
    // 检查MQTT连接
    if(!mqtt_check_connected(Buf, usart_x)) {
        MqttInit(Buf, usart_x);
    }

    // 依次发送4条状态消息
    mqtt_send_water_status(usart_x);
    mqtt_send_sampling_status(usart_x);
    mqtt_send_delivery_status(usart_x);
    mqtt_send_retain_status(usart_x);

    printf("[MQTT] 15分钟状态上报完成(4条)\r\n");
}

/**
 * @brief t=16: 发送GPS/LBS位置信息
 */
static void mqtt_send_location(usart_type *usart_x)
{
    LocationInfo_t loc = {0};
    char message[250];

    if (cfg_load_location(&loc) && loc.valid) {
        sprintf(message, "{\"t\":16,\"i\":\"%s\",\"lon\":%.2f,\"lat\":%.2f,\"sn\":\"%s\",\"cv\":\"%s\",\"lv\":\"%s\"}",
                g_CommSettingConfig.IDSET,
                loc.longitude, loc.latitude,
                g_SystemSettingConfig.SoftwareSerial,
                g_SystemSettingConfig.SoftwareCoreVer,
                g_SystemSettingConfig.SoftwareLcdVer);
        printf("[MQTT] 位置+版本上报: lon=%.2f, lat=%.2f\r\n", loc.longitude, loc.latitude);
    } else {
        sprintf(message, "{\"t\":16,\"i\":\"%s\",\"sn\":\"%s\",\"cv\":\"%s\",\"lv\":\"%s\"}",
                g_CommSettingConfig.IDSET,
                g_SystemSettingConfig.SoftwareSerial,
                g_SystemSettingConfig.SoftwareCoreVer,
                g_SystemSettingConfig.SoftwareLcdVer);
        printf("[MQTT] 版本上报(无位置数据)\r\n");
    }
    mqtt_publish_message(usart_x, message);
}

/**
 * @brief 2小时设置上报(5条MQTT消息 + 位置信息)
 */
void MqttSendSettingsAll(char *Buf, usart_type *usart_x)
{
    // 检查MQTT连接
    if(!mqtt_check_connected(Buf, usart_x)) {
        MqttInit(Buf, usart_x);
    }

    // 依次发送5条设置消息
    mqtt_send_sample_config(usart_x);
    mqtt_send_delivery_config(usart_x);
    mqtt_send_retain_basic(usart_x);
    mqtt_send_channel_limits(usart_x);
    mqtt_send_channel_cals(usart_x);
    
    // 发送位置信息
    mqtt_send_location(usart_x);

    printf("[MQTT] 2小时设置上报完成(5条+位置)\r\n");
}

/**
 * @brief 时间同步函数
 * @param usart_x 串口指针
 * @param Buf 缓冲区
 */
void timesyc(usart_type *usart_x, char *Buf)
{
    char year[3], month[3], date[3], hour[3], min[3], sec[3];
    char *time_start;

    if (TestATCommand(usart_x, Buf, "AT+CCLK?\r\n", "OK", 20, 1500))
    {

        // 查找 "+CCLK:" 后面的时间字符串
        time_start = strstr(Buf, "+CCLK:");
        if (time_start != NULL)
        {
            time_start += 6; // 跳过 "+CCLK:"
            // 跳过??能的空格，找到引??
            while (*time_start == ' ')
            {
                time_start++;
            }
            if (*time_start == '\"')
            {
                time_start++; // 跳过引号

                // 解析格式: 25/09/16,11:39:01
                // 提取年月??: YY/MM/DD
                strncpy(year, time_start, 2);
                year[2] = '\0';
                strncpy(month, time_start + 3, 2);
                month[2] = '\0';
                strncpy(date, time_start + 6, 2);
                date[2] = '\0';

                // 提取时分??: HH:MM:SS
                strncpy(hour, time_start + 9, 2);
                hour[2] = '\0';
                strncpy(min, time_start + 12, 2);
                min[2] = '\0';
                strncpy(sec, time_start + 15, 2);
                sec[2] = '\0';

                memset(Buf, 0, 512);
                calendar.year = 2000 + atoi(year);
                calendar.month = atoi(month);
                calendar.date = atoi(date);
                calendar.hour = atoi(hour);
                calendar.min = atoi(min);
                calendar.sec = atoi(sec);

                adjust_to_beijing_time(&calendar);

                rtc_time_set(&calendar);

                // ★ 同步更新g_SystemSettingConfig的时间字段，确保所有时间源一致
                g_SystemSettingConfig.Year = calendar.year;
                g_SystemSettingConfig.Month = calendar.month;
                g_SystemSettingConfig.Day = calendar.date;
                g_SystemSettingConfig.Hour = calendar.hour;
                g_SystemSettingConfig.Minute = calendar.min;
                g_SystemSettingConfig.Second = calendar.sec;
            }
        }
    }
}

/**
 * @brief 调整时间为北京时间
 * @param time_struct 时间结构体
 */
void adjust_to_beijing_time(calendar_type *time_struct)
{
    int year = time_struct->year;
    int month = time_struct->month;
    int date = time_struct->date;
    int hour = time_struct->hour;
    int min = time_struct->min;
    int sec = time_struct->sec;

    // 1. Add 8 hours to convert from UTC to Beijing time
    hour += 8;

    // 2. Handle hour overflow
    if (hour >= 24)
    {
        hour -= 24;
        date += 1;

        // 3. Handle day overflow
        if (date > days_in_month(month, year))
        {
            date = 1;
            month += 1;

            // 4. Handle month overflow
            if (month > 12)
            {
                month = 1;
                year += 1;
            }
        }
    }

    // Update the time_struct with adjusted Beijing time
    time_struct->year = year;
    time_struct->month = month;
    time_struct->date = date;
    time_struct->hour = hour;
    time_struct->min = min;
    time_struct->sec = sec;
}

/**
 * @brief 获取某个月的天数
 * @param month 月份
 * @param year 年份
 * @return 天数
 */
int days_in_month(int month, int year)
{
    switch (month)
    {
    case 4:
    case 6:
    case 9:
    case 11:
        return 30;

    case 2:
        if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
        {
            return 29; // Leap year
        }
        else
        {
            return 28;
        }

    default:
        return 31;
    }
}

/**
 * @brief 解析IP地址的函数  收到字符串"SET_***_IP192.168.100.0Y"
 * @param str 输入字符串
 * @param ip_out 输出IP地址
 * @param max_len 最大长度
 * @return 提取的长度
 */
int extract_ip_from_setmcu(const char *str, char *ip_out, int max_len)
{
    char *ip_pos = strstr(str, "_IP");
    if (ip_pos == NULL)
    {
        return 0;
    }

    char *start = ip_pos + 3;
    char *end = strchr(start, 'Y');

    if (end == NULL)
    {
        return 0;
    }

    int len = end - start;
    if (len > 0 && len < max_len)
    {
        strncpy(ip_out, start, len);
        ip_out[len] = '\0';
        return len;
    }

    return 0;
}

/**
 * @brief 检查并处理调试指令
 * @param recv_buf 接收到的数据缓冲区
 * @return 1=处理了调试指令, 0=非调试指令
 */
uint8_t CheckDebugCommand(char *recv_buf)
{
    char *debug_prefix = "Debug_";
    char *device_id_pos;

    // 查找Debug_前缀
    device_id_pos = strstr(recv_buf, debug_prefix);
    if (!device_id_pos)
    {
        return 0;
    }

    // 跳过前缀
    device_id_pos += strlen(debug_prefix);

    // 检查关闭指令
    if (strstr(device_id_pos, "Close") || strstr(device_id_pos, "close"))
    {
        if (debug_mode)
        {
            debug_mode = 0;
            // 清空调试缓冲区
            if (debug_mutex)
            {
                xSemaphoreTake(debug_mutex, pdMS_TO_TICKS(100));
                memset(debug_buffer, 0, sizeof(debug_buffer));
                debug_buffer_index = 0;
                xSemaphoreGive(debug_mutex);
            }
            printf("[调试] 已关闭MQTT调试模式\r\n");
        }
        return 1;
    }

    // 检查开启指令
    if (strncmp(device_id_pos, g_CommSettingConfig.IDSET, strlen(g_CommSettingConfig.IDSET)) == 0)
    {
        debug_mode = 1;
        printf("[调试] 已开启MQTT调试模式，设备ID: %s\r\n", g_CommSettingConfig.IDSET);
        printf("[调试] 调试消息将发送到MQTT主题: DEBUG\r\n");
        return 1;
    }

    return 0;
}

/**
 * @brief 检查是否在调试模式
 * @return 1=在调试模式, 0=非调试模式
 */
uint8_t IsInDebugMode(void)
{
    return debug_mode;
}

/**
 * @brief 检查并处理调试缓存
 * @param usart_x 串口指针
 * @return 1=发送成功, 0=发送失败
 */
uint8_t ProcessDebugCache(usart_type *usart_x)
{
    char mqtt_cmd[600];
    uint16_t send_len;
    //    uint32_t len;

    // 如果没有缓存数据或互斥锁未创建，直接返回
    if (debug_buffer_index == 0 || debug_mutex == NULL)
    {
        return 0;
    }

    // 创建互斥访问
    if (xSemaphoreTake(debug_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {

        // 构造MQTT发布命令 - 单条消息最大400字节
        send_len = debug_buffer_index > 400 ? 400 : debug_buffer_index;

        // 处理特殊字符
        char escaped_msg[410];
        uint16_t src_idx = 0, dst_idx = 0;
        while (src_idx < send_len && dst_idx < sizeof(escaped_msg) - 2)
        {
            if (debug_buffer[src_idx] == '\"' || debug_buffer[src_idx] == '\\')
            {
                escaped_msg[dst_idx++] = '\\';
            }
            // 过滤掉不可打印字符
            if (debug_buffer[src_idx] >= 32 && debug_buffer[src_idx] <= 126)
            {
                escaped_msg[dst_idx++] = debug_buffer[src_idx];
            }
            src_idx++;
        }
        escaped_msg[dst_idx] = '\0';

        sprintf(mqtt_cmd, "AT+MQTTPUB=0,\"DEBUG\",0,0,0,%d,\"%s\"\r\n",
                dst_idx, escaped_msg);

        // 清空UART6_Buf的前部，用于接收MQTT发布响应
        memset(UART6_Buf, 0, 200);

        // 发送AT命令
        SendData(usart_x, mqtt_cmd, strlen(mqtt_cmd));

        // 等待发送完成并适当延迟
        vTaskDelay(pdMS_TO_TICKS(300));

        // 清空已发送部分
        if (debug_buffer_index > 400)
        {
            memmove(debug_buffer, debug_buffer + 400, debug_buffer_index - 400);
            debug_buffer_index -= 400;
        }
        else
        {
            debug_buffer_index = 0;
            memset(debug_buffer, 0, sizeof(debug_buffer));
        }

        xSemaphoreGive(debug_mutex);
        return 1;
    }

    return 0;
}

/**
 * @brief 发送调试消息到MQTT（兼容函数）
 * @param usart_x 串口指针
 * @return 1=发送成功, 0=发送失败
 */
uint8_t MqttDebugSend(usart_type *usart_x)
{
    return ProcessDebugCache(usart_x);
}

/**
 * @brief 过滤字符串中的空格
 * @param str 要处理的字符串
 */
void filter_spaces(char *str)
{
    if (str == NULL)
        return;

    char *dst = str;
    while (*str)
    {
        if (*str != ' ')
        {
            *dst++ = *str;
        }
        str++;
    }
    *dst = '\0';
}

/**
 * @brief 从AT响应中提取数字
 * @param buf 响应缓冲区
 * @param prefix 数字前缀
 * @param suffix 数字后缀
 * @return 提取的数字，失败返回0
 */
uint32_t extract_number_from_response(char *buf, const char *prefix, const char *suffix)
{
    char *start = strstr(buf, prefix);
    if (start == NULL)
        return 0;

    start += strlen(prefix);
    char *end = strstr(start, suffix);
    if (end == NULL)
        return 0;

    // 临时存储数字字符串
    char num_str[32];
    int len = end - start;
    if (len <= 0 || len >= sizeof(num_str))
        return 0;

    strncpy(num_str, start, len);
    num_str[len] = '\0';

    // 过滤数字字符串中的空格
    filter_spaces(num_str);

    return strtoul(num_str, NULL, 10);
}

/**
 * @file    app_ota.c
 * @brief   OTA升级状态机 — 擦除、接收、校验、写标志、重启
 */
#include "app_ota.h"
#include "bsp_flash.h"
#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

/* 外部定时器 */
extern volatile uint32_t g_tmr4_milliseconds;

/* 全局OTA控制块 */
ota_ctrl_t g_ota;

/* 待处理包标志 */
static uint8_t s_pkt_ready;
static ota_packet_t s_pkt_buf;

/* CRC16累加和（与samplingB兼容） */
static uint16_t ota_crc16(uint32_t start_addr, uint32_t len)
{
    uint16_t crc = 0;
    uint16_t val;
    uint32_t i;
    for (i = 0; i < len; i += 2) {
        bsp_flash_read(start_addr + i, &val, 1);
        crc += val;
    }
    return crc;
}

/* 擦除OTA临时区 */
static uint8_t ota_erase_temp(void)
{
    uint32_t addr;
    printf("[OTA] 擦除临时区 0x%08lX ~ 0x%08lX\r\n",
           (unsigned long)OTA_TEMP_ADDR,
           (unsigned long)(OTA_TEMP_ADDR + OTA_TEMP_SIZE - 1));
    flash_unlock();
    for (addr = OTA_TEMP_ADDR; addr < OTA_TEMP_ADDR + OTA_TEMP_SIZE;
         addr += FLASH_BANK1_SECTOR_SIZE)
    {
        if (flash_sector_erase(addr) != FLASH_OPERATE_DONE) {
            flash_lock();
            printf("[OTA] 擦除失败 addr=0x%08lX\r\n", (unsigned long)addr);
            return 0;
        }
    }
    flash_lock();
    printf("[OTA] 擦除完成\r\n");
    return 1;
}

/* ======================== 公共接口 ======================== */

void ota_init(void)
{
    memset(&g_ota, 0, sizeof(g_ota));
    s_pkt_ready = 0;
    printf("[OTA] 初始化\r\n");
}

uint8_t ota_start(ota_source_t src, uint32_t fw_size, uint32_t fw_crc)
{
    if (g_ota.state != OTA_IDLE && g_ota.state != OTA_ERROR &&
        g_ota.state != OTA_COMPLETE)
        return 0;

    if (fw_size == 0 || fw_size > OTA_MAX_FW_SIZE) {
        printf("[OTA] 固件大小无效: %lu\r\n", (unsigned long)fw_size);
        return 0;
    }

    memset(&g_ota, 0, sizeof(g_ota));
    g_ota.source      = src;
    g_ota.total_size   = fw_size;
    g_ota.file_crc     = fw_crc;
    g_ota.flash_addr   = OTA_TEMP_ADDR;
    g_ota.state        = OTA_INIT;
    g_ota.last_tick    = g_tmr4_milliseconds;
    s_pkt_ready = 0;

    printf("[OTA] 启动 src=%d size=%lu crc=0x%04lX\r\n",
           src, (unsigned long)fw_size, (unsigned long)fw_crc);
    return 1;
}

/* 缓冲区写入Flash */
static void ota_flush_buffer(void)
{
    uint32_t write_addr;
    uint16_t len;

    if (g_ota.buf_pos == 0) return;

    write_addr = g_ota.flash_addr +
                 (g_ota.received_size - g_ota.buf_pos);

    /* 不足半字部分填0xFF */
    len = g_ota.buf_pos;
    if (len & 1) {
        g_ota.buffer[len] = 0xFF;
        len++;
    }

    bsp_flash_write_nocheck(write_addr, (uint16_t *)g_ota.buffer, len / 2);
    g_ota.buf_pos = 0;
}

uint8_t ota_feed_packet(const uint8_t *data, uint16_t len)
{
    const ota_packet_t *pkt;

    if (g_ota.state != OTA_WAIT_DATA) return 0;
    if (len < OTA_PKT_TOTAL_SIZE) return 0;

    pkt = (const ota_packet_t *)data;

    /* 包头校验 */
    if (pkt->header != OTA_PKT_HEADER) return 0;

    /* 包序号校验 */
    if (pkt->packet_id != g_ota.expected_pkt) {
        printf("[OTA] 包序号不匹配: 期望%lu 收到%u\r\n",
               (unsigned long)g_ota.expected_pkt, pkt->packet_id);
        return 0;
    }

    /* 数据校验和 */
    {
        uint8_t sum = 0;
        uint16_t i;
        for (i = 0; i < pkt->data_len; i++)
            sum += pkt->data[i];
        if (sum != pkt->checksum) {
            printf("[OTA] 校验和错误: pkt=%u\r\n", pkt->packet_id);
            return 0;
        }
    }

    /* 缓存到待处理区 */
    memcpy(&s_pkt_buf, pkt, sizeof(ota_packet_t));
    s_pkt_ready = 1;
    g_ota.last_tick = g_tmr4_milliseconds;
    return 1;
}

uint8_t ota_feed_raw(const uint8_t *data, uint16_t len)
{
    uint16_t i;

    if (g_ota.state != OTA_WAIT_DATA && g_ota.state != OTA_WRITE)
        return 0;

    for (i = 0; i < len; i++) {
        g_ota.buffer[g_ota.buf_pos++] = data[i];
        g_ota.received_size++;

        if (g_ota.buf_pos >= OTA_BUF_SIZE) {
            ota_flush_buffer();
        }
    }
    g_ota.last_tick = g_tmr4_milliseconds;
    return 1;
}

/* 处理一个数据包 */
static void ota_process_packet(void)
{
    uint16_t copy_len = s_pkt_buf.data_len;
    if (copy_len > OTA_PKT_DATA_SIZE)
        copy_len = OTA_PKT_DATA_SIZE;

    memcpy(&g_ota.buffer[g_ota.buf_pos], s_pkt_buf.data, copy_len);
    g_ota.buf_pos += copy_len;
    g_ota.received_size += copy_len;

    if (g_ota.buf_pos >= OTA_BUF_SIZE)
        ota_flush_buffer();

    g_ota.expected_pkt++;
    s_pkt_ready = 0;
    g_ota.retry_count = 0;

    /* 接收完毕？ */
    if (g_ota.expected_pkt >= g_ota.total_packets ||
        g_ota.received_size >= g_ota.total_size)
    {
        ota_flush_buffer();
        g_ota.state = OTA_VERIFY;
        printf("[OTA] 接收完毕 %lu/%lu\r\n",
               (unsigned long)g_ota.received_size,
               (unsigned long)g_ota.total_size);
    } else {
        g_ota.state = OTA_WAIT_DATA;
        g_ota.last_tick = g_tmr4_milliseconds;
    }
}

/* 校验固件并完成升级 */
static void ota_verify_firmware(void)
{
    uint16_t crc;

    /* 大小校验 */
    if (g_ota.received_size < g_ota.total_size) {
        printf("[OTA] 大小不足 %lu < %lu\r\n",
               (unsigned long)g_ota.received_size,
               (unsigned long)g_ota.total_size);
        g_ota.state = OTA_ERROR;
        return;
    }

    /* CRC16校验 */
    crc = ota_crc16(OTA_TEMP_ADDR, g_ota.total_size);
    if (crc != (uint16_t)g_ota.file_crc) {
        printf("[OTA] CRC校验失败: 计算=0x%04X 期望=0x%04lX\r\n",
               crc, (unsigned long)g_ota.file_crc);
        g_ota.state = OTA_ERROR;
        return;
    }

    printf("[OTA] 校验通过，写入升级标志\r\n");
    bsp_flash_upgrade_flag_write();
    g_ota.state = OTA_COMPLETE;
    printf("[OTA] 升级完成，等待重启\r\n");
}

/* ======================== 状态机轮询 ======================== */

void ota_poll(void)
{
    switch (g_ota.state) {
    case OTA_INIT:
        if (!ota_erase_temp()) {
            g_ota.state = OTA_ERROR;
            printf("[OTA] 擦除失败，中止\r\n");
            break;
        }
        g_ota.state = OTA_WAIT_DATA;
        g_ota.last_tick = g_tmr4_milliseconds;
        printf("[OTA] 等待数据包...\r\n");
        break;

    case OTA_WAIT_DATA:
        if ((g_tmr4_milliseconds - g_ota.last_tick) > OTA_TIMEOUT_MS) {
            g_ota.retry_count++;
            if (g_ota.retry_count >= OTA_MAX_RETRY) {
                g_ota.state = OTA_ERROR;
                printf("[OTA] 超时，重试耗尽\r\n");
            } else {
                g_ota.last_tick = g_tmr4_milliseconds;
                printf("[OTA] 超时，重试%lu\r\n",
                       (unsigned long)g_ota.retry_count);
            }
            break;
        }
        if (s_pkt_ready) {
            g_ota.state = OTA_PROCESS;
        }
        break;

    case OTA_PROCESS:
        ota_process_packet();
        break;

    case OTA_VERIFY:
        ota_verify_firmware();
        break;

    case OTA_COMPLETE:
    case OTA_ERROR:
    default:
        break;
    }
}

/* ======================== 查询接口 ======================== */

ota_state_t ota_get_state(void) { return g_ota.state; }

uint8_t ota_is_active(void)
{
    return (g_ota.state != OTA_IDLE && g_ota.state != OTA_COMPLETE &&
            g_ota.state != OTA_ERROR) ? 1 : 0;
}

uint8_t ota_get_progress(void)
{
    if (g_ota.total_size == 0) return 0;
    return (uint8_t)((g_ota.received_size * 100) / g_ota.total_size);
}

void ota_abort(void)
{
    if (!ota_is_active()) return;
    g_ota.state = OTA_ERROR;
    s_pkt_ready = 0;
    printf("[OTA] 中止\r\n");
}

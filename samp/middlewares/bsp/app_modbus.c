/**
 * @file    app_modbus.c
 * @brief   Modbus从站协议栈实现
 */
#include "app_modbus.h"
#include "bsp_crc.h"
#include "app_config.h"
#include "app_adc_module.h"
#include "app_scheduler.h"
#include <string.h>
#include <stdio.h>

/* 内部状态 */
static uint8_t  s_protocol;
static uint8_t  s_slave_addr;
static uint16_t s_holding_regs[MB_HOLDING_REG_COUNT];
static uint16_t s_input_regs[MB_INPUT_REG_COUNT];

/* ======================== 寄存器读写 ======================== */

uint16_t modbus_get_holding(uint16_t addr)
{
    if (addr < MB_HOLDING_REG_COUNT)
        return s_holding_regs[addr];
    return 0;
}

void modbus_set_holding(uint16_t addr, uint16_t value)
{
    if (addr < MB_HOLDING_REG_COUNT)
        s_holding_regs[addr] = value;
}

uint16_t modbus_get_input(uint16_t addr)
{
    if (addr < MB_INPUT_REG_COUNT)
        return s_input_regs[addr];
    return 0;
}

void modbus_set_input(uint16_t addr, uint16_t value)
{
    if (addr < MB_INPUT_REG_COUNT)
        s_input_regs[addr] = value;
}

/* ======================== 初始化 ======================== */

void modbus_init(uint8_t protocol, uint8_t slave_addr)
{
    s_protocol   = protocol;
    s_slave_addr = slave_addr;
    memset(s_holding_regs, 0, sizeof(s_holding_regs));
    memset(s_input_regs, 0, sizeof(s_input_regs));

    /* 从配置同步初始值到保持寄存器 */
    s_holding_regs[0] = g_sampling_cfg.mode;
    s_holding_regs[1] = g_sampling_cfg.interval_min;
    s_holding_regs[2] = g_sampling_cfg.volume_ml;
    s_holding_regs[3] = g_sampling_cfg.blowback_sec;
    s_holding_regs[4] = g_sampling_cfg.motor_rpm;
    s_holding_regs[5] = g_delivery_cfg.volume_ml;
    s_holding_regs[6] = g_delivery_cfg.motor_rpm;
    s_holding_regs[7] = g_retain_cfg.volume_ml;
    s_holding_regs[8] = g_retain_cfg.bottle_count;

    printf("[Modbus] 初始化 协议=%u 地址=%u\r\n",
           (unsigned int)protocol, (unsigned int)slave_addr);
}

/* ======================== 同步系统状态 ======================== */

void modbus_sync_status(void)
{
    s_input_regs[0] = g_state.running;
    s_input_regs[1] = g_state.bucket_a_state;
    s_input_regs[2] = g_state.bucket_b_state;
    s_input_regs[3] = g_state.current_bucket;
    s_input_regs[4] = s_protocol;  /* 当前协议变体 */

    /* ADC模块数据 */
    s_input_regs[10] = adc_module_get_raw(0);
    s_input_regs[11] = adc_module_get_raw(1);
    s_input_regs[12] = adc_module_get_raw(2);
    s_input_regs[13] = adc_module_get_raw(3);
    s_input_regs[14] = adc_module_get_raw(4);
    s_input_regs[15] = adc_module_get_raw(5);

    /* 调度器状态 */
    s_input_regs[20] = g_state.current_mode;
    s_input_regs[21] = g_state.current_phase;
    s_input_regs[22] = (uint16_t)g_state.cycle_count;
    s_input_regs[23] = (uint16_t)g_state.sample_count;
    s_input_regs[24] = (uint16_t)g_state.delivery_count;
}

/* ======================== 异常应答 ======================== */

static uint16_t mb_exception(uint8_t func, uint8_t code,
                             uint8_t *resp)
{
    resp[0] = s_slave_addr;
    resp[1] = func | 0x80;
    resp[2] = code;
    uint16_t crc = crc16_modbus(resp, 3);
    resp[3] = (uint8_t)(crc & 0xFF);
    resp[4] = (uint8_t)(crc >> 8);
    return 5;
}

/* ======================== 读保持寄存器 0x03 ======================== */

static uint16_t mb_read_holding(const uint8_t *frame,
                                uint8_t *resp, uint16_t resp_max)
{
    uint16_t start = (frame[2] << 8) | frame[3];
    uint16_t count = (frame[4] << 8) | frame[5];

    if (count == 0 || count > 125)
        return mb_exception(MB_FUNC_READ_HOLDING, 0x03, resp);
    if ((start + count) > MB_HOLDING_REG_COUNT)
        return mb_exception(MB_FUNC_READ_HOLDING, 0x02, resp);
    if ((3 + count * 2 + 2) > resp_max)
        return 0;

    resp[0] = s_slave_addr;
    resp[1] = MB_FUNC_READ_HOLDING;
    resp[2] = (uint8_t)(count * 2);

    uint16_t i;
    for (i = 0; i < count; i++) {
        resp[3 + i * 2]     = (uint8_t)(s_holding_regs[start + i] >> 8);
        resp[3 + i * 2 + 1] = (uint8_t)(s_holding_regs[start + i] & 0xFF);
    }

    uint16_t pdu_len = 3 + count * 2;
    uint16_t crc = crc16_modbus(resp, pdu_len);
    resp[pdu_len]     = (uint8_t)(crc & 0xFF);
    resp[pdu_len + 1] = (uint8_t)(crc >> 8);
    return pdu_len + 2;
}

/* ======================== 读输入寄存器 0x04 ======================== */

static uint16_t mb_read_input(const uint8_t *frame,
                              uint8_t *resp, uint16_t resp_max)
{
    uint16_t start = (frame[2] << 8) | frame[3];
    uint16_t count = (frame[4] << 8) | frame[5];

    if (count == 0 || count > 125)
        return mb_exception(MB_FUNC_READ_INPUT, 0x03, resp);
    if ((start + count) > MB_INPUT_REG_COUNT)
        return mb_exception(MB_FUNC_READ_INPUT, 0x02, resp);
    if ((3 + count * 2 + 2) > resp_max)
        return 0;

    resp[0] = s_slave_addr;
    resp[1] = MB_FUNC_READ_INPUT;
    resp[2] = (uint8_t)(count * 2);

    uint16_t i;
    for (i = 0; i < count; i++) {
        resp[3 + i * 2]     = (uint8_t)(s_input_regs[start + i] >> 8);
        resp[3 + i * 2 + 1] = (uint8_t)(s_input_regs[start + i] & 0xFF);
    }

    uint16_t pdu_len = 3 + count * 2;
    uint16_t crc = crc16_modbus(resp, pdu_len);
    resp[pdu_len]     = (uint8_t)(crc & 0xFF);
    resp[pdu_len + 1] = (uint8_t)(crc >> 8);
    return pdu_len + 2;
}

/* ======================== 写单个寄存器 0x06 ======================== */

static uint16_t mb_write_single(const uint8_t *frame,
                                uint8_t *resp, uint16_t resp_max)
{
    uint16_t addr  = (frame[2] << 8) | frame[3];
    uint16_t value = (frame[4] << 8) | frame[5];

    if (addr >= MB_HOLDING_REG_COUNT)
        return mb_exception(MB_FUNC_WRITE_SINGLE, 0x02, resp);
    if (resp_max < 8)
        return 0;

    s_holding_regs[addr] = value;

    /* 通信触发命令 */
    switch (addr) {
    case 40:
        scheduler_notify_comm(COMM_REQ_SAMPLING,
                              (uint8_t)(value >> 8), value & 0xFF);
        break;
    case 41:
        scheduler_notify_comm(COMM_REQ_DELIVERY,
                              (uint8_t)(value >> 8), 0);
        break;
    case 42:
        scheduler_notify_comm(COMM_REQ_DRAIN,
                              (uint8_t)(value >> 8), 0);
        break;
    default:
        break;
    }

    /* 原样回显 */
    memcpy(resp, frame, 6);
    uint16_t crc = crc16_modbus(resp, 6);
    resp[6] = (uint8_t)(crc & 0xFF);
    resp[7] = (uint8_t)(crc >> 8);
    return 8;
}

/* ======================== 写多个寄存器 0x10 ======================== */

static uint16_t mb_write_multiple(const uint8_t *frame,
                                  uint8_t *resp, uint16_t resp_max)
{
    uint16_t start = (frame[2] << 8) | frame[3];
    uint16_t count = (frame[4] << 8) | frame[5];
    uint8_t  bytes = frame[6];

    if (count == 0 || count > 123 || bytes != count * 2)
        return mb_exception(MB_FUNC_WRITE_MULTIPLE, 0x03, resp);
    if ((start + count) > MB_HOLDING_REG_COUNT)
        return mb_exception(MB_FUNC_WRITE_MULTIPLE, 0x02, resp);
    if (resp_max < 8)
        return 0;

    uint16_t i;
    for (i = 0; i < count; i++) {
        s_holding_regs[start + i] =
            (frame[7 + i * 2] << 8) | frame[7 + i * 2 + 1];
    }

    resp[0] = s_slave_addr;
    resp[1] = MB_FUNC_WRITE_MULTIPLE;
    resp[2] = (uint8_t)(start >> 8);
    resp[3] = (uint8_t)(start & 0xFF);
    resp[4] = (uint8_t)(count >> 8);
    resp[5] = (uint8_t)(count & 0xFF);
    uint16_t crc = crc16_modbus(resp, 6);
    resp[6] = (uint8_t)(crc & 0xFF);
    resp[7] = (uint8_t)(crc >> 8);
    return 8;
}

/* ======================== 帧处理入口 ======================== */

uint16_t modbus_poll(const uint8_t *frame, uint16_t len,
                     uint8_t *resp, uint16_t resp_max)
{
    if (len < 4) return 0;
    if (frame[0] != s_slave_addr) return 0;

    /* CRC校验 */
    if (!crc16_check((uint8_t *)frame, len)) return 0;

    switch (frame[1]) {
    case MB_FUNC_READ_HOLDING:
        return mb_read_holding(frame, resp, resp_max);
    case MB_FUNC_READ_INPUT:
        return mb_read_input(frame, resp, resp_max);
    case MB_FUNC_WRITE_SINGLE:
        return mb_write_single(frame, resp, resp_max);
    case MB_FUNC_WRITE_MULTIPLE:
        return mb_write_multiple(frame, resp, resp_max);
    default:
        return mb_exception(frame[1], 0x01, resp);
    }
}

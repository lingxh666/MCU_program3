/**
 * @file mb.c
 * @brief Modbus RTU 多实例核心实现
 * @note  精简版FreeModbus，支持0x03/0x04/0x06/0x10功能码
 *        适配DMA+IDLE接收方式，使用AT32硬件CRC
 */

#include "mb.h"
#include "work.h"  /* for CRC16_MODBUS, vSendData */
#include <stdio.h>

/* ===== 内部函数声明 ===== */
static uint16_t mb_func_read_holding(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp);
static uint16_t mb_func_read_input(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp);
static uint16_t mb_func_write_single(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp);
static uint16_t mb_func_write_multiple(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp);

/* ===== 实例初始化 ===== */
uint8_t eMBInit_Inst(MBInstance_t *inst, uint8_t address,
                     usart_type *usart, MBMode_t mode)
{
    if (inst == NULL || usart == NULL) {
        return 0;
    }

    /* 清零实例 */
    memset(inst, 0, sizeof(MBInstance_t));

    /* 基本配置 */
    inst->address = address;
    inst->mode = mode;
    inst->state = MB_STATE_DISABLED;

    /* 串口配置 */
    inst->serial.usart = usart;

    return 1;
}

uint8_t eMBEnable_Inst(MBInstance_t *inst)
{
    if (inst == NULL) {
        return 0;
    }
    inst->state = MB_STATE_ENABLED;
    return 1;
}

uint8_t eMBDisable_Inst(MBInstance_t *inst)
{
    if (inst == NULL) {
        return 0;
    }
    inst->state = MB_STATE_DISABLED;
    return 1;
}

/* ===== 设置接收帧 ===== */
void eMBSetRcvFrame_Inst(MBInstance_t *inst, uint8_t *data, uint16_t len)
{
    if (inst == NULL || data == NULL || len == 0) {
        return;
    }

    inst->serial.rcv_buf = data;
    inst->serial.rcv_len = len;
    inst->serial.rcv_pos = 0;

    /* 触发帧接收事件 */
    inst->event.event_in_queue = 1;
    inst->event.queued_event = 1;
}

/* ===== 轮询处理 ===== */
uint8_t eMBPoll_Inst(MBInstance_t *inst)
{
    uint8_t *frame;
    uint16_t len;
    uint16_t crc_calc, crc_recv;
    uint8_t rcv_addr, func_code;
    uint8_t resp[256];
    uint16_t resp_len = 0;
    uint16_t crc;

    if (inst == NULL || inst->state != MB_STATE_ENABLED) {
        return 0;
    }

    if (!inst->event.event_in_queue) {
        return 0;
    }

    inst->event.event_in_queue = 0;

    frame = inst->serial.rcv_buf;
    len = inst->serial.rcv_len;

    /* 最小帧长度检查：地址(1) + 功能码(1) + CRC(2) = 4 */
    if (len < 4) {
        printf("[MB] 帧长度过短: %d\r\n", len);
        return 0;
    }

    /* CRC校验 */
    crc_calc = CRC16_MODBUS(frame, len - 2);
    crc_recv = frame[len-2] | ((uint16_t)frame[len-1] << 8);
    if (crc_calc != crc_recv) {
        printf("[MB] CRC错误: calc=0x%04X, recv=0x%04X\r\n", crc_calc, crc_recv);
        return 0;
    }

    /* 站号检查 */
    rcv_addr = frame[0];
    inst->rcv_address = rcv_addr;  /* 保存用于应答 */

    if (inst->mode == MB_MODE_NORMAL) {
        /* 正常模式：必须匹配站号（0为广播地址，不响应） */
        if (rcv_addr != inst->address) {
            if (rcv_addr != 0) {
                /* 非广播地址且不匹配，忽略 */
            }
            return 0;
        }
    }
    /* 广播模式：忽略站号检查，使用请求帧站号应答 */

    /* 解析功能码并处理 */
    func_code = frame[1];

    switch (func_code) {
        case MB_FUNC_READ_HOLDING:  /* 0x03 读保持寄存器 */
            resp_len = mb_func_read_holding(inst, frame, len, resp);
            break;

        case MB_FUNC_READ_INPUT:    /* 0x04 读输入寄存器 */
            resp_len = mb_func_read_input(inst, frame, len, resp);
            break;

        case MB_FUNC_WRITE_SINGLE:  /* 0x06 写单个寄存器 */
            resp_len = mb_func_write_single(inst, frame, len, resp);
            break;

        case MB_FUNC_WRITE_MULTIPLE: /* 0x10 写多个寄存器 */
            resp_len = mb_func_write_multiple(inst, frame, len, resp);
            break;

        default:
            /* 不支持的功能码：返回异常 */
            printf("[MB] 不支持的功能码: 0x%02X\r\n", func_code);
            resp[0] = inst->rcv_address;
            resp[1] = func_code | 0x80;
            resp[2] = MB_EX_ILLEGAL_FUNCTION;
            resp_len = 3;
            break;
    }

    /* 添加CRC并发送 */
    if (resp_len > 0) {
        crc = CRC16_MODBUS(resp, resp_len);
        resp[resp_len++] = crc & 0xFF;
        resp[resp_len++] = (crc >> 8) & 0xFF;

        /* 发送响应 */
        vSendData(inst->serial.usart, resp, resp_len);
    }

    return 1;
}

/* ===== 注册回调 ===== */
void eMBRegisterInputCB_Inst(MBInstance_t *inst, MBRegInputCB_t cb)
{
    if (inst != NULL) {
        inst->reg_input_cb = cb;
    }
}

void eMBRegisterHoldingCB_Inst(MBInstance_t *inst, MBRegHoldingCB_t cb)
{
    if (inst != NULL) {
        inst->reg_holding_cb = cb;
    }
}

void eMBSetUserData_Inst(MBInstance_t *inst, void *user_data)
{
    if (inst != NULL) {
        inst->user_data = user_data;
    }
}

void *eMBGetUserData_Inst(MBInstance_t *inst)
{
    return (inst != NULL) ? inst->user_data : NULL;
}

/* ===== 功能码处理函数 ===== */

/**
 * @brief 读保持寄存器（0x03）
 * @note  请求帧：地址(1) + 功能码(1) + 起始地址(2) + 寄存器数量(2) + CRC(2)
 *        响应帧：地址(1) + 功能码(1) + 字节数(1) + 数据(N) + CRC(2)
 */
static uint16_t mb_func_read_holding(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp)
{
    uint16_t start_addr;
    uint16_t reg_count;
    uint8_t exception;

    if (req_len < 8) {
        /* 帧长度错误 */
        resp[0] = inst->rcv_address;
        resp[1] = 0x83;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    start_addr = ((uint16_t)req[2] << 8) | req[3];
    reg_count = ((uint16_t)req[4] << 8) | req[5];

    /* 寄存器数量检查 */
    if (reg_count == 0 || reg_count > 125) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x83;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    resp[0] = inst->rcv_address;
    resp[1] = MB_FUNC_READ_HOLDING;
    resp[2] = reg_count * 2;

    /* 调用用户回调填充数据 */
    if (inst->reg_holding_cb != NULL) {
        exception = inst->reg_holding_cb(inst, &resp[3], start_addr, reg_count, MB_REG_READ);
        if (exception != 0) {
            /* 错误：返回异常响应 */
            resp[1] = 0x83;
            resp[2] = exception;
            return 3;
        }
    } else {
        /* 无回调，返回异常 */
        resp[1] = 0x83;
        resp[2] = MB_EX_ILLEGAL_ADDRESS;
        return 3;
    }

    return 3 + reg_count * 2;
}

/**
 * @brief 读输入寄存器（0x04）
 * @note  请求帧：地址(1) + 功能码(1) + 起始地址(2) + 寄存器数量(2) + CRC(2)
 *        响应帧：地址(1) + 功能码(1) + 字节数(1) + 数据(N) + CRC(2)
 */
static uint16_t mb_func_read_input(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp)
{
    uint16_t start_addr;
    uint16_t reg_count;
    uint8_t exception;

    if (req_len < 8) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x84;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    start_addr = ((uint16_t)req[2] << 8) | req[3];
    reg_count = ((uint16_t)req[4] << 8) | req[5];

    if (reg_count == 0 || reg_count > 125) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x84;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    resp[0] = inst->rcv_address;
    resp[1] = MB_FUNC_READ_INPUT;
    resp[2] = reg_count * 2;

    if (inst->reg_input_cb != NULL) {
        exception = inst->reg_input_cb(inst, &resp[3], start_addr, reg_count);
        if (exception != 0) {
            resp[1] = 0x84;
            resp[2] = exception;
            return 3;
        }
    } else {
        resp[1] = 0x84;
        resp[2] = MB_EX_ILLEGAL_ADDRESS;
        return 3;
    }

    return 3 + reg_count * 2;
}

/**
 * @brief 写单个寄存器（0x06）
 * @note  请求帧：地址(1) + 功能码(1) + 寄存器地址(2) + 寄存器值(2) + CRC(2)
 *        响应帧：原样返回请求帧
 */
static uint16_t mb_func_write_single(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp)
{
    uint16_t reg_addr;
    uint8_t temp[2];
    uint8_t exception;

    if (req_len < 8) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x86;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    reg_addr = ((uint16_t)req[2] << 8) | req[3];

    /* 调用回调写入 */
    temp[0] = req[4];
    temp[1] = req[5];

    if (inst->reg_holding_cb != NULL) {
        exception = inst->reg_holding_cb(inst, temp, reg_addr, 1, MB_REG_WRITE);
        if (exception != 0) {
            resp[0] = inst->rcv_address;
            resp[1] = 0x86;
            resp[2] = exception;
            return 3;
        }
    } else {
        resp[0] = inst->rcv_address;
        resp[1] = 0x86;
        resp[2] = MB_EX_ILLEGAL_ADDRESS;
        return 3;
    }

    /* 原样返回（不含CRC） */
    memcpy(resp, req, 6);
    resp[0] = inst->rcv_address;
    return 6;
}

/**
 * @brief 写多个寄存器（0x10）
 * @note  请求帧：地址(1) + 功能码(1) + 起始地址(2) + 寄存器数量(2) + 字节数(1) + 数据(N) + CRC(2)
 *        响应帧：地址(1) + 功能码(1) + 起始地址(2) + 寄存器数量(2) + CRC(2)
 */
static uint16_t mb_func_write_multiple(MBInstance_t *inst,
    uint8_t *req, uint16_t req_len, uint8_t *resp)
{
    uint16_t start_addr;
    uint16_t reg_count;
    uint8_t byte_count;
    uint8_t exception;

    if (req_len < 9) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x90;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    start_addr = ((uint16_t)req[2] << 8) | req[3];
    reg_count = ((uint16_t)req[4] << 8) | req[5];
    byte_count = req[6];

    /* 检查数据长度一致性 */
    if (byte_count != reg_count * 2 || reg_count == 0 || reg_count > 123) {
        resp[0] = inst->rcv_address;
        resp[1] = 0x90;
        resp[2] = MB_EX_ILLEGAL_VALUE;
        return 3;
    }

    if (inst->reg_holding_cb != NULL) {
        exception = inst->reg_holding_cb(inst, &req[7], start_addr, reg_count, MB_REG_WRITE);
        if (exception != 0) {
            resp[0] = inst->rcv_address;
            resp[1] = 0x90;
            resp[2] = exception;
            return 3;
        }
    } else {
        resp[0] = inst->rcv_address;
        resp[1] = 0x90;
        resp[2] = MB_EX_ILLEGAL_ADDRESS;
        return 3;
    }

    /* 返回：地址 + 功能码 + 起始地址 + 寄存器数量 */
    resp[0] = inst->rcv_address;
    resp[1] = MB_FUNC_WRITE_MULTIPLE;
    resp[2] = req[2];
    resp[3] = req[3];
    resp[4] = req[4];
    resp[5] = req[5];
    return 6;
}

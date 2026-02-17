/**
 * @file mb_instance.h
 * @brief Modbus RTU 多实例上下文结构体定义
 * @note  支持单份代码运行多个Modbus从站实例
 */

#ifndef _MB_INSTANCE_H
#define _MB_INSTANCE_H

#include <stdint.h>
#include <string.h>
#include "at32f403a_407.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 类型定义 ===== */

/* Modbus实例状态枚举 */
typedef enum {
    MB_STATE_NOT_INITIALIZED = 0,
    MB_STATE_DISABLED,
    MB_STATE_ENABLED,
} MBState_t;

/* Modbus实例模式 */
typedef enum {
    MB_MODE_NORMAL = 0,       /* 正常模式：检查站号匹配 */
    MB_MODE_BROADCAST,        /* 广播模式：忽略站号，使用请求帧站号应答 */
} MBMode_t;

/* 寄存器操作模式 */
#define MB_REG_READ     0
#define MB_REG_WRITE    1

/* Modbus异常码 */
#define MB_EX_NONE              0x00
#define MB_EX_ILLEGAL_FUNCTION  0x01
#define MB_EX_ILLEGAL_ADDRESS   0x02
#define MB_EX_ILLEGAL_VALUE     0x03
#define MB_EX_SLAVE_DEVICE_FAIL 0x04

/* 功能码定义 */
#define MB_FUNC_READ_HOLDING    0x03
#define MB_FUNC_READ_INPUT      0x04
#define MB_FUNC_WRITE_SINGLE    0x06
#define MB_FUNC_WRITE_MULTIPLE  0x10

/* ===== 端口层结构体 ===== */

/* 串口实例 */
typedef struct {
    usart_type *usart;          /* 串口外设指针 */
    uint8_t *rcv_buf;           /* 接收缓冲区指针 */
    uint16_t rcv_len;           /* 接收长度 */
    uint16_t rcv_pos;           /* 当前读取位置 */
} MBPortSerial_t;

/* 事件实例 */
typedef struct {
    uint8_t event_in_queue;     /* 事件标志 */
    uint8_t queued_event;       /* 排队的事件类型 */
} MBPortEvent_t;

/* ===== Modbus实例上下文 ===== */

/* 前向声明 */
struct MBInstance;

/* 寄存器回调函数类型 */
typedef uint8_t (*MBRegInputCB_t)(struct MBInstance *inst, uint8_t *buf,
                                   uint16_t addr, uint16_t nregs);
typedef uint8_t (*MBRegHoldingCB_t)(struct MBInstance *inst, uint8_t *buf,
                                     uint16_t addr, uint16_t nregs, uint8_t mode);

/* Modbus实例上下文 - 所有状态封装于此 */
typedef struct MBInstance {
    /* 基本配置 */
    uint8_t address;            /* 从站地址 */
    MBState_t state;            /* 实例状态 */
    MBMode_t mode;              /* 工作模式 */

    /* 帧处理 */
    uint8_t rcv_address;        /* 接收到的站号（用于广播模式应答） */

    /* 端口层实例 */
    MBPortSerial_t serial;
    MBPortEvent_t event;

    /* 寄存器回调函数指针 */
    MBRegInputCB_t reg_input_cb;
    MBRegHoldingCB_t reg_holding_cb;

    /* 用户数据指针（可选，用于回调中访问特定数据） */
    void *user_data;

} MBInstance_t;

#ifdef __cplusplus
}
#endif

#endif /* _MB_INSTANCE_H */

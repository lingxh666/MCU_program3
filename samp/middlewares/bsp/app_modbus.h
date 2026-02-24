/**
 * @file    app_modbus.h
 * @brief   Modbus从站协议栈接口（支持多协议变体）
 */
#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 协议变体 */
#define PROTO_DAYUE     0   /* 大岳 */
#define PROTO_DAHU      1   /* 大湖 */
#define PROTO_SICHUAN   2   /* 四川 */
#define PROTO_XIAN      3   /* 西安 */

/* Modbus功能码 */
#define MB_FUNC_READ_HOLDING    0x03
#define MB_FUNC_READ_INPUT      0x04
#define MB_FUNC_WRITE_SINGLE    0x06
#define MB_FUNC_WRITE_MULTIPLE  0x10

/* 寄存器数量 */
#define MB_HOLDING_REG_COUNT    128
#define MB_INPUT_REG_COUNT      128

/* 初始化（设置协议变体和从站地址） */
void modbus_init(uint8_t protocol, uint8_t slave_addr);

/* 处理接收帧（解析并生成应答，返回应答长度，0=无应答） */
uint16_t modbus_poll(const uint8_t *frame, uint16_t len,
                     uint8_t *resp, uint16_t resp_max);

/* 寄存器读写（供内部模块同步数据） */
uint16_t modbus_get_holding(uint16_t addr);
void     modbus_set_holding(uint16_t addr, uint16_t value);
uint16_t modbus_get_input(uint16_t addr);
void     modbus_set_input(uint16_t addr, uint16_t value);

/* 同步系统状态到输入寄存器（Task04周期调用） */
void modbus_sync_status(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_MODBUS_H */

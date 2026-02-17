/**
 * @file mb.h
 * @brief Modbus RTU 多实例API声明
 * @note  精简版FreeModbus，支持0x03/0x04/0x06/0x10功能码
 */

#ifndef _MB_H
#define _MB_H

#include "mb_instance.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 实例化API ===== */

/**
 * @brief 初始化Modbus实例
 * @param inst 实例指针
 * @param address 从站地址（广播模式下可设为0）
 * @param usart 串口外设指针
 * @param mode 工作模式：MB_MODE_NORMAL 或 MB_MODE_BROADCAST
 * @return 0=失败, 1=成功
 */
uint8_t eMBInit_Inst(MBInstance_t *inst, uint8_t address,
                     usart_type *usart, MBMode_t mode);

/**
 * @brief 使能Modbus实例
 * @param inst 实例指针
 * @return 0=失败, 1=成功
 */
uint8_t eMBEnable_Inst(MBInstance_t *inst);

/**
 * @brief 禁用Modbus实例
 * @param inst 实例指针
 * @return 0=失败, 1=成功
 */
uint8_t eMBDisable_Inst(MBInstance_t *inst);

/**
 * @brief 设置接收到的帧数据
 * @param inst 实例指针
 * @param data 数据指针
 * @param len 数据长度
 * @note  由DMA+IDLE接收完成后调用
 */
void eMBSetRcvFrame_Inst(MBInstance_t *inst, uint8_t *data, uint16_t len);

/**
 * @brief 轮询处理（调用后自动发送响应）
 * @param inst 实例指针
 * @return 0=无事件/未处理, 1=已处理并发送响应
 */
uint8_t eMBPoll_Inst(MBInstance_t *inst);

/**
 * @brief 注册输入寄存器回调（功能码0x04）
 * @param inst 实例指针
 * @param cb 回调函数
 */
void eMBRegisterInputCB_Inst(MBInstance_t *inst, MBRegInputCB_t cb);

/**
 * @brief 注册保持寄存器回调（功能码0x03/0x06/0x10）
 * @param inst 实例指针
 * @param cb 回调函数
 */
void eMBRegisterHoldingCB_Inst(MBInstance_t *inst, MBRegHoldingCB_t cb);

/**
 * @brief 设置用户数据指针
 * @param inst 实例指针
 * @param user_data 用户数据指针
 */
void eMBSetUserData_Inst(MBInstance_t *inst, void *user_data);

/**
 * @brief 获取用户数据指针
 * @param inst 实例指针
 * @return 用户数据指针
 */
void *eMBGetUserData_Inst(MBInstance_t *inst);

/* ===== 协议回调注册函数 ===== */

/**
 * @brief 注册大岳协议回调函数
 * @param inst Modbus实例指针
 */
void dayue_register_callbacks(MBInstance_t *inst);

/**
 * @brief 注册大湖协议回调函数
 * @param inst Modbus实例指针
 */
void dahu_register_callbacks(MBInstance_t *inst);

/**
 * @brief 注册四川管控协议回调函数
 * @param inst Modbus实例指针
 */
void sichuan_register_callbacks(MBInstance_t *inst);

/**
 * @brief 注册西安协议回调函数
 * @param inst Modbus实例指针
 */
void xian_register_callbacks(MBInstance_t *inst);

#ifdef __cplusplus
}
#endif

#endif /* _MB_H */

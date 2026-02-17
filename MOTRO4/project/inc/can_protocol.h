/**
  ***************************************************************************
  * @file     can_protocol.h
  * @brief    CAN通信协议头文件
  *           控制帧: 0x200+motor_id  (主机→设备)
  *           查询帧: 0x300           (主机→设备)
  *           应答帧: 0x100+motor_id  (设备→主机)
  ***************************************************************************
  */

#ifndef __CAN_PROTOCOL_H
#define __CAN_PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f422_426.h"
#include <stdint.h>

/* CAN帧ID定义 */
#define CAN_ID_STATUS_BASE      0x100   /* 状态应答: 0x100-0x103 */
#define CAN_ID_CONTROL_BASE     0x200   /* 电机控制: 0x200-0x203 */
#define CAN_ID_QUERY            0x300   /* 查询所有电机状态 */

/* 控制命令定义 */
#define CAN_CMD_NOP             0x00
#define CAN_CMD_RUN             0x01
#define CAN_CMD_STOP            0x02
#define CAN_CMD_IMMEDIATE_STOP  0x03

/* 控制帧数据格式 (8字节)
 * Byte 0:   命令 (CAN_CMD_xxx)
 * Byte 1:   方向 (0=CW, 1=CCW)
 * Byte 2-3: 目标RPM (大端)
 * Byte 4-5: 加速度 RPM/s (大端, 0=不修改)
 * Byte 6-7: 保留
 */

/* 应答帧数据格式 (8字节)
 * Byte 0:   运行状态 (motor_state_t)
 * Byte 1:   方向
 * Byte 2-3: 当前RPM (大端)
 * Byte 4-5: 电流mA (大端)
 * Byte 6:   监测状态 (monitor_status_t)
 * Byte 7:   故障码
 */

/* 函数声明 */
void CanProtocolInit(void);
void CanProtocol_OnRxISR(void);
void CanProtocolProcess(void);
void CanProtocolSendAllStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __CAN_PROTOCOL_H */

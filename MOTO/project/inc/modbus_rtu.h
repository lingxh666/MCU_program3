/**
  ***************************************************************************
  * @file     modbus_rtu.h
  * @brief    Modbus RTU slave header file for dual stepper motor control
  ***************************************************************************
  */

#ifndef __MODBUS_RTU_H
#define __MODBUS_RTU_H

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f421.h"
#include <stdint.h>

/* Modbus function codes */
#define MODBUS_FC_READ_HOLDING_REGISTERS    0x03
#define MODBUS_FC_WRITE_SINGLE_REGISTER     0x06
#define MODBUS_FC_WRITE_MULTIPLE_REGISTERS  0x10

/* Modbus exception codes */
typedef enum {
    MODBUS_EX_NONE = 0x00,
    MODBUS_EX_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EX_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_EX_ILLEGAL_DATA_VALUE = 0x03,
    MODBUS_EX_SLAVE_DEVICE_FAILURE = 0x04
} modbus_exception_t;

/* Motor control commands */
#define CMD_NOOP            0x0000
#define CMD_RUN             0x0001
#define CMD_STOP            0x0002
#define CMD_IMMEDIATE_STOP  0x0003

/* Holding register address definitions */
/* Motor 1 control registers (0x0000-0x0003) */
#define REG_M1_TARGET_RPM       0x0000
#define REG_M1_DIRECTION        0x0001
#define REG_M1_COMMAND          0x0002
#define REG_M1_ACCELERATION     0x0003

/* Motor 2 control registers (0x0004-0x0007) */
#define REG_M2_TARGET_RPM       0x0004
#define REG_M2_DIRECTION        0x0005
#define REG_M2_COMMAND          0x0006
#define REG_M2_ACCELERATION     0x0007

/* System configuration registers (0x0040) */
#define REG_SLAVE_ADDR          0x0040

/* Motor 1 status registers (0x0100-0x0104) */
#define REG_M1_CURRENT_RPM      0x0100
#define REG_M1_CURRENT_MA       0x0101
#define REG_M1_STATE            0x0102
#define REG_M1_STATUS           0x0103
#define REG_M1_FAULT_CODE       0x0104

/* Motor 2 status registers (0x0105-0x0109) */
#define REG_M2_CURRENT_RPM      0x0105
#define REG_M2_CURRENT_MA       0x0106
#define REG_M2_STATE            0x0107
#define REG_M2_STATUS           0x0108
#define REG_M2_FAULT_CODE       0x0109

/* Register count definition */
#define HOLDING_REG_COUNT       0x0110  /* Max register address + 1 */

/* Modbus frame structure */
typedef struct {
    uint8_t slave_addr;
    uint8_t function_code;
    uint16_t reg_addr;
    uint16_t reg_count_or_value;
} modbus_frame_t;

/* Function prototypes */

/**
 * @brief  initialize Modbus RTU slave
 * @param  none
 * @retval none
 * @note   detects slave address from PA1 pin, initializes holding registers
 */
void Modbus_Init(void);

/**
 * @brief  process received Modbus frame (call in main loop)
 * @param  none
 * @retval none
 * @note   non-blocking, checks len variable for received data
 */
void Modbus_Process(void);

/**
 * @brief  update holding registers from motor status (call every 1 second)
 * @param  none
 * @retval none
 * @note   syncs motor RPM, current, state, fault info to holding registers
 */
void Modbus_UpdateHoldingRegs(void);

/**
 * @brief  get current slave address
 * @param  none
 * @retval slave address (1 or 2)
 */
uint8_t Modbus_GetSlaveAddr(void);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_RTU_H */

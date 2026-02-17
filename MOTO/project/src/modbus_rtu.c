/**
  ***************************************************************************
  * @file     modbus_rtu.c
  * @brief    Modbus RTU slave implementation for dual stepper motor control
  ***************************************************************************
  */

#include "modbus_rtu.h"
#include "at32f421_wk_config.h"
#include "stepper_motor.h"
#include "motor_monitor.h"

/* External variables from USART2/DMA reception */
extern uint8_t Buf[100];
extern volatile uint16_t len;

/* External functions for CRC and transmission */
extern uint16_t CRC16_MODBUS(uint8_t *data, uint16_t length);
extern void SendData(uint8_t *buf, uint8_t len);

/* Modbus slave context */
static uint8_t g_slave_addr = 1;
static uint16_t g_holding_regs[HOLDING_REG_COUNT];
static uint8_t g_tx_buf[256];

/* Maximum readable register count (to prevent tx_buf overflow) */
/* Frame: addr(1) + func(1) + byte_cnt(1) + data(reg*2) + crc(2) */
/* Max data bytes = 256 - 5 = 251, so max regs = 125 */
#define MAX_READ_REGS  125

/* Motor control parameter cache (for auto-run on parameter write) */
static uint16_t g_m1_target_rpm = 0;
static uint16_t g_m1_direction = MOTOR_DIR_CW;
static uint16_t g_m2_target_rpm = 0;
static uint16_t g_m2_direction = MOTOR_DIR_CW;

/**
 * @brief  initialize Modbus RTU slave
 * @param  none
 * @retval none
 */
void Modbus_Init(void)
{
  uint16_t i;

  /* Detect slave address from PA1 pin */
  /* PA1 high = slave address 0x01, low = 0x02 */
  if (gpio_input_data_bit_read(GPIOA, GPIO_PINS_1))
  {
    g_slave_addr = 0x01;
  }
  else
  {
    g_slave_addr = 0x02;
  }

  /* Initialize holding registers to default values */
  for (i = 0; i < HOLDING_REG_COUNT; i++)
  {
    g_holding_regs[i] = 0;
  }

  /* Set default motor parameters */
  g_holding_regs[REG_M1_ACCELERATION] = 500;  /* 500 RPM/s */
  g_holding_regs[REG_M2_ACCELERATION] = 500;
  g_holding_regs[REG_SLAVE_ADDR] = g_slave_addr;

  /* Initialize cache */
  g_m1_target_rpm = 0;
  g_m1_direction = MOTOR_DIR_CW;
  g_m2_target_rpm = 0;
  g_m2_direction = MOTOR_DIR_CW;
}

/**
 * @brief  get current slave address
 * @param  none
 * @retval slave address (1 or 2)
 */
uint8_t Modbus_GetSlaveAddr(void)
{
  return g_slave_addr;
}

/**
 * @brief  build exception response frame
 * @param  slave_addr: slave address
 * @param  function_code: function code + 0x80 for exception
 * @param  exception_code: exception code
 * @param  tx_buf: transmit buffer
 * @retval frame length
 */
static uint16_t BuildExceptionResponse(uint8_t slave_addr, uint8_t function_code,
                                       uint8_t exception_code, uint8_t *tx_buf)
{
  uint16_t crc;

  tx_buf[0] = slave_addr;
  tx_buf[1] = function_code | 0x80;
  tx_buf[2] = exception_code;

  crc = CRC16_MODBUS(tx_buf, 3);
  tx_buf[3] = crc & 0xFF;
  tx_buf[4] = (crc >> 8) & 0xFF;

  return 5;
}

/**
 * @brief  process read holding registers (function code 0x03)
 * @param  frame: Modbus frame structure
 * @param  rx_buf: receive buffer
 * @param  tx_buf: transmit buffer
 * @param  rx_len: receive length
 * @retval response length
 */
static uint16_t ProcessReadHoldingRegs(modbus_frame_t *frame, uint8_t *rx_buf,
                                       uint8_t *tx_buf, uint16_t rx_len)
{
  uint16_t start_addr, reg_count, byte_count;
  uint16_t i, crc, tx_len;
  uint16_t data;

  start_addr = ((rx_buf[2] << 8) | rx_buf[3]);
  reg_count = ((rx_buf[4] << 8) | rx_buf[5]);

  /* Validate register count (prevent tx_buf overflow) */
  if (reg_count > MAX_READ_REGS)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
  }

  /* Validate register range */
  if ((start_addr + reg_count) > HOLDING_REG_COUNT)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Build response */
  byte_count = reg_count * 2;
  tx_buf[0] = frame->slave_addr;
  tx_buf[1] = frame->function_code;
  tx_buf[2] = byte_count;

  /* Copy register data */
  for (i = 0; i < reg_count; i++)
  {
    data = g_holding_regs[start_addr + i];
    tx_buf[3 + i * 2] = (data >> 8) & 0xFF;
    tx_buf[4 + i * 2] = data & 0xFF;
  }

  /* Calculate CRC */
  tx_len = 3 + byte_count;
  crc = CRC16_MODBUS(tx_buf, tx_len);
  tx_buf[tx_len] = crc & 0xFF;
  tx_buf[tx_len + 1] = (crc >> 8) & 0xFF;

  return tx_len + 2;
}

/**
 * @brief  execute motor command
 * @param  motor_id: motor id (0 or 1)
 * @param  command: command code
 * @retval none
 */
static void ExecuteMotorCommand(uint8_t motor_id, uint16_t command)
{
  uint16_t target_rpm;
  uint16_t direction;
  motor_dir_t dir;
  motor_id_t mid;

  if (motor_id >= 2)
    return;

  mid = (motor_id == 0) ? MOTOR_ID_1 : MOTOR_ID_2;

  /* Get cached parameters */
  if (motor_id == 0)
  {
    target_rpm = g_m1_target_rpm;
    direction = g_m1_direction;
  }
  else
  {
    target_rpm = g_m2_target_rpm;
    direction = g_m2_direction;
  }

  dir = (direction == 0) ? MOTOR_DIR_CW : MOTOR_DIR_CCW;

  switch (command)
  {
    case CMD_RUN:
      if (target_rpm > 0)
      {
        MotorRun(mid, target_rpm, dir);
      }
      break;

    case CMD_STOP:
      MotorStop(mid);
      break;

    case CMD_IMMEDIATE_STOP:
      MotorImmediateStop(mid);
      break;

    case CMD_NOOP:
    default:
      /* Do nothing */
      break;
  }
}

/**
 * @brief  process write single register (function code 0x06)
 * @param  frame: Modbus frame structure
 * @param  rx_buf: receive buffer
 * @param  tx_buf: transmit buffer
 * @retval response length
 */
static uint16_t ProcessWriteSingleReg(modbus_frame_t *frame, uint8_t *rx_buf,
                                      uint8_t *tx_buf)
{
  uint16_t reg_addr, reg_value;
  uint16_t crc;

  reg_addr = ((rx_buf[2] << 8) | rx_buf[3]);
  reg_value = ((rx_buf[4] << 8) | rx_buf[5]);

  /* Validate register address */
  if (reg_addr >= HOLDING_REG_COUNT)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Write-protected registers: status registers (0x0100+) */
  if (reg_addr >= 0x0100)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Validate register values */
  if (reg_addr == REG_M1_TARGET_RPM || reg_addr == REG_M2_TARGET_RPM)
  {
    /* RPM range: 1-1000 */
    if (reg_value < 1 || reg_value > 1000)
    {
      return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                    MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
    }
  }

  if (reg_addr == REG_M1_DIRECTION || reg_addr == REG_M2_DIRECTION)
  {
    /* Direction: 0 (CW) or 1 (CCW) */
    if (reg_value > 1)
    {
      return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                    MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
    }
  }

  if (reg_addr == REG_M1_COMMAND || reg_addr == REG_M2_COMMAND)
  {
    /* Command: 0-3 */
    if (reg_value > 3)
    {
      return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                    MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
    }
  }

  if (reg_addr == REG_M1_ACCELERATION || reg_addr == REG_M2_ACCELERATION)
  {
    /* Acceleration: 10-2000 RPM/s */
    if (reg_value < 10 || reg_value > 2000)
    {
      return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                    MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
    }
  }

  /* Write register value */
  g_holding_regs[reg_addr] = reg_value;

  /* Handle motor 1 control registers */
  if (reg_addr == REG_M1_TARGET_RPM)
  {
    g_m1_target_rpm = reg_value;
    /* Auto-run if command is CMD_RUN */
    if (g_holding_regs[REG_M1_COMMAND] == CMD_RUN)
    {
      ExecuteMotorCommand(0, CMD_RUN);
    }
  }
  else if (reg_addr == REG_M1_DIRECTION)
  {
    g_m1_direction = reg_value;
    /* Auto-run if command is CMD_RUN */
    if (g_holding_regs[REG_M1_COMMAND] == CMD_RUN)
    {
      ExecuteMotorCommand(0, CMD_RUN);
    }
  }
  else if (reg_addr == REG_M1_COMMAND)
  {
    ExecuteMotorCommand(0, reg_value);
  }
  else if (reg_addr == REG_M1_ACCELERATION)
  {
    MotorSetAcceleration(MOTOR_ID_1, (int16_t)reg_value);
  }

  /* Handle motor 2 control registers */
  if (reg_addr == REG_M2_TARGET_RPM)
  {
    g_m2_target_rpm = reg_value;
    /* Auto-run if command is CMD_RUN */
    if (g_holding_regs[REG_M2_COMMAND] == CMD_RUN)
    {
      ExecuteMotorCommand(1, CMD_RUN);
    }
  }
  else if (reg_addr == REG_M2_DIRECTION)
  {
    g_m2_direction = reg_value;
    /* Auto-run if command is CMD_RUN */
    if (g_holding_regs[REG_M2_COMMAND] == CMD_RUN)
    {
      ExecuteMotorCommand(1, CMD_RUN);
    }
  }
  else if (reg_addr == REG_M2_COMMAND)
  {
    ExecuteMotorCommand(1, reg_value);
  }
  else if (reg_addr == REG_M2_ACCELERATION)
  {
    MotorSetAcceleration(MOTOR_ID_2, (int16_t)reg_value);
  }

  /* Build echo response */
  tx_buf[0] = frame->slave_addr;
  tx_buf[1] = frame->function_code;
  tx_buf[2] = (reg_addr >> 8) & 0xFF;
  tx_buf[3] = reg_addr & 0xFF;
  tx_buf[4] = (reg_value >> 8) & 0xFF;
  tx_buf[5] = reg_value & 0xFF;

  crc = CRC16_MODBUS(tx_buf, 6);
  tx_buf[6] = crc & 0xFF;
  tx_buf[7] = (crc >> 8) & 0xFF;

  return 8;
}

/**
 * @brief  process write multiple registers (function code 0x10)
 * @param  frame: Modbus frame structure
 * @param  rx_buf: receive buffer
 * @param  tx_buf: transmit buffer
 * @retval response length
 */
static uint16_t ProcessWriteMultipleRegs(modbus_frame_t *frame, uint8_t *rx_buf,
                                         uint8_t *tx_buf)
{
  uint16_t start_addr, reg_count, byte_count;
  uint16_t i, reg_addr, reg_value, crc;
  uint8_t *data_ptr;

  start_addr = ((rx_buf[2] << 8) | rx_buf[3]);
  reg_count = ((rx_buf[4] << 8) | rx_buf[5]);
  byte_count = rx_buf[6];
  data_ptr = &rx_buf[7];

  /* Validate byte count matches register count */
  if (byte_count != reg_count * 2)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
  }

  /* Validate register range */
  if ((start_addr + reg_count) > HOLDING_REG_COUNT)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Write-protected registers: status registers (0x0100+) */
  if (start_addr >= 0x0100)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Check if any register crosses into status area */
  if ((start_addr + reg_count) > 0x0100)
  {
    return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                  MODBUS_EX_ILLEGAL_DATA_ADDRESS, tx_buf);
  }

  /* Write registers */
  for (i = 0; i < reg_count; i++)
  {
    reg_addr = start_addr + i;
    reg_value = (data_ptr[i * 2] << 8) | data_ptr[i * 2 + 1];

    /* Validate RPM range */
    if ((reg_addr == REG_M1_TARGET_RPM || reg_addr == REG_M2_TARGET_RPM))
    {
      if (reg_value < 1 || reg_value > 1000)
      {
        return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                      MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
      }
    }

    /* Validate Direction range */
    if ((reg_addr == REG_M1_DIRECTION || reg_addr == REG_M2_DIRECTION))
    {
      if (reg_value > 1)
      {
        return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                      MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
      }
    }

    /* Validate Command range */
    if ((reg_addr == REG_M1_COMMAND || reg_addr == REG_M2_COMMAND))
    {
      if (reg_value > 3)
      {
        return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                      MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
      }
    }

    /* Validate Acceleration range */
    if ((reg_addr == REG_M1_ACCELERATION || reg_addr == REG_M2_ACCELERATION))
    {
      if (reg_value < 10 || reg_value > 2000)
      {
        return BuildExceptionResponse(frame->slave_addr, frame->function_code,
                                      MODBUS_EX_ILLEGAL_DATA_VALUE, tx_buf);
      }
    }

    /* Validate and write */
    g_holding_regs[reg_addr] = reg_value;

    /* Handle motor 1 control registers */
    if (reg_addr == REG_M1_TARGET_RPM)
    {
      g_m1_target_rpm = reg_value;
    }
    else if (reg_addr == REG_M1_DIRECTION)
    {
      g_m1_direction = reg_value;
    }
    else if (reg_addr == REG_M1_ACCELERATION)
    {
      MotorSetAcceleration(MOTOR_ID_1, (int16_t)reg_value);
    }

    /* Handle motor 2 control registers */
    if (reg_addr == REG_M2_TARGET_RPM)
    {
      g_m2_target_rpm = reg_value;
    }
    else if (reg_addr == REG_M2_DIRECTION)
    {
      g_m2_direction = reg_value;
    }
    else if (reg_addr == REG_M2_ACCELERATION)
    {
      MotorSetAcceleration(MOTOR_ID_2, (int16_t)reg_value);
    }
  }

  /* Execute commands for both motors after all registers written */
  if (start_addr <= REG_M1_COMMAND && (start_addr + reg_count) > REG_M1_COMMAND)
  {
    ExecuteMotorCommand(0, g_holding_regs[REG_M1_COMMAND]);
  }
  if (start_addr <= REG_M2_COMMAND && (start_addr + reg_count) > REG_M2_COMMAND)
  {
    ExecuteMotorCommand(1, g_holding_regs[REG_M2_COMMAND]);
  }

  /* Build response */
  tx_buf[0] = frame->slave_addr;
  tx_buf[1] = frame->function_code;
  tx_buf[2] = (start_addr >> 8) & 0xFF;
  tx_buf[3] = start_addr & 0xFF;
  tx_buf[4] = (reg_count >> 8) & 0xFF;
  tx_buf[5] = reg_count & 0xFF;

  crc = CRC16_MODBUS(tx_buf, 6);
  tx_buf[6] = crc & 0xFF;
  tx_buf[7] = (crc >> 8) & 0xFF;

  return 8;
}

/**
 * @brief  process received Modbus frame
 * @param  rx_buf: receive buffer
 * @param  rx_len: receive length
 * @retval response length (0 if no response)
 */
static uint16_t ProcessFrame(uint8_t *rx_buf, uint16_t rx_len)
{
  modbus_frame_t frame;
  uint16_t response_len = 0;
  uint8_t is_broadcast = 0;
  uint8_t byte_count;

  /* Check minimum frame length */
  if (rx_len < 4)
    return 0;

  /* Parse frame header */
  frame.slave_addr = rx_buf[0];
  frame.function_code = rx_buf[1];

  /* Check if this is a broadcast address */
  is_broadcast = (frame.slave_addr == 0x00);

  /* Check slave address */
  /* 0x00 is broadcast - should process command but NOT respond */
  /* Only accept our assigned address or broadcast */
  if (frame.slave_addr != g_slave_addr && !is_broadcast)
    return 0;

  /* Verify frame length by function code */
  switch (frame.function_code)
  {
    case MODBUS_FC_READ_HOLDING_REGISTERS:
      /* minimum: addr(1) + func(1) + start(2) + count(2) + crc(2) = 8 */
      if (rx_len < 8)
        return 0;
      break;

    case MODBUS_FC_WRITE_SINGLE_REGISTER:
      /* minimum: addr(1) + func(1) + addr(2) + value(2) + crc(2) = 8 */
      if (rx_len < 8)
        return 0;
      break;

    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
      /* minimum: addr(1) + func(1) + start(2) + count(2) + byte_cnt(1) + crc(2) = 9 */
      if (rx_len < 9)
        return 0;
      /* validate byte count field */
      byte_count = rx_buf[6];
      if (rx_len < (9 + byte_count))
        return 0;
      break;

    default:
      /* unknown function code - still need minimum length */
      break;
  }

  /* Verify CRC */
  if (!CRCCheck(rx_buf, rx_len))
    return 0;

  /* Dispatch by function code */
  switch (frame.function_code)
  {
    case MODBUS_FC_READ_HOLDING_REGISTERS:
      response_len = ProcessReadHoldingRegs(&frame, rx_buf, g_tx_buf, rx_len);
      break;

    case MODBUS_FC_WRITE_SINGLE_REGISTER:
      response_len = ProcessWriteSingleReg(&frame, rx_buf, g_tx_buf);
      break;

    case MODBUS_FC_WRITE_MULTIPLE_REGISTERS:
      response_len = ProcessWriteMultipleRegs(&frame, rx_buf, g_tx_buf);
      break;

    default:
      /* Illegal function */
      response_len = BuildExceptionResponse(frame.slave_addr, frame.function_code,
                                            MODBUS_EX_ILLEGAL_FUNCTION, g_tx_buf);
      break;
  }

  /* Do not respond to broadcast addresses */
  if (is_broadcast)
    return 0;

  return response_len;
}

/**
 * @brief  process Modbus communication (call in main loop)
 * @param  none
 * @retval none
 */
void Modbus_Process(void)
{
  const uint8_t *rx_buf;
  uint16_t response_len;
  uint16_t rx_len;

  /* Get received data safely (uses double buffering) */
  rx_len = Modbus_GetRxData(&rx_buf);

  if (rx_len == 0)
    return;

  /* Process received frame */
  response_len = ProcessFrame((uint8_t *)rx_buf, rx_len);

  /* Send response if any */
  if (response_len > 0)
  {
    SendData(g_tx_buf, response_len);
  }
}

/**
 * @brief  update holding registers from motor status (call every 1 second)
 * @param  none
 * @retval none
 */
void Modbus_UpdateHoldingRegs(void)
{
  float current_ma;

  /* Motor 1 status */
  g_holding_regs[REG_M1_CURRENT_RPM] = MotorGetSpeedRPM(MOTOR_ID_1);
  g_holding_regs[REG_M1_STATE] = (uint16_t)MotorGetState(MOTOR_ID_1);
  g_holding_regs[REG_M1_STATUS] = (uint16_t)MotorGetStatus(MOTOR_ID_1);
  g_holding_regs[REG_M1_FAULT_CODE] = MotorGetFaultCode(MOTOR_ID_1);

  /* Motor 1 current (convert mA to uint16) */
  current_ma = MotorGetCurrent(MOTOR_ID_1);
  g_holding_regs[REG_M1_CURRENT_MA] = (uint16_t)current_ma;

  /* Motor 2 status */
  g_holding_regs[REG_M2_CURRENT_RPM] = MotorGetSpeedRPM(MOTOR_ID_2);
  g_holding_regs[REG_M2_STATE] = (uint16_t)MotorGetState(MOTOR_ID_2);
  g_holding_regs[REG_M2_STATUS] = (uint16_t)MotorGetStatus(MOTOR_ID_2);
  g_holding_regs[REG_M2_FAULT_CODE] = MotorGetFaultCode(MOTOR_ID_2);

  /* Motor 2 current (convert mA to uint16) */
  current_ma = MotorGetCurrent(MOTOR_ID_2);
  g_holding_regs[REG_M2_CURRENT_MA] = (uint16_t)current_ma;
}

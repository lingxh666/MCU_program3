/**
  ***************************************************************************
  * @file     can_protocol.c
  * @brief    CAN通信协议实现（AT32F422/426 CAN API）
  ***************************************************************************
  */

#include "can_protocol.h"
#include "stepper_motor.h"
#include "motor_monitor.h"
#include <stdio.h>

/* CAN接收环形缓冲 */
#define CAN_RX_QUEUE_SIZE   8
static can_rxbuf_type rx_queue[CAN_RX_QUEUE_SIZE];
static volatile uint8_t rx_head = 0;
static volatile uint8_t rx_tail = 0;

void CanProtocolInit(void)
{
  rx_head = 0;
  rx_tail = 0;

  /* 使能CAN接收中断 */
  can_interrupt_enable(CAN1, CAN_RIE_INT, TRUE);
  can_interrupt_enable(CAN1, CAN_ROIE_INT, TRUE);
  can_interrupt_enable(CAN1, CAN_EIE_INT, TRUE);
}

/**
 * @brief  CAN接收中断回调（CAN1_RX_IRQHandler中调用）
 */
void CanProtocol_OnRxISR(void)
{
  can_rxbuf_type rx_msg;
  uint8_t next;

  while (can_rxbuf_status_get(CAN1) != CAN_RXBUF_STATUS_EMPTY)
  {
    if (can_rxbuf_read(CAN1, &rx_msg) != SUCCESS)
      break;

    can_rxbuf_release(CAN1);
    next = (rx_head + 1) % CAN_RX_QUEUE_SIZE;
    if (next != rx_tail)
    {
      rx_queue[rx_head] = rx_msg;
      rx_head = next;
    }
  }

  /* 清除接收中断标志 */
  can_flag_clear(CAN1, CAN_RIF_FLAG);
  can_flag_clear(CAN1, CAN_ROIF_FLAG);
}

/**
 * @brief  处理控制帧
 */
static void handle_control_frame(uint8_t motor_id, const uint8_t *data)
{
  uint8_t cmd   = data[0];
  uint8_t dir   = data[1];
  uint16_t rpm  = ((uint16_t)data[2] << 8) | data[3];
  uint16_t accel = ((uint16_t)data[4] << 8) | data[5];

  if (motor_id >= MOTOR_COUNT)
    return;

  /* 设置加速度（非零时才修改） */
  if (accel > 0)
    MotorSetAcceleration((motor_id_t)motor_id, (int16_t)accel);

  switch (cmd)
  {
    case CAN_CMD_RUN:
      MotorRun((motor_id_t)motor_id, rpm,
               (dir == 0) ? MOTOR_DIR_CW : MOTOR_DIR_CCW);
      break;
    case CAN_CMD_STOP:
      MotorStop((motor_id_t)motor_id);
      break;
    case CAN_CMD_IMMEDIATE_STOP:
      MotorImmediateStop((motor_id_t)motor_id);
      break;
    default:
      break;
  }
}

/**
 * @brief  发送单个电机状态应答帧
 */
static void send_status_frame(uint8_t motor_id, can_txbuf_select_type txbuf)
{
  can_txbuf_type tx_msg;
  uint16_t rpm;
  uint16_t current_ma;

  if (motor_id >= MOTOR_COUNT)
    return;

  rpm        = MotorGetSpeedRPM((motor_id_t)motor_id);
  current_ma = MotorGetCurrentMA(motor_id);

  tx_msg.id_type      = CAN_ID_STANDARD;
  tx_msg.frame_type   = CAN_FRAME_DATA;
  tx_msg.id           = CAN_ID_STATUS_BASE + motor_id;
  tx_msg.data_length  = CAN_DLC_BYTES_8;
  tx_msg.tx_timestamp = FALSE;
  tx_msg.handle       = 0;

  tx_msg.data[0] = (uint8_t)MotorGetState((motor_id_t)motor_id);
  tx_msg.data[1] = (uint8_t)MotorGetDirection((motor_id_t)motor_id);
  tx_msg.data[2] = (uint8_t)(rpm >> 8);
  tx_msg.data[3] = (uint8_t)(rpm & 0xFF);
  tx_msg.data[4] = (uint8_t)(current_ma >> 8);
  tx_msg.data[5] = (uint8_t)(current_ma & 0xFF);
  tx_msg.data[6] = (uint8_t)MotorGetMonitorStatus(motor_id);
  tx_msg.data[7] = MotorGetFaultCode(motor_id);

  can_txbuf_write(CAN1, txbuf, &tx_msg);
}

/**
 * @brief  主循环中处理CAN接收队列
 */
void CanProtocolProcess(void)
{
  can_rxbuf_type msg;
  uint32_t id;

  while (rx_tail != rx_head)
  {
    msg = rx_queue[rx_tail];
    rx_tail = (rx_tail + 1) % CAN_RX_QUEUE_SIZE;
    id = msg.id;
    printf("CAN RX: ID=0x%03X DLC=%d\r\n", id, msg.data_length);

    /* 控制帧: 0x200-0x203 */
    if (id >= CAN_ID_CONTROL_BASE && id < (CAN_ID_CONTROL_BASE + MOTOR_COUNT))
    {
      handle_control_frame((uint8_t)(id - CAN_ID_CONTROL_BASE), msg.data);
    }
    /* 查询帧: 0x300 */
    else if (id == CAN_ID_QUERY)
    {
      CanProtocolSendAllStatus();
    }
  }
}

/**
 * @brief  发送所有电机状态
 */
void CanProtocolSendAllStatus(void)
{
  uint8_t i;
  for (i = 0; i < MOTOR_COUNT - 1; i++)
    send_status_frame(i, CAN_TXBUF_STB);
  can_txbuf_transmit(CAN1, CAN_TRANSMIT_STB_ALL);
  /* 第4帧用PTB发送 */
  send_status_frame(MOTOR_COUNT - 1, CAN_TXBUF_PTB);
  can_txbuf_transmit(CAN1, CAN_TRANSMIT_PTB);
}

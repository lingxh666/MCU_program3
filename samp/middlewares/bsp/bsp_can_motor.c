#include "bsp_can_motor.h"
#include <string.h>

/* 电机状态缓存 */
static motor_status_t motor_status[MOTOR_COUNT];

/* 发送CAN帧 */
static uint8_t can_send_frame(uint16_t id, const uint8_t *data, uint8_t len)
{
  can_tx_message_type tx_msg;

  tx_msg.standard_id = id;
  tx_msg.extended_id = 0;
  tx_msg.id_type     = CAN_ID_STANDARD;
  tx_msg.frame_type  = CAN_TFT_DATA;
  tx_msg.dlc         = len;
  memcpy(tx_msg.data, data, len);

  return can_message_transmit(CAN1, &tx_msg) != CAN_TX_STATUS_NO_EMPTY;
}

/* 发送简单命令（电机ID + 命令码） */
static void send_simple_cmd(uint8_t motor_id, uint8_t cmd)
{
  uint8_t data[2];
  data[0] = motor_id;
  data[1] = cmd;
  can_send_frame(CAN_ID_MOTOR_TX, data, 2);
}

void can_motor_init(void)
{
  memset(motor_status, 0, sizeof(motor_status));
  can_interrupt_enable(CAN1, CAN_RF0MIEN_INT, TRUE);
  nvic_irq_enable(CAN1_RX0_IRQn, 5, 0);
}

void can_motor_set_speed(uint8_t motor_id, uint16_t speed, uint8_t direction)
{
  uint8_t data[5];
  if(motor_id >= MOTOR_COUNT) return;

  data[0] = motor_id;
  data[1] = MOTOR_CMD_SET_SPEED;
  data[2] = (uint8_t)(speed >> 8);
  data[3] = (uint8_t)(speed & 0xFF);
  data[4] = direction;
  can_send_frame(CAN_ID_MOTOR_TX, data, 5);
}

void can_motor_stop(uint8_t motor_id)
{
  if(motor_id >= MOTOR_COUNT) return;
  send_simple_cmd(motor_id, MOTOR_CMD_STOP);
}

void can_motor_start(uint8_t motor_id)
{
  if(motor_id >= MOTOR_COUNT) return;
  send_simple_cmd(motor_id, MOTOR_CMD_START);
}

void can_motor_lock_panel(void)
{
  send_simple_cmd(0xFF, MOTOR_CMD_LOCK_PANEL);  /* 广播地址 */
}

motor_status_t can_motor_get_status(uint8_t motor_id)
{
  motor_status_t empty = {0};
  if(motor_id >= MOTOR_COUNT) return empty;
  return motor_status[motor_id];
}

/* CAN1 FIFO0接收中断中调用 */
void can_motor_rx_irq(void)
{
  can_rx_message_type rx_msg;
  uint8_t mid;

  while(can_receive_message_pending_get(CAN1, CAN_RX_FIFO0) > 0)
  {
    can_message_receive(CAN1, CAN_RX_FIFO0, &rx_msg);

    if(rx_msg.standard_id != CAN_ID_MOTOR_RX || rx_msg.dlc < 5)
      continue;

    mid = rx_msg.data[0];
    if(mid >= MOTOR_COUNT)
      continue;

    motor_status[mid].speed     = ((uint16_t)rx_msg.data[1] << 8) | rx_msg.data[2];
    motor_status[mid].direction = rx_msg.data[3];
    motor_status[mid].running   = rx_msg.data[4];
    if(rx_msg.dlc >= 6)
      motor_status[mid].error = rx_msg.data[5];
  }
}

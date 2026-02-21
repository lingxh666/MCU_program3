#ifndef BSP_CAN_MOTOR_H
#define BSP_CAN_MOTOR_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CAN ID定义 */
#define CAN_ID_MOTOR_TX       0x100   /* 主控→电机命令 */
#define CAN_ID_MOTOR_RX       0x200   /* 电机→主控状态 */
#define CAN_ID_HX711          0x300   /* 预留：HX711称重 */
#define CAN_ID_NFC            0x400   /* 预留：NFC */

/* 电机数量 */
#define MOTOR_COUNT           4

/* 电机命令 */
#define MOTOR_CMD_SET_SPEED   0x01
#define MOTOR_CMD_SET_DIR     0x02
#define MOTOR_CMD_START       0x03
#define MOTOR_CMD_STOP        0x04
#define MOTOR_CMD_GET_STATUS  0x05
#define MOTOR_CMD_LOCK_PANEL  0x06

/* 电机方向 */
#define MOTOR_DIR_CW          0x00    /* 顺时针 */
#define MOTOR_DIR_CCW         0x01    /* 逆时针 */

/* 电机状态 */
typedef struct {
  uint16_t speed;       /* 当前转速 RPM×10 */
  uint8_t  direction;   /* 当前方向 */
  uint8_t  running;     /* 运行中标志 */
  uint8_t  error;       /* 故障码 */
} motor_status_t;

void can_motor_init(void);
void can_motor_set_speed(uint8_t motor_id, uint16_t speed, uint8_t direction);
void can_motor_stop(uint8_t motor_id);
void can_motor_start(uint8_t motor_id);
void can_motor_lock_panel(void);
motor_status_t can_motor_get_status(uint8_t motor_id);
void can_motor_rx_irq(void);

/* 通用CAN接收 */
#define CAN_RX_BUF_SIZE  16

typedef struct {
  uint16_t id;
  uint8_t  dlc;
  uint8_t  data[8];
} can_rx_frame_t;

uint8_t can_rx_get(can_rx_frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif

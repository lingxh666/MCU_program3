#ifndef BSP_CRC_H
#define BSP_CRC_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 硬件CRC16 Modbus计算（poly=0x8005, init=0xFFFF） */
uint16_t crc16_modbus(uint8_t *data, uint16_t length);

/* CRC16校验（检查数据末尾2字节CRC是否正确） */
uint8_t crc16_check(uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* BSP_CRC_H */

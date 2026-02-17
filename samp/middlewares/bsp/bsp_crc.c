#include "bsp_crc.h"

/* 硬件CRC16 Modbus: 逐字节写入CRC->dt寄存器 */
uint16_t crc16_modbus(uint8_t *data, uint16_t length)
{
  uint16_t i;
  crc_data_reset();
  for(i = 0; i < length; i++)
  {
    (*(uint8_t *)&CRC->dt) = data[i];
  }
  return (uint16_t)(CRC->dt);
}

/* 校验数据末尾2字节CRC（小端序：低字节在前） */
uint8_t crc16_check(uint8_t *data, uint16_t length)
{
  uint16_t calc_crc, recv_crc;
  if(length < 4) return 0;
  calc_crc = crc16_modbus(data, length - 2);
  recv_crc = (uint16_t)((data[length - 1] << 8) | data[length - 2]);
  return (calc_crc == recv_crc) ? 1 : 0;
}

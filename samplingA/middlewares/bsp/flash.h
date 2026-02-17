#ifndef __FLASH_H__
#define __FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f403a_407.h"

void flash_read(uint32_t read_addr, uint16_t *p_buffer, uint16_t num_read);
error_status flash_write_nocheck(uint32_t write_addr, uint16_t *p_buffer, uint16_t num_write);
error_status flash_write(uint32_t write_addr,uint16_t *p_Buffer, uint16_t num_write);
error_status flash_2kb_write(uint32_t write_addr, uint8_t *pbuffer);
flag_status flash_upgrade_flag_read(void);
void write_upgrade_flag(void);



#endif



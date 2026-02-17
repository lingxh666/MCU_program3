/**
  **************************************************************************
  * @file     flash.c
  * @version  v2.0.5
  * @date     2021-12-17
  * @brief    flash program
  **************************************************************************
  *                       Copyright notice & Disclaimer
  *
  * The software Board Support Package (BSP) that is made available to 
  * download from Artery official website is the copyrighted work of Artery. 
  * Artery authorizes customers to use, copy, and distribute the BSP 
  * software and its related documentation for the purpose of design and 
  * development in conjunction with Artery microcontrollers. Use of the 
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */

#include "bsp_flash.h"
#include "bsp_ota.h"

u8 data_buffer1[SECTOR_SIZE];   
u8 data_buffer2[SECTOR_SIZE];

/**
  * @brief  check flash upgrade flag.
  * @param  none
  * @retval none
  */
flag_status flash_upgrade_flag_read(void)
{
  if((*(uint32_t*)OTA_UPGRADE_FLAG_ADDR) == OTA_UPGRADE_FLAG)
    return SET;
  else
    return RESET;
}

/**
  * @brief  flash write.
  * @param  addr: the address of flash.
  *         pbuffer: the pointer of data.
  *         num: the num of data.
  * @retval none
  */
static void flash_write(uint32_t addr, uint8_t *pbuffer, uint32_t num)
{
  uint16_t index;  
  for(index = 0; index < num; index += 2)
  {
    flash_halfword_program(addr, *(uint16_t*)(pbuffer+index));
    addr += sizeof(uint16_t);
  }
}

/**
  * @brief  flash read.
  * @param  addr: the address of flash.
  *         pbuffer: the pointer of data.
  *         num: the num of data.
  * @retval none
  */
static void flash_read(uint32_t addr, uint8_t *pbuffer, uint32_t num)
{
  uint16_t index;  
  for(index = 0; index < num; index += 2)
  {
    *(uint16_t*)(pbuffer+index) = *(uint16_t*)addr;
    addr += sizeof(uint16_t);
  }
}

/**
  * @brief  app flash update flow handle.
  * @param  none
  * @retval error_status
  */
error_status app_flash_update(void)
{
  uint32_t i;
  uint8_t sector_cnt = 0;
  uint32_t addr_offset;

  flash_unlock();
  do
  {
    addr_offset = sector_cnt * SECTOR_SIZE;
    flash_read(FLASH_BKP_ADDR+addr_offset, data_buffer1, SECTOR_SIZE);
    flash_sector_erase(FLASH_APP_ADDR+addr_offset);
    flash_write(FLASH_APP_ADDR+addr_offset, data_buffer1, SECTOR_SIZE);
    flash_read(FLASH_APP_ADDR+addr_offset, data_buffer2, SECTOR_SIZE);

    for(i=0; i<SECTOR_SIZE; i++)
    {
      if(data_buffer1[i] != data_buffer2[i])
      {
        flash_lock();  /* ★ 修复#32: 失败路径必须锁定Flash */
        return ERROR;
      }
    }
    sector_cnt++;
  }while(sector_cnt < 126);//60*2K=120k  ����ռ�
  flash_lock();
  return SUCCESS;
}

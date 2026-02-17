/**
  **************************************************************************
  * @file     flash.h
  * @version  v2.0.5
  * @date     2021-12-17
  * @brief    flash header file
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

#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f403a_407.h"

//#define FLASH_SIZE                       (1024*(*((uint32_t*)0x1FFFF7E0)))  /* read from at32 flash capacity register(unit:kbyte) */
#define APP_SIZE                         (252*1024)
#define OTA_SIZE                         (6*1024)
#define SECTOR_SIZE                      (0x800)
//#define SRAM_SIZE                        224                         /* sram size, unit:kbyte */

#define FLASH_APP_ADDR                   (0x08002000)
#define FLASH_BKP_ADDR                   (0x08041000)            // BKP Area:100k
//#define FLASH_BKP_END_ADDR               (0X0803DFFF)

error_status app_flash_update( void );
flag_status flash_upgrade_flag_read( void );

#ifdef __cplusplus
}
#endif

#endif

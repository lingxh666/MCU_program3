/**
  **************************************************************************
  * @file     ota.h
  * @version  v2.0.5
  * @date     2021-12-17
  * @brief    ota header file
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

#ifndef __BSP_OTA_H__
#define __BSP_OTA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f403a_407.h"

/* app starting address */
#define APP_START_ADDR                   0x08002000

/* the previous sector of app starting address is ota upgrade flag */
#define OTA_UPGRADE_FLAG_ADDR            0X08001800

/* when app received cmd 0x5aa5 from pc-tool, will set up the flag,
indicates that an app upgrade will follow, see ota application note for more details */
#define OTA_UPGRADE_FLAG                 0x41544B38

typedef void (*otafun)(void);
void ota_upgrade_app_handle(void);
void app_load(uint32_t appxaddr);
  
#ifdef __cplusplus
}
#endif

#endif

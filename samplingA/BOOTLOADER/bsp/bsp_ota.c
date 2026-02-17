
#include "bsp_ota.h"
#include "bsp_flash.h"


otafun jump_to_app;

/* app_load don't optimize */
#if defined (__CC_ARM)
  #pragma O0
#elif defined (__ICCARM__)
  #pragma optimize=s none
#endif
/**
  * @brief  app load.
  * @param  app_addr
  *         app code starting address
  * @retval none
  */
void app_load(uint32_t app_addr)
{
  /* check app starting address whether 0x08xxxxxx */
  if(((*(uint32_t*)(app_addr + 4)) & 0xFF000000) != 0x08000000)
    return;
  /* check the address of stack */
  else
  {
    /* disable periph clock */
    jump_to_app = (otafun)*(uint32_t*)(app_addr + 4);        /* code second word is reset address */    
    __set_MSP(*(uint32_t*)app_addr);                        /* init app stack pointer(code first word is stack address) */
    jump_to_app();                                          /* jump to user app */
  }
}


void ota_upgrade_app_handle(void)
{
  uint8_t i;
  uint32_t addr;
  if(flash_upgrade_flag_read() != SET)
    return;
  for(i = 0; i < 3; i++)
  {
    if(app_flash_update()==SUCCESS)
    {
      flash_sector_erase(OTA_UPGRADE_FLAG_ADDR); // 清除升级标志

      // 擦除KVDB区域 (0x08080000-0x080FFFFF, 512KB, 256个扇区)
      flash_unlock();
      for(addr = 0x08080000; addr < 0x08100000; addr += 0x800)
      {
        flash_sector_erase(addr);
      }
      flash_lock();

      app_load(APP_START_ADDR);
    }
  }
}

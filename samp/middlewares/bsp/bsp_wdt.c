#include "bsp_wdt.h"

void bsp_wdt_enable(void)
{
  wdt_enable();
}

void bsp_wdt_feed(void)
{
  wdt_counter_reload();
}

uint8_t bsp_wdt_is_reset(void)
{
  if(crm_flag_get(CRM_WDT_RESET_FLAG) != RESET)
  {
    crm_flag_clear(CRM_WDT_RESET_FLAG);
    return 1;
  }
  return 0;
}

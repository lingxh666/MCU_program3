
#include "at32f403a_407_clock.h"
#include "bsp_ota.h"
#include "bsp_flash.h"



int main(void)
{
  system_clock_config();

	 if(flash_upgrade_flag_read() == RESET)
  {
    app_load(APP_START_ADDR);
  }
	
  while(1)
  {
		ota_upgrade_app_handle();
		
  }
}


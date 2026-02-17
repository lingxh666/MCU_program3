#include <fal.h>

static uint8_t init_ok = 0;

int fal_init(void)
{
    extern int fal_flash_init(void);
    extern int fal_partition_init(void);

    int result;

    result = fal_flash_init();
    if (result < 0) {
        goto __exit;
    }

    result = fal_partition_init();

__exit:
    if ((result > 0) && (!init_ok))
    {
        init_ok = 1;
        log_i("Flash Abstraction Layer (V%s) initialize success.", FAL_SW_VERSION);
    }
    else if(result <= 0)
    {
        init_ok = 0;
        log_e("Flash Abstraction Layer (V%s) initialize failed.", FAL_SW_VERSION);
    }

    return result;
}

int fal_init_check(void)
{
    return init_ok;
}

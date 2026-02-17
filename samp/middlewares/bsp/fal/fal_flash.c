#include <fal.h>
#include <string.h>

#if !defined(FAL_FLASH_DEV_TABLE)
#error "You must defined flash device table (FAL_FLASH_DEV_TABLE) on 'fal_cfg.h'"
#endif

static const struct fal_flash_dev * const device_table[] = FAL_FLASH_DEV_TABLE;
static const size_t device_table_len = sizeof(device_table) / sizeof(device_table[0]);
static uint8_t init_ok = 0;

int fal_flash_init(void)
{
    size_t i;

    if (init_ok)
    {
        return 0;
    }

    for (i = 0; i < device_table_len; i++)
    {
        assert(device_table[i]->ops.read);
        assert(device_table[i]->ops.write);
        assert(device_table[i]->ops.erase);
        if (device_table[i]->ops.init)
        {
            device_table[i]->ops.init();
        }
        log_d("Flash device | %*.*s | addr: 0x%08lx | len: 0x%08x | blk_size: 0x%08x |initialized finish.",
                FAL_DEV_NAME_MAX, FAL_DEV_NAME_MAX, device_table[i]->name, device_table[i]->addr, device_table[i]->len,
                device_table[i]->blk_size);
    }

    init_ok = 1;
    return 0;
}

const struct fal_flash_dev *fal_flash_device_find(const char *name)
{
    assert(init_ok);
    assert(name);

    size_t i;

    for (i = 0; i < device_table_len; i++)
    {
        if (!strncmp(name, device_table[i]->name, FAL_DEV_NAME_MAX)) {
            return device_table[i];
        }
    }

    return NULL;
}

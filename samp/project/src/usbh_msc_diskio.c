/**
  **************************************************************************
  * @file     usbh_msc_diskio.c
  * @brief    USB MSC 磁盘IO接口 - 桥接 FatFS 与 USB MSC Host
  **************************************************************************
  */
#include "ff.h"
#include "diskio.h"
#include "usb_core.h"
#include "usbh_msc_class.h"

/* OTG1 Host 结构体（定义在 usb_app.c） */
extern otg_core_type otg_core_struct_fs1;

/**
  * @brief  获取磁盘状态
  */
DSTATUS disk_status(BYTE pdrv)
{
    if (usbh_msc_is_ready(&otg_core_struct_fs1.host, pdrv) == MSC_OK)
        return RES_OK;
    return RES_ERROR;
}

/**
  * @brief  初始化磁盘
  */
DSTATUS disk_initialize(BYTE pdrv)
{
    return RES_OK;
}

/**
  * @brief  读取扇区
  */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    usb_sts_type status;
    status = usbh_msc_read(&otg_core_struct_fs1.host, sector, count, buff, pdrv);
    if (status == USB_OK)
        return RES_OK;
    return RES_ERROR;
}

/**
  * @brief  写入扇区（只读模式下不使用）
  */
#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    usb_sts_type status;
    status = usbh_msc_write(&otg_core_struct_fs1.host, sector, count, (uint8_t *)buff, pdrv);
    if (status == USB_OK)
        return RES_OK;
    return RES_ERROR;
}
#endif

/**
  * @brief  磁盘控制
  */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    usbh_core_type *host = (usbh_core_type *)&otg_core_struct_fs1.host;
    usbh_msc_type *pmsc = (usbh_msc_type *)host->class_handler->pdata;
    DRESULT res = RES_OK;

    switch (cmd)
    {
    case CTRL_SYNC:
        res = RES_OK;
        break;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = pmsc->l_unit_n[pdrv].capacity.blk_nbr;
        break;
    case GET_SECTOR_SIZE:
        *(DWORD *)buff = pmsc->l_unit_n[pdrv].capacity.blk_size;
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = pmsc->l_unit_n[pdrv].capacity.blk_size;
        break;
    default:
        res = RES_PARERR;
        break;
    }
    return res;
}

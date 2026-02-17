/**
  **************************************************************************
  * @file     usbh_user.c
  * @brief    USB Host 用户回调 - MSC U盘 OTA 升级
  **************************************************************************
  */

#include "usbh_user.h"
#include "usbh_msc_class.h"
#include "ff.h"
#include "usb_conf.h"
#include <stdio.h>
#include <string.h>

/* OTA 固件文件名 */
#define OTA_FW_FILENAME    "firmware.bin"

/* OTA 状态机 */
typedef enum {
    USR_IDLE = 0,
    USR_READY,
    USR_OTA_CHECK,
    USR_OTA_UPGRADE,
    USR_FINISH
} usr_state_type;

/* OTA 固件头结构（16字节） */
typedef struct {
    uint32_t magic;       /* 魔数: 0x4F544146 ("OTAF") */
    uint32_t version;     /* 固件版本号 */
    uint32_t data_len;    /* 固件数据长度（不含头） */
    uint32_t crc32;       /* 固件数据CRC32校验 */
} ota_fw_header_t;

#define OTA_FW_MAGIC    0x4F544146u  /* "OTAF" */

static FATFS g_fatfs;
static FIL   g_file;
static usr_state_type g_usr_state = USR_IDLE;
static volatile uint8_t g_ota_result = 0;  /* 0=未完成, 1=成功, 2=失败 */

/* 前向声明 */
static usb_sts_type usbh_user_init(void);
static usb_sts_type usbh_user_reset(void);
static usb_sts_type usbh_user_attached(void);
static usb_sts_type usbh_user_disconnect(void);
static usb_sts_type usbh_user_speed(uint8_t speed);
static usb_sts_type usbh_user_mfc_string(void *string);
static usb_sts_type usbh_user_product_string(void *string);
static usb_sts_type usbh_user_serial_string(void *string);
static usb_sts_type usbh_user_enumeration_done(void);
static usb_sts_type usbh_user_application(void);
static usb_sts_type usbh_user_active_vbus(void *uhost, confirm_state state);
static usb_sts_type usbh_user_not_support(void);

usbh_user_handler_type usbh_user_handle =
{
    usbh_user_init,
    usbh_user_reset,
    usbh_user_attached,
    usbh_user_disconnect,
    usbh_user_speed,
    usbh_user_mfc_string,
    usbh_user_product_string,
    usbh_user_serial_string,
    usbh_user_enumeration_done,
    usbh_user_application,
    usbh_user_active_vbus,
    usbh_user_not_support,
};

/* ================= 基础回调函数 ================= */

static usb_sts_type usbh_user_init(void)
{
    g_usr_state = USR_IDLE;
    g_ota_result = 0;
    printf("[USB] Host 初始化完成\r\n");
    return USB_OK;
}

static usb_sts_type usbh_user_reset(void)
{
    return USB_OK;
}

static usb_sts_type usbh_user_attached(void)
{
    printf("[USB] U盘已插入\r\n");
    return USB_OK;
}

static usb_sts_type usbh_user_disconnect(void)
{
    g_usr_state = USR_IDLE;
    printf("[USB] U盘已拔出\r\n");
    return USB_OK;
}

static usb_sts_type usbh_user_speed(uint8_t speed)
{
    if (speed == USB_PRTSPD_FULL_SPEED)
        printf("[USB] 全速设备\r\n");
    else if (speed == USB_PRTSPD_HIGH_SPEED)
        printf("[USB] 高速设备\r\n");
    return USB_OK;
}

static usb_sts_type usbh_user_mfc_string(void *string)
{
    printf("[USB] 厂商: %s\r\n", (char *)string);
    return USB_OK;
}

static usb_sts_type usbh_user_product_string(void *string)
{
    printf("[USB] 产品: %s\r\n", (char *)string);
    return USB_OK;
}

static usb_sts_type usbh_user_serial_string(void *string)
{
    printf("[USB] 序列号: %s\r\n", (char *)string);
    return USB_OK;
}

static usb_sts_type usbh_user_enumeration_done(void)
{
    g_usr_state = USR_READY;
    printf("[USB] 枚举完成，准备读取U盘\r\n");
    return USB_OK;
}

static usb_sts_type usbh_user_active_vbus(void *uhost, confirm_state state)
{
    return USB_OK;
}

static usb_sts_type usbh_user_not_support(void)
{
    printf("[USB] 不支持的设备\r\n");
    return USB_OK;
}

/* ================= OTA 升级核心逻辑 ================= */

/**
  * @brief  USB Host 应用回调 - U盘插入后自动执行OTA检查与升级
  */
static usb_sts_type usbh_user_application(void)
{
    FRESULT fres;
    UINT bytes_read;
    ota_fw_header_t header;
    static uint8_t read_buf[512];

    switch (g_usr_state)
    {
    case USR_READY:
        g_usr_state = USR_OTA_CHECK;
        break;

    case USR_OTA_CHECK:
        /* 挂载文件系统 */
        fres = f_mount(&g_fatfs, "", 1);
        if (fres != FR_OK)
        {
            printf("[OTA] U盘挂载失败: %d\r\n", fres);
            g_usr_state = USR_FINISH;
            g_ota_result = 2;
            break;
        }
        printf("[OTA] U盘挂载成功\r\n");

        /* 打开固件文件 */
        fres = f_open(&g_file, OTA_FW_FILENAME, FA_READ);
        if (fres != FR_OK)
        {
            printf("[OTA] 未找到固件文件: %s\r\n", OTA_FW_FILENAME);
            f_mount(0, "", 0);
            g_usr_state = USR_FINISH;
            g_ota_result = 0;  /* 无固件文件，非错误 */
            break;
        }

        /* 读取固件头 */
        fres = f_read(&g_file, &header, sizeof(header), &bytes_read);
        if (fres != FR_OK || bytes_read != sizeof(header))
        {
            printf("[OTA] 读取固件头失败\r\n");
            f_close(&g_file);
            f_mount(0, "", 0);
            g_usr_state = USR_FINISH;
            g_ota_result = 2;
            break;
        }

        /* 校验魔数 */
        if (header.magic != OTA_FW_MAGIC)
        {
            printf("[OTA] 固件魔数错误: 0x%08X\r\n", header.magic);
            f_close(&g_file);
            f_mount(0, "", 0);
            g_usr_state = USR_FINISH;
            g_ota_result = 2;
            break;
        }

        printf("[OTA] 固件版本: %u, 大小: %u 字节\r\n",
               header.version, header.data_len);

        /* 校验文件大小 */
        if (header.data_len == 0 || header.data_len > (256u * 1024u))
        {
            printf("[OTA] 固件大小异常\r\n");
            f_close(&g_file);
            f_mount(0, "", 0);
            g_usr_state = USR_FINISH;
            g_ota_result = 2;
            break;
        }

        g_usr_state = USR_OTA_UPGRADE;
        printf("[OTA] 开始升级...\r\n");
        break;

    case USR_OTA_UPGRADE:
    {
        /*
         * TODO: 实际的Flash写入逻辑
         * 需要根据Flash分区规划实现：
         * 1. 擦除目标Flash区域
         * 2. 逐块读取固件数据并写入Flash
         * 3. 校验写入数据的CRC32
         * 4. 设置升级标志位
         * 5. 系统重启
         *
         * 当前仅做CRC32校验验证，不执行实际写入
         */
        uint32_t total_read = 0;
        uint32_t crc = 0xFFFFFFFFu;
        uint32_t file_size;
        uint32_t i, j;

        /* 重新读取头部获取参数 */
        f_lseek(&g_file, 0);
        f_read(&g_file, &header, sizeof(header), &bytes_read);
        file_size = header.data_len;

        /* 逐块读取并计算CRC */
        while (total_read < file_size)
        {
            uint32_t to_read = file_size - total_read;
            if (to_read > sizeof(read_buf))
                to_read = sizeof(read_buf);

            fres = f_read(&g_file, read_buf, to_read, &bytes_read);
            if (fres != FR_OK || bytes_read == 0)
            {
                printf("[OTA] 读取固件数据失败，偏移: %u\r\n", total_read);
                g_ota_result = 2;
                break;
            }

            /* 累计CRC32 */
            for (i = 0; i < bytes_read; i++)
            {
                crc ^= read_buf[i];
                for (j = 0; j < 8; j++)
                {
                    if (crc & 1)
                        crc = (crc >> 1) ^ 0xEDB88320u;
                    else
                        crc >>= 1;
                }
            }

            total_read += bytes_read;
        }
        crc ^= 0xFFFFFFFFu;

        f_close(&g_file);
        f_mount(0, "", 0);

        if (g_ota_result != 2)
        {
            if (crc == header.crc32)
            {
                printf("[OTA] CRC校验通过 (0x%08X)\r\n", crc);
                printf("[OTA] 固件验证成功，等待实现Flash写入逻辑\r\n");
                g_ota_result = 1;
            }
            else
            {
                printf("[OTA] CRC校验失败: 期望0x%08X, 实际0x%08X\r\n",
                       header.crc32, crc);
                g_ota_result = 2;
            }
        }

        g_usr_state = USR_FINISH;
        break;
    }

    case USR_FINISH:
        /* 升级完成，不再重复处理 */
        break;

    default:
        break;
    }

    return USB_OK;
}

/* ================= 外部查询接口 ================= */

/**
  * @brief  获取OTA升级结果
  * @retval 0=未完成/无固件, 1=成功, 2=失败
  */
uint8_t usb_ota_get_result(void)
{
    return g_ota_result;
}

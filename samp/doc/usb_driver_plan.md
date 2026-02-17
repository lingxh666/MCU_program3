# USB 驱动改造计划

## 一、现状分析

### 1.1 硬件配置
| USB端口 | 引脚 | 当前功能 | 目标功能 |
|---------|------|---------|---------|
| USB1 (OTG1) | PA11/PA12 | Host HID（键盘/鼠标） | Host MSC（U盘OTA升级） |
| USB2 (OTG2) | PB14/PB15 | Device CDC（虚拟串口） | Device CDC（虚拟串口，保留） |

### 1.2 现有工程 USB 文件清单

**应用层（project/src, project/inc）：**
| 文件 | 功能 | 处理方式 |
|------|------|---------|
| usb_app.c/h | USB初始化与主循环 | **重写** OTG1部分 |
| usb_conf.h | USB配置（FIFO、GPIO等） | **修改** 增加MSC配置 |
| cdc_class.c/h | OTG2 CDC Device类 | **保留** 不动 |
| cdc_desc.c/h | OTG2 CDC描述符 | **保留** 不动 |
| usbh_user.c/h | OTG1 Host用户回调 | **重写** 为MSC+OTA逻辑 |
| usbh_hid_class.c | OTG1 HID Host类 | **删除** |
| usbh_hid_keyboard.c | HID键盘解码 | **删除** |
| usbh_hid_mouse.c | HID鼠标解码 | **删除** |

**USB底层驱动（middlewares/usb_drivers/）：**
- usb_core.c/h, usbd_core.c/h, usbd_int.c/h, usbd_sdr.c/h
- usbh_core.c/h, usbh_ctrl.c/h, usbh_int.c/h
- 全部 **保留**，无需修改

**USB Host类驱动（middlewares/usbh_class/）：**
| 目录 | 文件 | 处理方式 |
|------|------|---------|
| usbh_hid/ | usbh_hid_class.c/h, keyboard/mouse | **从Keil工程移除**（源文件保留不删） |
| usbh_msc/ | usbh_msc_class.c/h, usbh_msc_bot_scsi.c/h | **添加到Keil工程** |
| usbh_cdc/ | usbh_cdc_class.c/h | 不使用，不添加 |

**USB Device类驱动（middlewares/usbd_class/）：**
- msc/, composite_cdc_msc/ 等 — 不使用，不添加

### 1.3 缺失组件

| 组件 | 说明 | 来源 |
|------|------|------|
| FatFS | 文件系统库（ff.c, ff.h, ffsystem.c, ffunicode.c, diskio.h） | 开源库 FatFs R0.14b (ChaN)，需下载放入 `middlewares/3rd_party/fatfs/source/` |
| usbh_msc_diskio.c | FatFS ↔ USB MSC 桥接 | 参考 `msc_only_fat32` demo，适配后放入 `project/src/` |
| ffconf.h | FatFS 配置 | 参考 demo，适配后放入 `project/inc/` |

## 二、参考 Demo 分析

### 2.1 msc_only_fat32 Demo 关键架构
- 单 OTG1 Host MSC 模式
- 使用 `uhost_msc_class_handler` 作为 class handler
- `usbh_user_application()` 在枚举完成后执行文件操作
- 状态机：`USR_IDLE → USR_APP → USR_FINISH`
- diskio 层通过 `usbh_msc_read/write()` 桥接 FatFS 与 USB MSC

### 2.2 otg1_host_otg2_device Demo 关键架构
- 两个独立 `otg_core_type` 结构体
- OTG1 = Host (USB_FULL_SPEED_CORE_ID, USB_ID=0)
- OTG2 = Device (USB_FULL_SPEED_CORE_ID, USB2_ID=1)
- 独立 GPIO 配置和中断处理

## 三、改造步骤

### 步骤1：获取并集成 FatFS 库

**操作：**
1. 从 FatFs 官网 (http://elm-chan.org/fsw/ff/) 下载 R0.14b 版本
2. 创建目录 `samp/middlewares/3rd_party/fatfs/source/`
3. 放入文件：`ff.c`, `ff.h`, `diskio.h`, `ffsystem.c`, `ffunicode.c`
4. 在 `project/inc/` 创建 `ffconf.h`（基于 demo 修改）

**ffconf.h 关键配置：**
```c
#define FF_FS_READONLY   1    /* OTA只需读取，设为只读减小体积 */
#define FF_FS_MINIMIZE   0    /* 保留基本API */
#define FF_USE_LFN       0    /* 禁用长文件名，节省RAM */
#define FF_CODE_PAGE     437  /* 美国代码页 */
#define FF_VOLUMES       1    /* 单卷 */
#define FF_FS_NORTC      1    /* 无需RTC时间戳 */
#define FF_FS_REENTRANT  0    /* 单线程访问，无需重入 */
#define FF_MIN_SS        512
#define FF_MAX_SS        512
```

### 步骤2：创建 USB MSC diskio 桥接文件

**操作：**
1. 基于 `msc_only_fat32/src/usbh_msc_diskio.c` 创建 `project/src/usbh_msc_diskio.c`
2. 修改 `extern otg_core_type` 引用为 `otg_core_struct_fs1`（OTG1 Host 结构体）
3. OTA 只读场景下 `disk_write()` 可简化

**关键适配点：**
```c
extern otg_core_type otg_core_struct_fs1;  /* OTG1 Host */

DRESULT disk_read(...) {
    status = usbh_msc_read(&otg_core_struct_fs1.host, sector, count, buff, pdrv);
    ...
}
```

### 步骤3：修改 usb_conf.h

**操作：**
1. 保留 `USE_OTG_DEVICE_MODE` 和 `USE_OTG_HOST_MODE`
2. 保留 OTG1 Host FIFO 配置（RX=128, NP_TX=96, P_TX=96）
3. 保留 OTG2 Device FIFO 配置
4. 保留 `USB_VBUS_IGNORE`
5. 确认 `USB_HOST_CHANNEL_NUM` 足够（MSC 需要至少2个通道，当前16个足够）
6. 添加 MSC 相关 include 路径（如需要）

**基本不需要大改，当前配置已兼容 MSC Host。**

### 步骤4：重写 usb_app.c

**删除内容：**
- HID 相关头文件 include
- HID 相关初始化代码
- HID 数据处理逻辑

**修改内容：**
```c
/* OTG1 初始化：从 HID Host 改为 MSC Host */
#include "usbh_msc_class.h"

void wk_usb_app_init(void)
{
    /* OTG2: CDC Device — 保持不变 */
    usbd_init(..., &cdc_class_handler, &cdc_desc_handler);

    /* OTG1: MSC Host — 替换 HID */
    usbh_init(&otg_core_struct_fs1,
              USB_FULL_SPEED_CORE_ID, USB_OTG1_ID,
              &uhost_msc_class_handler,    /* 替换 uhost_hid_class_handler */
              &usbh_user_handle);
}
```

**wk_usb_app_task() 修改：**
- OTG2 CDC 收发逻辑保留
- OTG1 保留 `usbh_loop_handler()` 调用（MSC 枚举由底层自动处理）
- 删除 HID 数据处理代码

### 步骤5：重写 usbh_user.c — OTA 升级核心逻辑

**这是最关键的文件，实现 U盘插入后自动 OTA 升级。**

**状态机设计：**
```
USB_IDLE → USB_CONNECTED → USB_ENUMERATED → OTA_CHECK → OTA_UPGRADE → OTA_DONE
```

**usbh_user_application() 核心流程：**
```
1. f_mount() 挂载 U盘
2. f_open() 打开固件文件（如 "firmware.bin"）
3. 校验固件头（魔数、版本号、CRC等）
4. 逐块读取固件数据
5. 写入内部 Flash（通过 FAL 或直接操作）
6. 校验写入数据
7. f_close() 关闭文件
8. f_mount(NULL) 卸载
9. 设置升级标志，重启系统
```

**OTA 固件文件约定：**
- 文件名：`firmware.bin`（或 `samp_fw.bin`）
- 放在 U盘根目录
- 文件格式：`[固件头16字节] + [固件数据]`
- 固件头：魔数(4B) + 版本号(4B) + 数据长度(4B) + CRC32(4B)

**关键函数：**
```c
/* 用户回调结构体 */
usbh_user_handler_type usbh_user_handle = {
    usbh_user_init,
    usbh_user_reset,
    usbh_user_attached,
    usbh_user_disconnect,
    usbh_user_speed,
    usbh_user_mfc_string,
    usbh_user_product_string,
    usbh_user_serial_string,
    usbh_user_enumeration_done,
    usbh_user_application,       /* 核心：OTA逻辑入口 */
    usbh_user_not_support,
    usbh_user_unrecovered_error,
};
```

### 步骤6：修改 Keil 工程文件 (samp.uvprojx)

**添加 Include 路径：**
```
../../middlewares/usbh_class/usbh_msc
../../middlewares/3rd_party/fatfs/source
```

**添加源文件组：**
```
Group: middlewares/usbh_msc
  - ../../middlewares/usbh_class/usbh_msc/usbh_msc_class.c
  - ../../middlewares/usbh_class/usbh_msc/usbh_msc_bot_scsi.c

Group: middlewares/fatfs
  - ../../middlewares/3rd_party/fatfs/source/ff.c
  - ../../middlewares/3rd_party/fatfs/source/ffsystem.c
  - ../../middlewares/3rd_party/fatfs/source/ffunicode.c
```

**从工程移除（不删除源文件）：**
```
- usbh_hid_class.c
- usbh_hid_keyboard.c
- usbh_hid_mouse.c
```

### 步骤7：删除应用层 HID 文件

**从 project/src/ 删除：**
- usbh_hid_class.c（这是应用层副本，非中间件）
- usbh_hid_keyboard.c
- usbh_hid_mouse.c

**注意：** `middlewares/usbh_class/usbh_hid/` 目录下的文件保留不删，只是不加入工程。

### 步骤8：编译验证与调试

1. Keil 编译，修复所有错误和警告
2. 验证 OTG2 CDC 虚拟串口功能不受影响
3. 验证 OTG1 MSC Host 枚举正常
4. 验证 FatFS 挂载和文件读取正常
5. 验证 OTA 升级流程

## 四、文件变更汇总

### 新增文件
| 文件路径 | 说明 |
|---------|------|
| middlewares/3rd_party/fatfs/source/ff.c | FatFS 核心（外部获取） |
| middlewares/3rd_party/fatfs/source/ff.h | FatFS 头文件（外部获取） |
| middlewares/3rd_party/fatfs/source/diskio.h | FatFS 磁盘IO接口（外部获取） |
| middlewares/3rd_party/fatfs/source/ffsystem.c | FatFS 系统函数（外部获取） |
| middlewares/3rd_party/fatfs/source/ffunicode.c | FatFS Unicode支持（外部获取） |
| project/src/usbh_msc_diskio.c | USB MSC ↔ FatFS 桥接 |
| project/inc/ffconf.h | FatFS 配置文件 |

### 修改文件
| 文件路径 | 修改内容 |
|---------|---------|
| project/src/usb_app.c | OTG1 从 HID Host 改为 MSC Host |
| project/inc/usb_app.h | 更新函数声明 |
| project/src/usbh_user.c | 重写为 OTA 升级逻辑 |
| project/inc/usbh_user.h | 更新声明 |
| project/inc/usb_conf.h | 添加 MSC include 路径（如需要） |
| project/MDK_V5/samp.uvprojx | 添加/移除文件组和 include 路径 |

### 删除文件（从工程移除 + 删除应用层副本）
| 文件路径 | 说明 |
|---------|------|
| project/src/usbh_hid_class.c | HID Host类（应用层副本） |
| project/src/usbh_hid_keyboard.c | HID键盘解码 |
| project/src/usbh_hid_mouse.c | HID鼠标解码 |

## 五、实施顺序

```
步骤1 → 步骤2 → 步骤3 → 步骤4 → 步骤5 → 步骤6 → 步骤7 → 步骤8
  │         │         │         │         │         │         │         │
获取FatFS  创建diskio  改usb_conf  改usb_app  改usbh_user  改Keil工程  删HID文件  编译验证
```

**注意：步骤1（FatFS获取）需要用户确认来源，其余步骤可自动执行。**

## 六、风险与注意事项

1. **FatFS 库获取**：SDK 中缺失 FatFS 源文件，需从官网下载或从完整 SDK 包中提取
2. **Flash 空间**：OTA 需要预留足够的 Flash 空间存放新固件，需确认 Flash 分区规划
3. **OTA 安全性**：固件校验（CRC32）防止损坏固件写入
4. **断电保护**：OTA 过程中断电的恢复机制（双分区或标志位方案）
5. **FreeRTOS 集成**：USB Host 轮询需要在合适的任务中执行
6. **内存占用**：FatFS + USB MSC 缓冲区需要额外 RAM，需确认不超出限制

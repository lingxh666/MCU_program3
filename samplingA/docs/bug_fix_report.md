# samplingB 缺陷修复测试报告

**项目**: samplingB MCU固件
**日期**: 2026-01-25
**编译器**: Keil MDK V5.06 update 7 (build 960)
**最终编译状态**: 0 Error(s), 0 Warning(s)

---

## 修复摘要

| Part | 类别 | 修复问题数 | 状态 |
|------|------|-----------|------|
| Part 0 | 编译阻塞修复 | 1 | ✅ 完成 |
| Part 1 | 串口DMA安全 | 2 | ✅ 完成 |
| Part 2 | ISR安全与volatile | 2 | ✅ 完成 |
| Part 3 | 数组越界修复 | 3 | ✅ 完成 |
| Part 4 | 采样调度边界保护 | 2 | ✅ 完成 |
| Part 5 | FlashDB并发与锁 | 2 | ✅ 完成 |
| Part 6 | OTA校验与解析 | 2 | ✅ 完成 |
| Part 7 | Modbus协议修复 | 2 | ✅ 完成 |
| Part 8 | 触发器模块修复 | 1 | ✅ 完成 |
| Part 9 | 记录缓存与TSDB | 1 | ✅ 完成 |
| Part 10 | 低优先级与清理 | 3 | ✅ 完成 |
| **总计** | | **21** | ✅ |

---

## 详细修复记录

### Part 0: 编译阻塞修复

**问题 #18**: `calendar.day` 字段不存在导致编译错误

| 文件 | 修改内容 |
|------|----------|
| `mb_reg_dahu.c:453` | `calendar.day` → `calendar.date` |
| `mb_reg_sichuan.c:325` | `calendar.day` → `calendar.date` |
| `mb_reg_dayue.c:1102` | `calendar.day` → `calendar.date` |

---

### Part 1: 串口DMA安全

**问题 #4**: UART6 DMA缓冲区未终止且被直接strstr

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `at32f403a_407_int.c` | ISR | 添加 `UART6_Buf[len] = '\0'` 字符串终止符 |

**问题 #6**: UART2 DMA拷贝越界（160字节写入100字节缓冲区）

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `at32f403a_407_int.c` | ISR | 添加长度限制 `MIN(len, sizeof(message.data))` |

---

### Part 2: ISR安全与volatile

**问题 #2**: ISR计数器未volatile

| 文件 | 修改内容 |
|------|----------|
| `freertos_app.h` | 为ISR更新的全局变量添加volatile修饰符 |

**问题 #30**: Wiegand ISR中printf

| 文件 | 修改内容 |
|------|----------|
| `wiegand.c` | 移除ISR中的printf调用，改为设置标志位 |

---

### Part 3: 数组越界修复

**问题 #59**: 送样校准数组越界

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `work.c` | `calc_delivery_time_by_volume` | 扩容usable_t/usable_v数组或限制usable_count |

**问题 #26**: Modbus CRC短帧越界

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `work.c` | `vCrc16Check` | 添加 `if (length < 4) return 0;` 检查 |

**问题 #27**: 屏幕消息长度未校验

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `screen.c` | `screen_message_dispatcher` | 添加 `if (msg.len < 6) return;` 最小长度检查 |
| `screen.c` | 各分支 | 访问msg.data[6/7/8]前添加长度校验 |

---

### Part 4: 采样调度边界保护

**问题 #7/#21**: 启动采样间隔除零/下溢

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `sampling_time.c` | `tp_compute_startup_interval` | 添加下溢保护检查 |
| `sampling_time.c` | 3处位置 | `if (seconds_to_delivery <= buffer_time)` 特殊处理 |

**问题 #17**: 采样间隔可为0

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `screen.c` | SampleInterval赋值 | 添加 `if (newv < 1) newv = 1;` 最小值校验 |

---

### Part 5: FlashDB并发与锁

**问题 #28**: KVDB锁超时仍访问

| 文件 | 修改内容 |
|------|----------|
| `app_flashdb.c` | 所有cfg_save_*函数添加 `kvdb_lock_was_timeout()` 检查 |

**问题 #40**: TSDB缓存flush无锁

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `screen_cache.c` | `tsdb_cache_flush_all` | 添加互斥锁保护 |

---

### Part 6: OTA校验与解析

**问题 #14**: OTA启动命令未校验

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `ota.c` | `OTA_CheckStartCommand` | 解析失败返回错误并清零旧值 |

**问题 #15**: Base64解码超长截断

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `ota.c` | `OTA_Base64DecodeLen` | 超出缓冲返回-2错误码 |

---

### Part 7: Modbus协议修复

**问题 #22**: 大湖满水位16位溢出

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `mb_reg_dahu.c` | 满水位计算 | `uint16_t` → `uint32_t` |

**问题 #44**: Dayue float strict aliasing

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `mb_reg_dayue.c` | 浮点读取 | 使用memcpy替代指针强转 |

---

### Part 8: 触发器模块修复

**问题 #38**: Flowtrigger通知标志无原子

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `Flowtrigger.c` | `flow_trigger_notify_start` | 添加 `taskENTER_CRITICAL()` 保护 |
| `Flowtrigger.c` | `flow_trigger_notify_stop` | 添加 `taskENTER_CRITICAL()` 保护 |

---

### Part 9: 记录缓存与TSDB

**问题 #47**: TSDB缓存写失败仍出队

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `screen_cache.c` | `tsdb_cache_flush_all` | 检查写入结果，失败时停止flush |

---

### Part 10: 低优先级与清理

**问题 #32**: Bootloader失败路径未锁Flash

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `BOOTLOADER/bsp/bsp_flash.c` | `app_flash_update` | 失败路径添加 `flash_lock()` |

**问题 #33**: Wiegand读64位未加锁

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `wiegand.c` | `wiegand_is_data_ready` | 添加临界区保护 |

**问题 #60**: 门禁事件时长未填

| 文件 | 行号 | 修改内容 |
|------|------|----------|
| `bsp_button.c` | `BTN03_PRESS_DOWN_Handler` | 计算并填充实际开门时长 |

---

## 编译验证记录

| Part | 编译结果 | 代码大小 |
|------|----------|----------|
| Part 0 | 0 Error, 0 Warning | - |
| Part 1 | 0 Error, 0 Warning | - |
| Part 2 | 0 Error, 0 Warning | - |
| Part 3 | 0 Error, 0 Warning | - |
| Part 4 | 0 Error, 0 Warning | - |
| Part 5 | 0 Error, 0 Warning | - |
| Part 6 | 0 Error, 0 Warning | - |
| Part 7 | 0 Error, 0 Warning | - |
| Part 8 | 0 Error, 0 Warning | - |
| Part 9 | 0 Error, 0 Warning | - |
| Part 10 | 0 Error, 0 Warning | Code=203252 |

---

## 未修复问题（评估后排除）

以下问题经代码验证后确认不存在或已修复：

| 编号 | 问题 | 排除原因 |
|------|------|----------|
| #1/#45 | Sample ID缓冲区 | 代码正确处理了18字节需求 |
| #5 | SPI NOR容量不一致 | 动态识别和调整已实现 |
| #36 | 广播地址仍应答 | 代码已正确处理 |
| #37 | Flowtrigger采样槽 | 数组大小与使用一致 |

---

## 测试建议

### 功能测试
1. **串口通信**: 发送超长帧测试DMA安全
2. **Modbus协议**: 短帧/异常帧测试
3. **采样调度**: interval=0/1、sample_count=1配置测试
4. **FlashDB**: 并发读写压力测试
5. **OTA升级**: 全链路升级测试
6. **门禁系统**: 开关门事件记录验证

### 边界测试
1. 配置参数边界值测试
2. 缓冲区边界测试
3. 时间戳回绕测试

---

## 结论

本次修复共处理21个缺陷，涵盖：
- 内存安全（数组越界、缓冲区溢出）
- 并发安全（锁保护、原子操作）
- 协议健壮性（长度校验、边界检查）
- 代码质量（volatile、strict aliasing）

所有修复均通过编译验证，建议进行完整功能测试后发布。

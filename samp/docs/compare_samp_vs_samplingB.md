# samp vs samplingB 差异对比与 samp 重构完成度清单

更新时间：2026-02-25  
对比对象：
- `D:\MCU_program3\samp`
- `D:\MCU_program3\samplingB`

> 口径说明（避免误判）  
> 1) 本文以 **samplingB 的“功能集合/模块划分”** 作为参考系，检查 `samp` 当前实现的对齐情况。  
> 2) `samp` 的重构目标在 `samp/docs/refactor_plan.md` 已明确：**参考 samplingB 架构，面向 AT32F435 全部重写/拆分模块**。  
> 3) 两工程 MCU/外设平台不同（AT32F435 vs AT32F403），因此存在“硬件差异导致的不一致”，本文单独列出，不直接算作“重构未完成”。  

---

## 1. 总览结论（先看重点）

### 1.1 核心差异一句话
- `samplingB`：**AT32F403A/407 + 功能更全**（Bootloader/OTA、`sample_id`、`sampling_time`、`record_cache`、`screen_cache`、独立触发器模块等）。
- `samp`：**AT32F435/437 + 按重构计划完成了“业务模块拆分 + 8任务骨架”**（`app_config/app_sampling/app_screen/app_modbus/...`），但与 `samplingB` 相比仍缺少若干关键“能力模块/周边工程”。

### 1.2 `samp` 当前“已完成/部分完成/未完成重构”（按 samplingB 能力基准）

**已完成（有明确等价物，且已接入任务/工程编译）**
- FreeRTOS 基座：两边 `middlewares/freertos/source` 基本一致（仅 `portable.h` 存在差异）。
- KVDB/FlashDB 基座：两边均有 `middlewares/bsp/flashDB`（文件集一致），并在 `samp` 中已用于配置/日志（但配置与应用层封装仍有差异，见下文）。
- `samp` 的 8 任务骨架（`project/src/freertos_app.c`）已落地：USB、采样主控、屏幕、Modbus、4G、CAN+ADC、系统管理、辅助任务（对应 `samp/docs/refactor_plan.md` 的任务拆分）。

**部分完成（存在对应模块，但能力/实现明显缩水或实现方式与 samplingB 差异较大）**
- 采样/送样/留样状态机：`samp/middlewares/bsp/app_sampling.*` 对标 `samplingB/middlewares/bsp/sampling.*`，但 samplingB 侧包含 `sample_id + split日志记录结构体 + 上位机状态码` 等更重的业务能力。
- 调度/触发：`samp/middlewares/bsp/app_scheduler.*` 覆盖 5 种触发模式，但 `samplingB` 侧将调度/触发拆为 `sampling_time.* + Timetrigger/Flowtrigger/Switchtrigger/Commtrigger`，并包含更复杂的“全天时间表/推迟采样/手动影响累计”等状态。
- 串口屏：`samp/middlewares/bsp/app_screen.*` 已做“变量地址表 + 命令处理 + 状态回写”，但 `samplingB` 侧是 `screen.* + screen_cache.*` + 独立“分发器/命令处理任务”，并且屏幕相关能力更大更散。
- 留样判定：`samp/middlewares/bsp/app_retain_judge.*` 有统一接口，但 `samplingB/middlewares/bsp/retain_judge.*` 包含更完整的“模拟量/流量/开关量/通讯触发 + 双级滤波 + 统计 + TSDB记录 + 屏幕电流值推送”等。
- 记录查询/缓存：`samp/middlewares/bsp/app_record_query.*` 对标 `samplingB/middlewares/bsp/record_cache.*`，但 samplingB 侧是“滑动窗口 + 智能预加载 + 异步任务 + mutex”的更重实现。
- FAL(Flash Abstraction Layer) 端口：两边都有 `middlewares/bsp/fal/*`，但 `samp` 使用 `fal_flash_qspi_port.c`，`samplingB` 使用 `fal_flash_spi_nor_port.c`，且核心 `fal_cfg/def/partition` 均存在内容差异（硬件差异 + 分区规划差异叠加）。

**未完成（samplingB 存在明确模块/工程，而 samp 当前缺失或仅有部分替代）**
- Bootloader 工程：`samplingB/BOOTLOADER/...` 存在；`samp` 当前无对应 Bootloader 工程。
- OTA 业务模块：`samplingB/middlewares/bsp/ota.*` 存在；`samp` 当前主要是 `project/src/usbh_user.c` 的 U 盘固件校验流程（并且日志提示“等待实现 Flash 写入逻辑”），与 samplingB 的“Bootloader + OTA”形态尚未对齐。
- `sample_id.*`：samplingB 有“线程安全样本ID生成器”；`samp` 当前无等价模块。
- `sampling_time.*`：samplingB 有“时间等比调度器/全天时间表”；`samp` 当前无等价模块（调度能力主要在 `app_scheduler` 内实现，复杂度显著不同）。
- `screen_cache.*`：samplingB 有“屏幕缓存”；`samp` 当前无等价模块。
- 独立触发器模块：samplingB 有 `Commtrigger/Flowtrigger/Switchtrigger/Timetrigger`；`samp` 当前没有同名文件（触发逻辑更可能内聚在 `app_scheduler`）。

---

## 2. 目录结构差异（顶层）

### 2.1 `samp` 顶层（简化）
- `doc/`, `docs/`, `libraries/`, `middlewares/`, `project/`

### 2.2 `samplingB` 顶层（更“整机化”）
- `BOOTLOADER/`, `OTA/`, `bluetooth/`, `androidApp/` 等整套子系统目录
- `libraries/`, `middlewares/`, `project/`

结论：`samplingB` 更像“完整产品工程集合”；`samp` 更聚焦“AT32F435 上的采样器主程序 + USB(OTA)驱动验证 + 业务重写”。

---

## 3. 工程与硬件平台差异（不直接算重构欠缺）

### 3.1 MCU/启动文件
- `samp`：`at32f435_437_*`，启动文件 `project/MDK_V5/startup_at32f435_437.s`
- `samplingB`：`at32f403a_407_*`，启动文件 `project/MDK_V5/startup_at32f403a_407.s`

### 3.2 `main.c` 外设初始化差异（从调用点抽取）

**`samp/project/src/main.c` 额外/不同的初始化（相对 samplingB）**
- ADC：`wk_adc_common_init + wk_adc2_init + wk_dma2_channel2_init`（samplingB 侧主要是 ADC1 + DMA1_CH1）
- USB：`wk_usb_otgfs1_init + wk_usb_otgfs2_init`（samplingB 无）
- CAN/QSPI/ACC：`wk_can1_init + wk_qspi2_init + wk_acc_init`（samplingB 无）
- 定时器：`wk_tmr7_init + wk_tmr8_init`（samplingB 多 `tmr1/tmr5`，见下条）
- RTC：`samp` 侧业务用 `bsp_rtc_init()`（并注明不使用 `wk_ertc_init()` 以避免上电重置）

**`samplingB/project/src/main.c` 额外/不同的初始化（相对 samp）**
- `wk_debug_config()`（samp 无）
- `wk_spi2_init()`（samp 无）
- RTC：`wk_rtc_init()`（samp 用 `bsp_rtc_init()`）
- 定时器：`wk_tmr1_init + wk_tmr5_init`（samp 无）

> 注：这部分多数属于“芯片/板卡差异与外设组合差异”，不宜直接当成“重构缺失”。

---

## 4. FreeRTOS 任务模型差异（架构层面）

### 4.1 `samp`（8任务，按重构计划拆分）
来源：`samp/project/src/freertos_app.c` + `samp/docs/refactor_plan.md`
- Task01：USB（CDC Device + MSC Host OTA）
- Task02：采样/送样/留样主控状态机
- Task03：串口屏通信与命令处理
- Task04：数采仪/Modbus通信
- Task05：4G模块通信
- Task06：CAN电机控制 + ADC监控
- Task07：系统管理（WDT / KVDB刷写 / 掉电记录等）
- Task08：心跳/刷卡/备用通信

### 4.2 `samplingB`（10任务，屏幕/调度/留样判定拆得更细）
来源：`samplingB/project/src/freertos_app.c` 的 `freertos_task_create()`
- task01~task10：包含看门狗、通讯、调度器、留样判定、屏幕分发器、ADC采集、屏幕命令处理等（任务数量更多，职责切分不同）

**异同点总结**
- `samp`：任务划分更贴近“采样器整机外设清单”（USB、4G、CAN、屏幕、数采仪），并把业务主控集中在 Task02（辅以 Task07 系统管理）。
- `samplingB`：把“屏幕分发/命令处理、调度器、留样判定、ADC采集”等拆成更多任务，内部耦合也更强（大量业务函数在 `work.* / screen.* / sampling_time.*` 等处交错）。

---

## 5. `middlewares/bsp` 模块对照（最关键的重构差异）

### 5.1 文件清单对比（直观）

**`samp/middlewares/bsp`（平铺文件）**
- `app_*`：`app_config`, `app_sampling`, `app_scheduler`, `app_screen`, `app_modbus`, `app_adc_module`, `app_record_query`, `app_retain_judge`
- `bsp_*`：`bsp_adc`, `bsp_uart*`, `bsp_can_motor`, `bsp_qspi_flash`, `bsp_rtc`, `bsp_wdt`, `bsp_io`, `bsp_crc`, `bsp_screen`, `bsp_pvm`, `bsp_timer`, `bsp_wiegand`

**`samplingB/middlewares/bsp`（平铺文件）**
- 业务/架构：`sampling`, `sampling_time`, `sample_id`, `work`
- UI/缓存：`screen`, `screen_cache`, `record_cache`
- 留样与触发：`retain_judge`, `Timetrigger/Flowtrigger/Switchtrigger/Commtrigger`
- 存储/升级：`spi_flash`, `flash`, `ota`
- 其他：`bsp_button`, `multi_button`, `wiegand`

### 5.2 关键模块对照表（建议优先看这一张）

| 领域 | samplingB 模块（参考） | samp 当前模块（对齐/替代） | 重构完成度（按 samplingB） | 主要差异点 |
|---|---|---|---|---|
| 采样/送样/留样主流程 | `sampling.c/h` | `app_sampling.c/h` | 部分完成 | samplingB：包含 `sample_id`、split日志结构体、更多全局上下文；samp：接口更清晰，但缺少上述能力 |
| 调度/触发（时间等比等） | `sampling_time.*` + `Timetrigger/Flowtrigger/Switchtrigger/Commtrigger` | `app_scheduler.*` | 部分完成 | samplingB：调度/触发模块化且状态更复杂；samp：把多模式触发集中到一个调度器，复杂能力（全天表/预加载/手动影响累计等）未见等价实现 |
| 串口屏 | `screen.*` + `screen_cache.*` + 屏幕分发/命令处理任务 | `app_screen.*` + `bsp_screen.*` | 部分完成 | samplingB：分发/缓存/页面渲染函数很多；samp：更偏“地址表 + 命令处理 + 状态回写”，缓存/分发能力未对齐 |
| 记录查询/缓存 | `record_cache.*` | `app_record_query.*` | 部分完成 | samplingB：滑动窗口 + 智能预加载 + mutex + preload任务；samp：固定页缓存 + 同步查询 |
| 留样判定 | `retain_judge.*` | `app_retain_judge.*` | 部分完成 | samplingB：模拟量/流量/开关量/通讯触发、双级滤波、统计、TSDB记录、屏幕电流推送；samp：接口较简化，更多能力缺失或分散到其他模块 |
| 通讯/Modbus | `work.*`（大量协议/Modbus/CRC/解析函数）+ `freemodbus/` | `app_modbus.*` | 部分完成 | samp：自实现从站协议栈 + 寄存器映射更内聚；samplingB：协议/寄存器/业务耦合更重且存在 freemodbus 子目录 |
| Flash 抽象/分区 | `flash.* + spi_flash.* + fal/*` | `bsp_qspi_flash.* + fal/*` | 部分完成（硬件差异项） | samp：QSPI port；samplingB：SPI NOR port；分区/配置均不同，需要按 AT32F435 重新规划对齐 |
| OTA/升级 | `ota.*` + `BOOTLOADER/` | `project/src/usbh_user.c`（U盘固件校验） | 未完成 | samp：目前偏“校验/准备阶段”，Flash 写入与 Bootloader 链路未对齐 |
| 样本ID | `sample_id.*` | 无 | 未完成 | samplingB 有线程安全生成器；samp 未实现 |

---

## 6. `samp` 仍未完成的“对齐项清单”（建议作为后续任务拆分依据）

> 这里列的是“samplingB 明确存在且相对独立”的能力模块；**不含** MCU 外设差异导致的自然不同。

### 6.1 工程/升级链路
- 缺失：Bootloader 工程（`samplingB/BOOTLOADER`）
- 缺失：`middlewares/bsp/ota.c/h` 对应的升级流程（`samp` 当前主要在 `project/src/usbh_user.c` 做 U 盘固件校验，且 Flash 写入未完成）

### 6.2 时间与标识
- 缺失：`sample_id.*`（样本ID生成器）
- 缺失：`sampling_time.*`（时间等比“全天时间表/复杂推迟/手动影响累计”等能力；或需要明确由 `app_scheduler` 承担并补齐）

### 6.3 UI/缓存体系
- 缺失：`screen_cache.*`（屏幕侧缓存/渲染加速机制）
- `record_cache.*` 未对齐：`samp` 已有 `app_record_query`，但与 samplingB 的“滑动窗口 + 异步预加载”能力差距较大

### 6.4 触发器拆分（是否需要对齐取决于目标架构选择）
- samplingB 侧有 `Timetrigger/Flowtrigger/Switchtrigger/Commtrigger`；samp 侧目前无同名模块。  
  建议先明确：  
  - 方案A：继续保持 `samp` 的“触发逻辑内聚到 `app_scheduler`”，则无需同名文件，但需要补齐 samplingB 中触发器的边界条件与状态；  
  - 方案B：将 `app_scheduler` 拆回独立触发器模块，以便与 samplingB 更易对齐与复用测试用例。

---

## 7. 已完成重构的部分（以 samp 的“重写/拆分目标”视角）

> 这一节回答“samp 自身重构计划（refactor_plan.md）已经落地了哪些骨架/模块”，便于确认阶段性成果。

- 8任务骨架与心跳/看门狗框架：`samp/project/src/freertos_app.c`
- 业务模块拆分（均在 `samp/middlewares/bsp/`）：
  - `app_config.*`：配置结构体与KVDB存取
  - `app_sampling.*`：采样/送样/留样/排水状态机接口（非阻塞 step 风格）
  - `app_screen.*`：屏幕变量地址表 + 命令处理 + 状态回写
  - `app_modbus.*`：从站协议栈 + 寄存器映射 + 与调度器联动
  - `app_scheduler.*`：5种采样触发模式调度（时间等比/定时/流量/开关量/通信）
  - `app_retain_judge.*`：留样判定与执行入口（并提供阻塞留样/排水执行）
  - `app_record_query.*`：记录查询分页与缓存
  - `app_adc_module.*`：外部AD模块帧解析 + 流量检测

---

## 8. 附录：常用“快速定位”文件

### 8.1 任务与主流程
- `samp/project/src/freertos_app.c`
- `samplingB/project/src/freertos_app.c`
- `samp/middlewares/bsp/app_sampling.c`
- `samplingB/middlewares/bsp/sampling.c`

### 8.2 调度与触发
- `samp/middlewares/bsp/app_scheduler.c`
- `samplingB/middlewares/bsp/sampling_time.c`
- `samplingB/middlewares/bsp/Timetrigger.c`（以及 Flow/Switch/Comm）

### 8.3 UI/记录
- `samp/middlewares/bsp/app_screen.c`
- `samplingB/middlewares/bsp/screen.c`
- `samp/middlewares/bsp/app_record_query.c`
- `samplingB/middlewares/bsp/record_cache.c`

### 8.4 OTA
- `samp/project/src/usbh_user.c`
- `samplingB/middlewares/bsp/ota.c`
- `samplingB/BOOTLOADER/project/src/main.c`


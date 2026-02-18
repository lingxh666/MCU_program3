# samp 驱动测试计划（不改固件代码版）

> 适用项目：`D:\MCU_program3\samp`（AT32F435/437 + FreeRTOS）  
> 编写日期：2026-02-18  
> 约束：**本阶段不修改固件源码**；优先通过 **现有固件 + Keil 调试调用函数 + 外部仪表/上位机脚本** 得到可重复反馈。  
> 目标：把“已实现/部分实现”的驱动逐项做**驱动级验证**，形成可执行用例与可量化通过标准（Pass/Fail）。

---

## 1. 测试范围与验收口径

### 1.1 范围

本计划覆盖 `middlewares/bsp`、`middlewares/bsp/fal`、`middlewares/bsp/flashDB`、`project/src` 中已经存在的驱动/框架代码，对照 `samp/docs/master_plan.md` 的 D01~D28。

### 1.2 本阶段验收口径（不改代码）

对每个驱动给出 3 类结论：

1. **可黑盒验证（无需 Keil 调试）**：通过串口日志/USB 枚举/CAN 报文等直接确认。
2. **可白盒驱动验证（Keil 调试调用/观测）**：需要用 Keil uVision Debug 模式调用函数/看变量/看寄存器或外部波形。
3. **暂不可验证/验证成本过高（需后续加测试口）**：记录原因与后续建议（会另建 issue，不在本阶段改代码）。

> 注意：本计划尽量把“可重复/可记录”的反馈放在前面；对必须依赖人工/仪器确认的项，明确“你需要怎么确认”。

---

## 2. 现状盘点（对照 master_plan）

### 2.1 驱动实现位置总览（以实际代码为准）

| master_plan 编号 | 名称（简写） | 主要实现文件（实际存在） | 完成度判断 | 本阶段可验证方式 |
|---|---|---|---|---|
| D01 | GPIO 输出（继电器/阀门） | `middlewares/bsp/bsp_io.h` `middlewares/bsp/bsp_io.c` | 🟡（接口齐；未见统一 init/上电默认态由 wk_gpio_config 决定） | Keil 调试调用 + 万用表/继电器动作 |
| D02 | H桥瓶排空电机（PE14/PE15） | `middlewares/bsp/bsp_io.*` | 🟡（正反停 OK；未集成电流判堵转） | Keil 调试调用 + 逻辑分析仪/电机方向 |
| D03 | GPIO 输入（液位/触发/拨码/锁） | `middlewares/bsp/bsp_io.*` | 🟡（读取 OK；未实现消抖/边沿） | Keil 调试调用 + 手动拉高/拉低 |
| D04 | ADC1 DMA 扫描（电流+温度+基准） | `middlewares/bsp/bsp_adc.*` + `project/src/at32f435_437_wk_config.c:wk_adc1_init` | 🟡（DMA/均值/校准存在；电流转换系数为假设） | Keil 观测 `adc1_dma_buf` + 继电器开关/电流变化 |
| D05 | 4-20mA（ADC2 单次） | `middlewares/bsp/bsp_adc.*` + `wk_adc2_init` | ✅（公式按 INA180A2+3Ω 写了） | 外接电流源 + Keil 调用读取 |
| D06 | NTC 温度 | `middlewares/bsp/bsp_adc.*`（温度通道在 ADC1 扫描中） | 🟡（算法 OK；需实测校准） | 温度计对比 + Keil 调用 |
| D07 | 2.5V 基准校准 | `middlewares/bsp/bsp_adc.*` | 🟡（运行/应用 OK；未落 KVDB） | Keil 调用 + 比对校准前后电压 |
| D08 | USART1 调试打印 | `project/src/wk_system.c`（fputc/_write → USART1） | ✅ | 黑盒：串口抓 log |
| D09~D15 | UART DMA 接收框架 + 各串口封装 | `middlewares/bsp/bsp_uart.*` + `bsp_uart_*` 若干 | 🟡（DMA+IDLE 收包 OK；上层协议大多占位） | Keil 调用 `bsp_uart_send/get_rxdata` + 回环/外设回包 |
| D16 | CAN1 电机控制 | `middlewares/bsp/bsp_can_motor.*` + `project/src/at32f435_437_int.c:CAN1_RX0_IRQHandler` | 🟡（自定义帧格式与 master_plan 附录B不一致） | CAN 抓包 + 电机板联调/状态回读 |
| D17 | Wiegand 输入 | `middlewares/bsp/bsp_wiegand.*` + `EXINT4/7` | 🟡（26/34 位解析；需实卡验证） | 刷卡/信号源 + Keil 读取卡号 |
| D18 | QSPI2 Flash | `middlewares/bsp/bsp_qspi_flash.*` | ✅（读写擦基本齐） | Keil 调用读写/校验 |
| D19 | FAL 适配 | `middlewares/bsp/fal/*` | ✅（分区表在 `fal_cfg.h`） | Keil 调用 `fal_init` + 分区读写 |
| D20~D21 | FlashDB KVDB/TSDB | `middlewares/bsp/flashDB/*` | 🟡（库齐；需要在运行态初始化验证） | Keil 调用 `settings_init_load/fdb_start_tasks` + 读写/迭代 |
| D22 | ERTC RTC | `middlewares/bsp/bsp_rtc.*` | ✅ | 黑盒/Keil：断电保持、时间戳转换 |
| D23 | WDT | `middlewares/bsp/bsp_wdt.*` | ✅ | 黑盒：复位标志 + 不喂狗复位 |
| D24 | USB CDC（Device） | `project/src/usb_app.c`（OTGFS2 CDC echo） | ✅ | 黑盒：PC 端虚拟串口回显 |
| D24/D26 | USB MSC Host + OTA 校验 | `project/src/usbh_user.c` | 🟡（能挂载+读文件+CRC32；写 Flash TODO） | 黑盒：串口 log + U盘文件 |
| D27 | CRC（硬件 CRC16 Modbus） | `middlewares/bsp/bsp_crc.*` | ✅ | Keil 调用 + 已知向量对比 |
| D28 | Modbus 协议栈 | 未检索到实现 | ❌ | 本阶段不测 |

### 2.2 已识别的“实现与 master_plan 不一致点”（测试时以实际代码为准）

1. **温度采集**：master_plan 写 ADC2 DMA 温度（D06），实际代码把温度通道放进了 **ADC1 的 12 路扫描**（`bsp_adc.h` 的 `ADC_CH_TEMP1/2`）。
2. **CAN 电机协议**：master_plan 附录B使用 `0x200/0x300` 等帧；当前 `bsp_can_motor.*` 定义为 `0x100`(TX)、`0x200`(RX)，字段也不同。测试需要分别验证“按当前实现能跑通”，并记录差异用于后续统一协议。
3. **继电器电流转换系数**：`adc1_get_current_ma()` 注释明确“系数是假设”。因此电流测试以“趋势变化/阈值判断”为主；绝对值仅做参考，后续要用实测标定。
4. **4G AT 超时逻辑**：`module_4g_send_at()` 内部 `while(wait < timeout_ms) { wait++; }` 没有 `vTaskDelay`/`wk_delay_ms`，因此 **timeout_ms 不是毫秒**。本阶段只验证收发链路/回包到达，不用它做精确超时判定。
5. **AD 模块解析**：`bsp_uart_admodule.c` 标记 `TODO`，本阶段只能验证 UART 收包框架，不验“值解析正确性”。

---

## 3. 测试环境与准备

### 3.1 硬件清单（建议）

- AT32F435/437 主控板（运行 `samp` 工程）
- 调试器：J-Link / DAPLink（Keil 可下载与 Debug）
- 串口工具：
  - USB-TTL（**3.3V 电平**）用于 USART1/其他 UART
  - USB-RS485 转换器（测试 USART2/5/7 等 485 口）
  - USB-RS232 转换器（如板上支持 232 模式）
- CAN：USB-CAN 适配器 + CAN 收发器（确保与板上 CAN1 物理层匹配）
- 仪表：万用表、示波器/逻辑分析仪（建议 ≥ 10MHz 采样）、可调电源
- 4-20mA：电流源/信号源（或 4-20mA 变送器 + 可调模拟输入）
- 温度：温度计（用于 NTC 对比），冰水/温水环境（可选）
- U盘：FAT32 格式（用于 USB Host OTA 校验）
- Wiegand 读头 + 卡（或用信号发生器模拟 D0/D1）

### 3.2 软件清单

- Keil MDK-ARM（路径按仓库约定：`C:\Keil_v5\UV4\UV4.exe`）
- Python 3（用于串口/USB 虚拟串口脚本）
  - 建议依赖：`pyserial`（必要）、`crcmod`（可选）、`python-can`（如做 CAN 脚本）
- 串口调试软件（任选其一）：XCOM/SSCOM/CRT/TeraTerm 等（用于人工发送/抓包）

### 3.3 关键接口连线（最小可跑通）

1. **USART1 调试串口（只发）**
   - MCU：PA9 (USART1_TX)，9600，8N1
   - 接 USB-TTL 的 RX
   - 目的：抓取 `[USB]`、`[OTA]`、`[FDB]` 等日志（如果后续通过 Keil 调用触发）

2. **USB**
   - OTGFS2：作为 CDC Device（连 PC，出现虚拟串口并回显）
   - OTGFS1：作为 MSC Host（插 U盘，触发 OTA 检查/CRC 日志）

3. **CAN1**
   - 需要物理 CAN 收发器与正确 120Ω 终端
   - 目的：抓取电机控制帧、状态帧

---

## 4. 通用流程（不改代码也能做驱动测试）

### 4.1 先做一次“0 错误 / 0 警告”编译（质量门禁）

运行（PowerShell）：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -b 'D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx' -o build_log.txt -j0
```

验收：
- `build_log.txt` 中 **0 Error / 0 Warning**

### 4.2 串口日志采集（USART1）

建议用 Python 持续抓取到文件，便于留证与对比（把 COM 口改成实际值）：

```python
# 保存为本机临时脚本运行即可（本计划不落库）
import time
import serial

PORT = "COM12"
BAUD = 9600
OUT  = f"uart1_log_{time.strftime('%Y%m%d_%H%M%S')}.txt"

with serial.Serial(PORT, BAUD, timeout=0.2) as ser, open(OUT, "w", encoding="utf-8") as f:
    while True:
        line = ser.readline()
        if not line:
            continue
        try:
            s = line.decode("utf-8", errors="replace").rstrip()
        except Exception:
            s = str(line)
        ts = time.strftime("%H:%M:%S")
        f.write(f"[{ts}] {s}\n")
        f.flush()
        print(f"[{ts}] {s}")
```

### 4.3 Keil Debug 的“白盒驱动测试法”（核心手段）

目的：**不改固件**，但仍然能“驱动调用 → 外设响应 → 读回/测量确认”。

建议操作：

1. 用 uVision 下载程序并进入 Debug（Run/Stop 可控）。
2. 在 **Command Window**（或 Debug 工具栏的函数执行能力）调用驱动函数，例如：
   - `relay_set(RELAY_INLET_VALVE, 1);`
   - `bottle_motor_set(MOTOR_EMPTY);`
   - `bsp_adc1_dma_start();`
   - `qspi_flash_read_id();`
3. 在 **Watch/Memory** 里观测变量：
   - `adc1_dma_buf[0][ADC_CH_VREF]`
   - `adc_cal.vref_factor`
   - `can_motor_get_status(0)`
4. 同时用外部仪表确认“引脚电平/波形/电流/温度/总线报文”。

> 通过这一套方法，本阶段可以做到：不改代码也能完成绝大部分“驱动级功能验证”。

---

## 5. 详细测试用例（按驱动分组）

下面每个用例都给出：**前置条件**、**步骤**、**期望**、**记录/判定**、**需要你确认点**。

### 5.1 GPIO 输出（继电器/阀门）& H桥电机（D01/D02）

#### TC-IO-01：全输出关闭（安全基线）

- 前置：
  - 板子供电正常，Keil 可 Halt/Run
  - 外设不会因误动作造成风险（先断开高压/大电流负载或用假负载）
- 步骤：
  1. Keil Debug 运行：调用 `relay_all_off()`
  2. 用万用表测各输出引脚对地电压（或看继电器指示灯/线圈动作）
- 期望：
  - 所有继电器/阀门输出应为低电平（或定义的“关闭态”）
  - `bottle_motor_get_dir()` 返回 `MOTOR_STOP`
- 记录/判定：
  - 逐项记录电平（V）或继电器“无吸合”

#### TC-IO-02：逐路继电器输出开关验证（包含 `relay_get_state`）

- 步骤（对 `relay_id_t` 0..`RELAY_COUNT-1` 逐个执行）：
  1. `relay_set(id, 1)` → 观察输出引脚电平/继电器吸合
  2. 读回 `relay_get_state(id)`（Watch 或直接看返回值）
  3. `relay_set(id, 0)` → 观察释放
- 期望：
  - 输出电平随开关变化（低≈0V，高≈3.3V）
  - `relay_get_state` 与实际一致
- 需要你确认点：
  - 如果外部是继电器/驱动芯片，建议你用 **继电器“咔哒”声/指示灯** 作为第一确认，再用万用表复核。

#### TC-IO-03：瓶排空电机 H桥方向与互斥（PE14/PE15）

- 步骤：
  1. `bottle_motor_set(MOTOR_EMPTY)`：确认 PE14=H，PE15=L
  2. `bottle_motor_set(MOTOR_RESTORE)`：确认 PE14=L，PE15=H
  3. `bottle_motor_set(MOTOR_STOP)`：确认 PE14=L，PE15=L
- 期望：
  - 两个控制脚不会同时为高（避免 H 桥冲突）
- 记录/判定：
  - 逻辑分析仪抓取 PE14/PE15 波形截图（或万用表电平记录）
- 需要你确认点：
  - 若已接实际电机：你用肉眼确认“转向是否与定义一致”，并记录“EMPTY=排空方向/RESTORE=复位方向”。

### 5.2 GPIO 输入（D03）

#### TC-IN-01：输入电平读取（液位/触发/锁/瓶原点等）

- 步骤：
  1. 在 Watch 中逐个调用 `input_read(INPUT_...)`
  2. 人工让对应输入从 0→1 或 1→0（例如开关动作、传感器遮挡；或用跳线+电阻拉到 3.3V/GND）
- 期望：
  - 返回值与实际电平一致
- 记录/判定：
  - 每个输入至少验证一次高、一次低，并记录“是否需要上拉/下拉”
- 备注：
  - 当前实现未消抖，本阶段只验“读取正确”；后续集成业务前再做抖动/边沿测试。

#### TC-IN-02：拨码开关（PE11/12/13）组合值

- 步骤：
  1. 改变拨码组合（0~7）
  2. 调用 `input_get_dip_switch()` 读值
- 期望：
  - 返回值与 3 位组合一致（bit0=SW1，bit1=SW2，bit2=SW3）

### 5.3 ADC（D04/D05/D06/D07）

#### TC-ADC-01：ADC1 DMA 扫描是否在跑（`adc1_dma_buf` 动态更新）

- 前置：
  - `wk_adc1_init()` 已在 `main()` 调用完成
- 步骤：
  1. Keil 调用 `bsp_adc1_dma_start()`
  2. 观察 `adc1_dma_buf[0][0]` 等值是否随时间变化（或均值 `adc1_get_raw()`）
- 期望：
  - 缓冲区数据在变化（噪声/抖动属正常）

#### TC-ADC-02：2.5V 基准校准（`adc_cal_run`）

- 步骤：
  1. 记录校准前 `adc_cal.valid`、`adc_cal.vref_factor`
  2. 调用 `adc_cal_run()`
  3. 调用 `adc1_get_voltage_mv(ADC_CH_VREF)`，看是否接近 2500mV
- 期望：
  - `adc_cal.valid=1`
  - VREF 电压接近 2.5V（误差由硬件/采样决定）
- 记录：
  - 记录 raw、factor、校准后电压

#### TC-ADC-03：电流通道“趋势验证”（开关阀门 → 电流变化）

- 步骤（建议先只接安全负载）：
  1. `relay_set(RELAY_INLET_VALVE, 0)`，记录 `adc1_get_current_ma(ADC_CH_INLET_VALVE)`
  2. `relay_set(RELAY_INLET_VALVE, 1)`，记录同一通道电流
  3. 对其他带电流检测的通道重复（见 `bsp_adc.h` 映射）
- 期望：
  - 开 → 电流显著上升；关 → 电流接近 0（或明显下降）
  - `adc1_is_active(ch)` 的判断与实际一致（阈值 5mA）
- 重要说明：
  - `adc1_get_current_ma()` 的绝对值系数当前是“假设”，所以本用例以“变化趋势/阈值”为主。

#### TC-ADC-04：ADC2 4-20mA 线性度（两路）

- 前置：
  - 外接 4-20mA 电流源到 PC2（CH12）/PA1（CH1）对应通道，按硬件接法接入
- 步骤：
  1. 设定 4mA、12mA、20mA 三个点（至少三点）
  2. 每个点调用 `adc2_get_420ma_current(ADC_420MA_CHx)` 读取并记录 10 次均值
- 期望：
  - 读数与设定值基本一致（误差阈值建议先按 ±5% 作为阶段门槛，后续再收紧）
- 需要你确认点：
  - 你确认电流源输出的“真实电流”（用串联万用表电流档复核一次）。

#### TC-ADC-05：NTC 温度对比（两路）

- 步骤：
  1. 使温度探头处于稳定环境（室温/冰水/温水）
  2. 分别调用 `adc_get_temp(0)`、`adc_get_temp(1)`
  3. 与温度计读数对比
- 期望：
  - 误差在可接受范围（初期可按 ±2~5°C 观察；精度目标后续再定）

### 5.4 UART DMA 接收框架（D09~D15）+ 各功能串口封装

本组建议优先做“**回环自检**”，因为它不依赖外部设备协议，且能验证 DMA+IDLE 收包链路。

#### 5.4.1 通用回环用例（适用于 UART_PORT_* 任一端口）

#### TC-UART-LB-01：单包回环（短帧）

- 前置：
  - 把目标串口的 **TX 与 RX 短接**（例如 USART2: PD5↔PD6）
  - 对应串口已初始化（在 `main()` 中 `wk_usartX_init()` 均已调用）
  - 已调用 `bsp_uart_init()`（`main()` 已调用）
- 步骤：
  1. Keil 调用 `bsp_uart_send(port, "HELLO", 5)`
  2. 等待 10~100ms（让 IDLE 触发收包完成；可 Run 一小段时间）
  3. 申请一个缓冲区（Keil 可在 Watch 中准备数组），调用 `bsp_uart_get_rxdata(port, buf, max_len)`
- 期望：
  - 返回长度=5，内容等于发送内容
- 记录：
  - 记录端口、帧长、回读数据（可截图 Watch）

#### TC-UART-LB-02：长帧/边界（>=256 字节前后）

- 目的：
  - 验证 `UART_DMA_BUF_SIZE=256` 下的截断/溢出行为是否可控
- 步骤：
  - 发送 200 字节、255 字节、260 字节三组数据，分别回读长度与内容
- 期望：
  - 200/255 正常；260 可能被截断（记录实际行为，作为后续协议层设计输入）

#### 5.4.2 按端口的建议优先级（先做能“自反馈”的）

| 端口枚举 | 外设 | 建议优先验证 | 可选外设联调 |
|---|---|---|---|
| `UART_PORT_SCREEN` | UART4 串口屏 | 回环 + 观察 TX 帧 | 接屏做 `screen_write_u16` |
| `UART_PORT_4G` | USART6 4G | 回环（先证 DMA） | 接模块做 AT 回包观测 |
| `UART_PORT_COLLECTOR` | USART2 数采仪 | 回环 + PA15 模式脚验证 | 接 485 设备做收发 |
| `UART_PORT_XIAN485` | UART5 西安485 | 回环 | 接 Modbus 工具（后续） |
| `UART_PORT_BLUETOOTH` | USART3 蓝牙 | 回环 | 手机/串口蓝牙透传 |
| `UART_PORT_SPARE485` | UART7 备用 | 回环 | 备用 |
| `UART_PORT_ADMODULE` | UART8 AD 模块 | 回环（解析暂不验） | 接模块仅验收包到达 |

#### 5.4.3 串口屏协议（D11）专项

##### TC-SCREEN-01：帧格式与回包解析（不接屏也能做）

- 步骤：
  1. 逻辑分析仪夹在 UART4_TX 上
  2. Keil 调用 `screen_write_u16(0x1000, 0x1234)` 或 `screen_switch_page(1)`
  3. 抓取串口帧
- 期望：
  - 帧头 `5A A5`、长度字段正确、命令 `0x82`（写变量）等符合 `bsp_screen.h`

##### TC-SCREEN-02：接屏功能确认（需要你最终确认）

- 步骤：
  - 接入屏幕后重复 TC-SCREEN-01，并观察屏幕变量/页面是否实际变化
- 需要你确认点：
  - 你确认“地址映射/页面号”与屏工程一致（这是业务/屏资源决定的）。

#### 5.4.4 4G 模块 AT（D13）专项（当前为“链路验证”）

##### TC-4G-01：接收回包进入 `at_resp_buf`（需要外设）

- 步骤：
  1. 接入 4G 模块到 USART6
  2. Keil 调用 `module_4g_init()`
  3. 调用 `module_4g_send_data((uint8_t*)\"AT\\r\\n\", 4)`（或模块上电后自发 URC）
  4. 观察（Watch）`at_resp_ready/at_resp_len/at_resp_buf`
- 期望：
  - 模块回包被捕获（`at_resp_ready=1`，buf 内含 `OK` 等）
- 备注：
  - `module_4g_send_at()` 的 timeout 不是毫秒，本阶段不以它做超时验收。

### 5.5 CAN1 电机控制（D16）

#### 5.5.1 按当前实现的帧格式（`bsp_can_motor.*`）

- 主控→电机（TX）：标准帧 ID=`0x100`
  - `SET_SPEED`：`[motor_id, 0x01, speed_h, speed_l, dir]`
  - `START/STOP/LOCK_PANEL`：`[motor_id, cmd]`（cmd 见头文件）
- 电机→主控（RX）：标准帧 ID=`0x200`
  - 期望至少 5 字节：`[motor_id, speed_h, speed_l, dir, running, (optional)error]`

#### TC-CAN-01：总线基础（收发通）

- 前置：
  - CAN 物理层正确：终端电阻、波特率匹配（波特率以 `wk_can1_init()` 配置为准）
- 步骤：
  1. 用 USB-CAN 抓包，确认板子上电后 CAN 无错误风暴
  2. Keil 调用 `can_motor_set_speed(0, 3000, MOTOR_DIR_CW)`（速度单位按代码：RPM×10）
  3. 观察总线上是否出现 ID=0x100 的数据帧
- 期望：
  - 抓包看到正确帧

#### TC-CAN-02：状态回读（需要电机板/模拟节点）

- 步骤：
  1. 让电机板（或另一节点）发送 ID=0x200 状态帧
  2. 在 Keil 中调用 `can_motor_get_status(0)` 读取结构体
- 期望：
  - `speed/direction/running/error` 与对端发送一致
- 需要你确认点：
  - 若接真实电机板：你用 **逻辑分析仪/示波器** 观察电机驱动板输出脉冲/运行状态，确认与 `running` 匹配。

> 如果你更希望按 master_plan 附录B协议测：本计划先验证“现有实现能跑通”，并把协议差异留给后续统一（另建 issue）。

### 5.6 Wiegand（D17）

#### TC-WG-01：实卡刷卡（推荐）

- 前置：
  - Wiegand D0→PD4，D1→PD7（按硬件连接）
- 步骤：
  1. 运行系统，让中断可触发
  2. 刷卡一次
  3. Keil 调用：
     - `uint32_t id;`
     - `wiegand_get_card_id(&id)`（多调用几次直到返回 1）
- 期望：
  - 返回 1 且 `id` 稳定（与卡号一致或可映射）
- 记录：
  - 记录卡号、位宽（26/34）、是否有偶发错误

#### TC-WG-02：信号模拟（可选）

- 用信号源输出符合 Wiegand 时序的脉冲到 D0/D1，验证 26/34 位解析。

### 5.7 QSPI Flash（D18）+ FAL（D19）+ FlashDB（D20/D21）

#### TC-QSPI-01：读 ID

- 步骤：Keil 调用 `qspi_flash_init()`、`qspi_flash_read_id()`
- 期望：
  - ID 在 `bsp_qspi_flash.h` 允许集合中（ZD25Q/W25Q）

#### TC-QSPI-02：扇区擦除 + 写入 + 读回一致性（含跨页）

- 步骤：
  1. 选测试地址（建议从 0 开始，避免覆盖后续可能的分区数据；若担心破坏数据，先只在空白新板上做）
  2. `qspi_flash_erase_sector(addr)`
  3. 写入 1KB pattern（包含跨 256B 页边界的写）
  4. 读回比对
- 期望：逐字节一致
- 需要你确认点：
  - 若板上已存放重要数据，先确认测试地址是否安全（或先整片备份）。

#### TC-FAL-01：FAL 初始化与分区表

- 步骤：
  1. 调用 `fal_init()`
  2. 调用 `fal_partition_find("fdb_kvdb")`、`fal_partition_find("fdb_tsdb")`
- 期望：
  - 分区存在，offset/len 与 `middlewares/bsp/fal/fal_cfg.h` 一致：
    - `fdb_kvdb` 在片内 flash `0x08080000` 起 512KB
    - `fdb_tsdb` 在 QSPI `qspi_nor` 全片

#### TC-FDB-01：KVDB 初始化与读写回归（需要 FreeRTOS 运行态）

- 前置：
  - 系统已进入 FreeRTOS（`wk_freertos_init()` 已启动）
- 步骤：
  1. Keil 调用 `settings_init_load()`（内部会 `cfg_kv_init()` 并 printf）
  2. 准备 32 字节结构体数据（例如全 0xA5），调用 `cfg_save_sample(&buf32)`
  3. 清空另一个缓冲，调用 `cfg_load_sample(&buf32_2)` 读回并比对
- 期望：
  - 串口看到 `[FDB] KVDB init OK`
  - 读回数据与写入一致
- 记录：
  - 记录写入/读回的字节摘要（前 8 字节即可）+ 串口日志

#### TC-FDB-02：TSDB 初始化 + 追加 + 时间范围遍历（需要 FreeRTOS 运行态）

- 步骤：
  1. Keil 调用 `fdb_start_tasks()`（内部 `fdb_tsdb_init` 并 printf）
  2. 调用 `tsdb_event_append(event_type, body, body_len)` 追加若干条
  3. 用 Keil 断点/Watch 验证 `tsdb_iter_range(from,to,cb,...)` 能遍历到记录
- 期望：
  - 串口看到 `[FDB] TSDB init OK`
  - `tsdb_is_ready()` 返回 1
  - 追加成功返回 1；迭代回调被调用且 event_type/body_len 正确

> 说明：TSDB 的回调里喂狗 `feed_dog()`，若你在 Debug 下频繁停机，可能影响 WDT 行为；建议测试 FlashDB 时先暂不启用 WDT（或确认 WDT 未实际启用）。

### 5.8 RTC（D22）

#### TC-RTC-01：时间设置/读取一致

- 步骤：
  1. `rtc_set_time(26, 2, 18, 12, 0, 0)`（示例：2026-02-18 12:00:00）
  2. `rtc_get_time(&dt)` 读回
- 期望：
  - 年月日时分秒一致；week 合理

#### TC-RTC-02：断电保持

- 步骤：
  1. 设置时间后断电（保留后备电池/电容供电）
  2. 重新上电，调用 `bsp_rtc_init()` 后读回时间
- 期望：
  - 时间未被重置为默认值（`bsp_rtc_init()` 使用 BPR 魔数判断）

### 5.9 WDT（D23）

#### TC-WDT-01：复位源判定

- 步骤：
  1. 运行后调用 `bsp_wdt_enable()`（或确认 wk_wdt_init 已配置但未 enable）
  2. 故意长时间停止喂狗（例如在 Keil 中 Halt，或让系统不调用 `bsp_wdt_feed`）
  3. 观察系统是否复位
  4. 复位后调用 `bsp_wdt_is_reset()` 看是否返回 1
- 期望：
  - 能触发 WDT 复位，并能识别复位来源

### 5.10 CRC16 Modbus（D27）

#### TC-CRC-01：已知向量对比

- 用例数据：ASCII `"123456789"`（9字节）
- 期望 CRC16/MODBUS：`0x4B37`
- 步骤：
  1. 在 Keil 中准备 `uint8_t buf[] = "123456789";`
  2. 调用 `crc16_modbus(buf, 9)`
- 期望：
  - 返回 `0x4B37`

### 5.11 USB（D24/D26）

#### TC-USB-CDC-01：PC 虚拟串口回显（黑盒）

- 前置：
  - OTGFS2 连 PC，设备枚举出 CDC（COM 口）
- 步骤（Python 示例）：

```python
import os, time, serial

PORT = "COM20"   # 改成实际枚举口
BAUD = 115200    # CDC linecoding 默认 115200（不影响 USB 传输本身）

payload = os.urandom(256)
with serial.Serial(PORT, BAUD, timeout=1) as ser:
    ser.reset_input_buffer()
    ser.write(payload)
    echo = ser.read(len(payload))
    assert echo == payload, (len(echo), echo[:16])
print("PASS: CDC echo")
```

- 期望：
  - 回显内容与发送完全一致（`usb_app.c` 做了 echo）

#### TC-USB-MSC-OTA-01：U盘挂载与固件校验（黑盒）

- 前置：
  - OTGFS1 插 U盘（FAT32）
  - 串口已连接 USART1（看 `[USB]/[OTA]` log）
- 步骤：
  1. 准备 `firmware.bin` 到 U盘根目录，格式见下方“固件文件生成”
  2. 插入 U盘
  3. 观察串口 log：挂载、打开文件、读取头、CRC 校验结果
- 期望：
  - 正确文件：出现 `[OTA] CRC校验通过` 并提示“等待实现Flash写入逻辑”
  - 错误文件：能打印明确错误（魔数/大小/CRC）

##### 固件文件生成（Python 示例）

```python
import os, struct, zlib

MAGIC = 0x4F544146  # "OTAF"
VERSION = 1
data = os.urandom(4096)
crc32 = zlib.crc32(data) & 0xFFFFFFFF
hdr = struct.pack("<IIII", MAGIC, VERSION, len(data), crc32)

with open("firmware.bin", "wb") as f:
    f.write(hdr)
    f.write(data)
print("firmware.bin generated:", len(data), "CRC32=", hex(crc32))
```

---

## 6. 测试记录模板（建议每个用例一行）

| 用例ID | 日期 | 固件版本/提交 | 测试人 | 结果(P/F) | 关键数据/截图 | 备注/问题 |
|---|---|---|---|---|---|---|
| TC-IO-02 | 2026-02-xx | xxx | xxx | P | 电平记录/LA截图 |  |

---

## 7. 需要你最终确认/仪器确认的项目清单

这些项我可以给出“怎么测”和“看什么”，但最终需要你用硬件现象做确认并记录：

1. **GPIO 输出是否真的驱动到外部执行器**：继电器吸合/阀动作（建议万用表+听声/看灯）。
2. **H桥电机方向**：`MOTOR_EMPTY/RESTORE` 与实际电机机械方向一致性（建议拍视频/记录）。
3. **CAN 电机联动**：电机板是否按命令输出脉冲/转动（逻辑分析仪/示波器看 STEP/DIR 或电机运行状态）。
4. **NTC 温度精度**：需要你提供可信温度基准（温度计）做对比并决定误差门槛。
5. **4-20mA 精度**：需要你确认电流源输出（串联电流表复核一次）。
6. **串口屏显示效果**：地址/页面是否与屏工程一致，需要你对照屏资源确认。

---

## 8. 后续建议（不在本阶段改代码）

为提高“自反馈自动化程度”，后续建议新增一个 `driver_test` 模式（UART1 命令行/菜单），把上述 Keil 调试调用改为“串口命令触发”，并把结果打印到 USART1/USB CDC。这样就能在脱离调试器的情况下做回归测试。


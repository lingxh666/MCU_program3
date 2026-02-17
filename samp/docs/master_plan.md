# 采样留样仪固件开发总计划

> **平台:** AT32F435/437 (ARM Cortex-M4, 288MHz)
> **RTOS:** FreeRTOS
> **项目路径:** `D:\MCU_program3\samp`
> **参考旧工程:** `D:\MCU_program3\samplingB` (AT32F403A)

---

## 目录

- [一、开发阶段总览](#一开发阶段总览)
- [二、硬件引脚总表](#二硬件引脚总表)
  - [2.1 GPIO 输出（继电器/执行器）](#21-gpio-输出继电器执行器)
  - [2.2 GPIO 输入（传感器/开关）](#22-gpio-输入传感器开关)
  - [2.3 ADC1 通道分配](#23-adc1-通道分配)
  - [2.4 ADC2 通道分配](#24-adc2-通道分配)
  - [2.5 UART 分配](#25-uart-分配)
  - [2.6 CAN 总线](#26-can-总线)
  - [2.7 QSPI2 Flash](#27-qspi2-flash)
  - [2.8 USB](#28-usb)
  - [2.9 其他外设](#29-其他外设)
- [三、第一阶段：驱动开发与测试](#三第一阶段驱动开发与测试)
  - [3.1 驱动清单](#31-驱动清单)
  - [3.2 各驱动详细说明](#32-各驱动详细说明)
- [四、第二阶段：业务逻辑与流程图](#四第二阶段业务逻辑与流程图)
- [五、第三阶段：FreeRTOS 任务规划与整体代码](#五第三阶段freertos-任务规划与整体代码)
- [六、第四阶段：集成测试与 Bug 修复](#六第四阶段集成测试与-bug-修复)

---

## 一、开发阶段总览

| 阶段 | 内容 | 目标 |
|------|------|------|
| **第一阶段** | 编写并测试所有驱动 | 每个驱动独立可验证 |
| **第二阶段** | 编写整体业务逻辑，画出流程图 | 明确采样/送样/留样/瞬时等完整流程 |
| **第三阶段** | 确定 FreeRTOS 任务规划，完成整体代码 | 任务分配、优先级、IPC 机制确定 |
| **第四阶段** | 集成测试与 Bug 修复 | 系统稳定运行 |

---

## 二、硬件引脚总表

### 2.1 GPIO 输出（继电器/执行器）

| 功能 | GPIO 引脚 | 电流检测 ADC 引脚 | ADC 通道 | 备注 |
|------|-----------|-------------------|----------|------|
| 进水阀 | PA10 | PC0 | ADC1_CH10 | |
| 瞬时阀 | PA8 | PC1 | ADC1_CH11 | |
| 送留样阀 | PC9 | PC4 | ADC1_CH14 | |
| A排水 | PC8 | PA7 | ADC1_CH7 | |
| B排水 | PD15 | PA6 | ADC1_CH6 | |
| A搅拌 | PD14 | PA5 | ADC1_CH5 | |
| B搅拌 | PD13 | PA4 | ADC1_CH4 | |
| 出水阀A | PD12 | PA3 | ADC1_CH3 | |
| 出水阀B | PD11 | — | — | |
| 锁开关 | PD10 | — | — | 无电流检测 |
| 外接泵 | PB10 | — | — | 无电流检测 |
| 备用1 | PB11 | — | — | |
| 备用2 | PB12 | — | — | |
| 备用3 | PB13 | — | — | |
| 瓶排空电机 | PE14+PE15 | PA2 | ADC1_CH2 | H桥控制 |

**瓶排空电机 H桥逻辑：**
- PE14=HIGH, PE15=LOW → 排空方向
- PE14=LOW, PE15=HIGH → 复位方向
- PE14=LOW, PE15=LOW → 停止

### 2.2 GPIO 输入（传感器/开关）

| 功能 | GPIO 引脚 | 触发方式 | 备注 |
|------|-----------|----------|------|
| 拨码开关 SW1 | PE11 | 电平读取 | 地址/模式选择 |
| 拨码开关 SW2 | PE12 | 电平读取 | 地址/模式选择 |
| 拨码开关 SW3 | PE13 | 电平读取 | 地址/模式选择 |
| 采样液位 | PC12 | 电平读取 | |
| 送样液位 | PD2 | 电平读取 | |
| 留样液位 | PD3 | 电平读取 | |
| Wiegand D0 | PD4 | EXINT4 上升沿 | 门禁刷卡 |
| Wiegand D1 | PD7 | EXINT7 上升沿 | 门禁刷卡 |
| 瓶原点 | PE5 | 电平读取 | 留样转盘 |
| 瓶到位 | PE6 | 电平读取 | 留样转盘 |
| 采样触发 | PB7 | 电平读取 | |
| 送样触发 | PB9 | 电平读取 | |
| 留样触发 | PE2 | 电平读取 | |
| 锁状态 | PE3 | 电平读取 | |
| 备用输入 | PE4 | 电平读取 | |

### 2.3 ADC1 通道分配

**DMA 连续扫描（12通道）— 继电器/阀门电流监测 + 校准基准 + 冰箱温度：**

| 序号 | 引脚 | ADC 通道 | 功能 | 备注 |
|------|------|----------|------|------|
| 1 | PA0 | CH0 | 2.5V 基准校准 | |
| 2 | PA2 | CH2 | 瓶排空电机电流 | |
| 3 | PA3 | CH3 | 出水阀A电流 | |
| 4 | PA4 | CH4 | B搅拌电流 | |
| 5 | PA5 | CH5 | A搅拌电流 | |
| 6 | PA6 | CH6 | B排水电流 | |
| 7 | PA7 | CH7 | A排水电流 | |
| 8 | PC0 | CH10 | 进水阀电流 | |
| 9 | PC1 | CH11 | 瞬时阀电流 | |
| 10 | PC4 | CH14 | 送留样阀电流 | |
| 11 | PC5 | CH15 | 冰箱温度1 | NTC3950, 10KΩ, 10K分压 |
| 12 | PB0 | CH8 | 冰箱温度2 | NTC3950, 10KΩ, 10K分压 |

### 2.4 ADC2 通道分配

**按需单次转换（2通道）— 4-20mA 检测：**

| 引脚 | ADC 通道 | 功能 | 硬件 |
|------|----------|------|------|
| PC2 | CH12 | 4-20mA 通道1 | INA180A2 (50V/V), 3Ω采样电阻 |
| PA1 | CH1 | 4-20mA 通道2 | INA180A2 (50V/V), 3Ω采样电阻 |

### 2.5 UART 分配

| 外设 | TX | RX | 波特率 | DMA接收 | 功能 | 备注 |
|------|----|----|--------|---------|------|------|
| USART1 | PA9 | — | 9600 | 否 | 调试打印 | 仅TX |
| USART2 | PD5 | PD6 | 9600 | DMA1_CH2 | 数采仪(485/232) | PA15选择485/232模式 |
| USART3 | PD8 | PD9 | 9600 | DMA1_CH3 | 蓝牙模块 | |
| UART4 | PC10 | PC11 | 9600 | DMA1_CH4 | 串口屏 | 迪文屏协议 |
| UART5 | PB6 | PB5 | 9600 | DMA1_CH5 | 西安485 | Modbus |
| USART6 | PC6 | PC7 | 9600 | DMA1_CH6 | 4G模块 | |
| UART7 | PB4 | PB3 | 9600 | DMA1_CH7 | 485备用 | |
| UART8 | PE1 | PE0 | 9600 | DMA2_CH1 | AD模块 | 外部AD数据解析 |

### 2.6 CAN 总线

| 外设 | TX | RX | 波特率 | 备注 |
|------|----|----|--------|------|
| CAN1 | PD1 | PD0 | 500kbps | 72分频, 1+6+1 TQ |

**CAN ID 规划：**
- 电机控制帧: 0x200+motor_id（上位机→电机板，每电机独立ID）
- 电机查询帧: 0x300（上位机→电机板，查询所有电机状态）
- 电机应答帧: 0x100+motor_id（电机板→上位机，状态回复）
- 预留 HX711 称重模块 ×2
- 预留 NFC 模块

> 详细协议见 [附录B：CAN电机控制协议](#附录bcan电机控制协议)

### 2.7 QSPI2 Flash

| 引脚 | 功能 |
|------|------|
| PB1 | SCK |
| PB8 | CS |
| PE7 | IO0 |
| PE8 | IO1 |
| PE9 | IO2 |
| PE10 | IO3 |

用途：外部 Flash 存储，通过 FAL 抽象层供 FlashDB (KVDB/TSDB) 使用。

### 2.8 USB

| 外设 | 功能 | 模式 | 备注 |
|------|------|------|------|
| USB OTG FS1 | MSC | 主设备(Host) | 读取U盘bin/hex文件进行固件升级 |
| USB OTG FS2 | CDC | 从设备(Device) | 模拟串口，上位机通信（设置/升级/记录导出） |

### 2.9 其他外设

| 外设 | 功能 | 备注 |
|------|------|------|
| ERTC | 实时时钟 | 电池供电保持 |
| WDT | 看门狗 | 系统异常复位 |
| CRC | CRC校验 | 数据完整性 |
| TMR2 | 1Hz 定时器 | 秒级周期任务 |
| TMR3 | 1Hz 定时器 | 秒级周期任务 |
| TMR4 | 1000Hz 定时器 | 1ms周期任务 |
| TMR6 | 1000Hz 定时器 | 1ms周期任务 |
| TMR7 | 1000Hz 定时器 | 1ms周期任务 |
| TMR8 | 0.2Hz 定时器 | 5s周期任务 |

---

## 三、第一阶段：驱动开发与测试

### 3.1 驱动清单

| 编号 | 驱动名称 | 依赖外设 | 优先级 | 测试方法 |
|------|----------|----------|--------|----------|
| D01 | GPIO 输出驱动（继电器/阀门宏） | GPIO | 高 | 逐个开关继电器，万用表验证 |
| D02 | GPIO 输出驱动（H桥瓶排空电机） | GPIO PE14/PE15 | 高 | 正转/反转/停止验证 |
| D03 | GPIO 输入驱动（液位/触发/拨码/锁） | GPIO | 高 | 读取各引脚状态，对比实际 |
| D04 | ADC1 DMA 连续扫描驱动（10通道电流） | ADC1 + DMA | 高 | 开关阀门观察电流值变化 |
| D05 | ADC1 按需转换驱动（4-20mA） | ADC1 | 中 | 接信号源验证线性度 |
| D06 | ADC2 DMA 连续扫描驱动（NTC温度） | ADC2 + DMA | 中 | 对比温度计读数 |
| D07 | ADC 校准模块（2.5V基准） | ADC1 CH0 | 高 | 校准后验证精度 |
| D08 | USART1 调试打印驱动 | USART1 | 高 | printf输出验证 |
| D09 | USART2 485/232可切换驱动 | USART2 + PA15 | 高 | 分别测试485和232模式 |
| D10 | USART3 蓝牙驱动 | USART3 | 中 | 手机蓝牙连接收发 |
| D11 | UART4 串口屏协议驱动 | UART4 | 高 | 发送命令观察屏幕响应 |
| D12 | UART5 西安485 Modbus驱动 | UART5 | 中 | Modbus主站工具测试 |
| D13 | USART6 4G模块驱动 | USART6 | 中 | AT指令交互验证 |
| D14 | UART7 485备用驱动 | UART7 | 低 | 回环测试 |
| D15 | UART8 AD模块解析驱动 | UART8 | 中 | 接AD模块验证数据解析 |
| D16 | CAN1 电机控制驱动 | CAN1 | 高 | 发送控制帧，电机响应 |
| D17 | Wiegand 输入驱动 | EXINT4/7 | 中 | 刷卡读取卡号 |
| D18 | QSPI2 Flash驱动 | QSPI2 | 高 | 读写擦除验证 |
| D19 | FAL 抽象层适配 | QSPI2 Flash | 高 | FAL读写测试 |
| D20 | FlashDB KVDB驱动 | FAL | 高 | 键值对读写验证 |
| D21 | FlashDB TSDB驱动 | FAL | 高 | 时序数据追加/查询验证 |
| D22 | ERTC 实时时钟驱动 | ERTC | 高 | 设置/读取时间，断电保持 |
| D23 | WDT 看门狗驱动 | WDT | 中 | 喂狗/不喂狗验证复位 |
| D24 | USB OTG CDC驱动 | USB OTG FS1 | 中 | PC端虚拟串口通信 |
| D25 | USB OTG HID/MSC驱动 | USB OTG FS | 低 | U盘/HID设备识别 |
| D26 | Bootloader/OTA驱动 | Flash + UART/USB | 中 | 固件升级验证 |
| D27 | CRC 校验驱动 | CRC | 低 | 计算结果对比 |
| D28 | Modbus 协议栈（多区域变体） | UART | 高 | 大岳/大湖/四川/西安协议测试 |

### 3.2 各驱动详细说明

#### D01 — GPIO 输出驱动（继电器/阀门宏）

**文件:** `bsp_relay.h` / `bsp_relay.c`

提供统一的宏/函数接口控制所有继电器输出：

```c
// 阀门控制宏示例
#define INLET_VALVE_ON()      gpio_bits_set(GPIOA, GPIO_PINS_10)
#define INLET_VALVE_OFF()     gpio_bits_reset(GPIOA, GPIO_PINS_10)
#define INSTANT_VALVE_ON()    gpio_bits_set(GPIOA, GPIO_PINS_8)
#define INSTANT_VALVE_OFF()   gpio_bits_reset(GPIOA, GPIO_PINS_8)
// ... 其余阀门类似
```

需实现：
- 所有继电器的 ON/OFF 宏定义
- `relay_init()` 初始化所有输出引脚为低电平
- `relay_set(id, state)` 通用接口
- `relay_get_state(id)` 读取当前输出状态

#### D02 — GPIO 输出驱动（H桥瓶排空电机）

**文件:** `bsp_bottle_motor.h` / `bsp_bottle_motor.c`

控制 PE14/PE15 组成的 H桥驱动瓶排空电机：

需实现：
- `bottle_motor_empty()` — PE14=HIGH, PE15=LOW，排空方向
- `bottle_motor_restore()` — PE14=LOW, PE15=HIGH，复位方向
- `bottle_motor_stop()` — PE14=LOW, PE15=LOW，停止
- 电流检测集成（ADC1_CH2 / PA2），判断电机堵转/空转

#### D03 — GPIO 输入驱动（液位/触发/拨码/锁）

**文件:** `bsp_input.h` / `bsp_input.c`

统一管理所有数字输入引脚，提供消抖读取：

需实现：
- `input_init()` 初始化所有输入引脚（上拉/下拉按实际电路）
- `input_read(id)` 读取单个输入状态（带软件消抖）
- `input_get_dip_switch()` 读取3位拨码开关组合值（0~7）
- 输入ID枚举：`INPUT_LEVEL_SAMPLE`, `INPUT_LEVEL_DELIVERY`, `INPUT_LEVEL_RETAIN`, `INPUT_TRIG_SAMPLE`, `INPUT_TRIG_DELIVERY`, `INPUT_TRIG_RETAIN`, `INPUT_LOCK_STATE`, `INPUT_BOTTLE_ORIGIN`, `INPUT_BOTTLE_POS`

#### D04 — ADC1 DMA 连续扫描驱动（10通道电流监测）

**文件:** `bsp_adc_current.h` / `bsp_adc_current.c`

ADC1 配置为 DMA 循环扫描模式，持续采集10个通道的继电器/阀门电流：

需实现：
- ADC1 改为 DMA 循环扫描模式（当前是单次转换，需修改 `wk_adc1_init()`）
- DMA 双缓冲或环形缓冲，存储10通道原始值
- `adc_current_get(channel)` 获取指定通道的滤波后电流值（mA）
- `adc_current_is_active(channel)` 判断阀门是否正常通电
- 结合 D07 的 2.5V 基准校准结果修正 ADC 值

**通道映射：** CH0(基准), CH2(瓶电机), CH3(出水阀A), CH4(B搅拌), CH5(A搅拌), CH6(B排水), CH7(A排水), CH10(进水阀), CH11(瞬时阀), CH14(送留样阀)

#### D05 — ADC1 按需转换驱动（4-20mA）

**文件:** `bsp_adc_420ma.h` / `bsp_adc_420ma.c`

ADC1 的 CH12(PC2) 和 CH1(PA1) 用于 4-20mA 信号检测，按需触发单次转换：

硬件参数：
- 电流采样电阻：3Ω
- 放大器：INA180A2IDBVR，增益 50V/V
- 4mA → 3×0.004×50 = 0.6V，20mA → 3×0.020×50 = 3.0V

需实现：
- `adc_420ma_read(channel)` 触发单次转换并返回原始值
- `adc_420ma_get_current(channel)` 返回计算后的电流值（mA）
- 线性校准接口

#### D06 — ADC2 DMA 连续扫描驱动（NTC温度）

**文件:** `bsp_adc_temp.h` / `bsp_adc_temp.c`

ADC2 独立 DMA 循环扫描，采集2路 NTC3950 温度：

硬件参数：
- NTC3950，标称 10KΩ@25°C
- 上拉分压电阻 10KΩ，参考电压 3.3V
- 温度计算：Steinhart-Hart 方程或查表法

需实现：
- ADC2 改为 DMA 循环扫描模式（当前是单次转换，需修改 `wk_adc2_init()`）
- `adc_temp_get(channel)` 返回温度值（°C，精度0.1°C）
- 滤波处理（滑动平均）

#### D07 — ADC 校准模块（2.5V基准）

**文件:** `bsp_adc_cal.h` / `bsp_adc_cal.c`

利用 ADC1_CH0 (PA0) 接入的 2.5V 精密基准源校准 ADC：

需实现：
- `adc_cal_run()` 读取 CH0 原始值，计算校准系数
- `adc_cal_apply(raw)` 将原始 ADC 值转换为校准后的电压值
- 校准系数 = 2.5V / (CH0原始值 × 3.3V / 4096)
- 校准系数存储到 KVDB，上电自动加载

#### D08 — USART1 调试打印驱动

**文件:** 复用 `wk_config` 已有初始化，重定向 `fputc`

USART1 仅 TX (PA9)，9600 波特率，用于 `printf` 调试输出：

需实现：
- 重定向 `fputc` 到 USART1
- 可选：环形缓冲 + DMA 发送，避免阻塞

#### D09 — USART2 485/232可切换驱动

**文件:** `bsp_uart_collector.h` / `bsp_uart_collector.c`

USART2 (PD5/PD6) 通过 PA15 切换 485/232 模式，连接数采仪：

需实现：
- `collector_set_mode(MODE_485)` / `collector_set_mode(MODE_232)` — 控制 PA15
- `collector_send(buf, len)` 发送数据
- `collector_recv(buf, max_len, timeout)` 接收数据（DMA + 空闲中断）
- Modbus RTU 协议封装（大岳/大湖/四川协议变体）

#### D10 — USART3 蓝牙驱动

**文件:** `bsp_uart_bluetooth.h` / `bsp_uart_bluetooth.c`

USART3 (PD8/PD9)，9600，DMA接收，连接蓝牙模块：

需实现：
- `bluetooth_send(buf, len)` 发送数据
- `bluetooth_recv(buf, max_len, timeout)` 接收数据
- AT指令配置接口（波特率、名称、密码等）

#### D11 — UART4 串口屏协议驱动

**文件:** `bsp_screen.h` / `bsp_screen.c`

UART4 (PC10/PC11)，9600，DMA接收，连接迪文串口屏：

协议格式：`5A A5 [长度] [命令] [地址H] [地址L] [数据...]`
- 命令 0x82：写变量
- 命令 0x83：读变量

需实现：
- `screen_write_var(addr, data, len)` 写屏幕变量
- `screen_read_var(addr, len)` 读屏幕变量
- `screen_switch_page(page_id)` 切换页面
- `screen_send_notify(buf, len, retry)` 带重试的发送
- 接收解析与消息分发（详见 `screen_refactor.md`）

#### D12 — UART5 西安485 Modbus驱动

**文件:** `bsp_uart_xian.h` / `bsp_uart_xian.c`

UART5 (PB6/PB5)，9600，DMA接收，西安区域 Modbus 485通信：

需实现：
- Modbus RTU 从站协议
- 寄存器映射表（采样参数、状态、控制）
- `xian_modbus_poll()` 轮询处理

#### D13 — USART6 4G模块驱动

**文件:** `bsp_uart_4g.h` / `bsp_uart_4g.c`

USART6 (PC6/PC7)，9600，DMA接收，连接4G通信模块：

需实现：
- AT指令收发框架（带超时和重试）
- `module_4g_init()` 模块初始化（SIM卡检测、网络注册）
- `module_4g_send_data(buf, len)` 数据上传
- `module_4g_recv_data(buf, max_len, timeout)` 数据接收
- TCP/UDP 连接管理

#### D14 — UART7 485备用驱动

**文件:** `bsp_uart_spare.h` / `bsp_uart_spare.c`

UART7 (PB4/PB3)，9600，DMA接收，485备用通道：

需实现：
- 通用 485 收发接口
- 可配置为 Modbus RTU 主站或从站

#### D15 — UART8 AD模块解析驱动

**文件:** `bsp_uart_admodule.h` / `bsp_uart_admodule.c`

UART8 (PE1/PE0)，9600，DMA接收，连接外部AD模块：

需实现：
- 数据帧解析（根据AD模块协议）
- `admodule_get_value(channel)` 获取解析后的测量值
- 数据校验（CRC/校验和）

#### D16 — CAN1 电机控制驱动

**文件:** `bsp_can_motor.h` / `bsp_can_motor.c`

CAN1 (PD0/PD1)，500kbps，控制4个蠕动泵电机：

需实现：
- `can_motor_init()` CAN过滤器配置（接收0x100-0x103状态应答帧）
- `can_motor_send_run(motor_id, rpm, dir, accel)` 发送启动控制帧（0x200+motor_id）
- `can_motor_send_stop(motor_id)` 发送减速停止控制帧
- `can_motor_send_immediate_stop(motor_id)` 发送立即停止控制帧
- `can_motor_query_all()` 发送查询帧（0x300），触发电机板回复所有状态
- `can_motor_get_status(motor_id)` 从接收缓存读取电机状态（解析0x100+motor_id应答帧）
- 每个电机独立CAN ID，详见 [附录B：CAN电机控制协议](#附录bcan电机控制协议)
- 预留 HX711 称重和 NFC 的 CAN ID

#### D17 — Wiegand 输入驱动

**文件:** `bsp_wiegand.h` / `bsp_wiegand.c`

PD4(D0)/PD7(D1) 外部中断，Wiegand 26/34 协议门禁刷卡：

需实现：
- EXINT4/EXINT7 上升沿中断处理
- `wiegand_init()` 初始化中断
- `wiegand_get_card_id(uint32_t *card_id)` 获取卡号（非阻塞）
- 超时判断帧结束（250ms无脉冲视为一帧结束）
- 奇偶校验

#### D18 — QSPI2 Flash驱动

**文件:** `bsp_qspi_flash.h` / `bsp_qspi_flash.c`

QSPI2 四线模式连接外部 Flash（PB1/PB8/PE7-PE10）：

需实现：
- `qspi_flash_init()` 初始化 QSPI 及 Flash 芯片
- `qspi_flash_read(addr, buf, len)` 读取数据
- `qspi_flash_write(addr, buf, len)` 写入数据（自动处理页编程）
- `qspi_flash_erase_sector(addr)` 扇区擦除
- `qspi_flash_erase_block(addr)` 块擦除
- `qspi_flash_read_id()` 读取芯片ID

#### D19 — FAL 抽象层适配

**文件:** `fal_cfg.h` / `fal_flash_qspi.c`

FAL (Flash Abstraction Layer) 对接 QSPI Flash，为 FlashDB 提供统一接口：

需实现：
- FAL Flash 设备注册（init/read/write/erase）
- 分区表定义（KVDB区、TSDB区、OTA区等）
- `fal_init()` 初始化验证

#### D20 — FlashDB KVDB驱动

**文件:** `app_flashdb.h` / `app_flashdb.c`

基于 FAL 的键值数据库，存储系统配置参数：

需实现：
- `kvdb_init()` 初始化 KVDB
- `kvdb_get(key, value, len)` 读取键值
- `kvdb_set(key, value, len)` 写入键值
- 缓存机制（减少 Flash 读写次数）
- 脏标记 + 定时刷写策略

#### D21 — FlashDB TSDB驱动

**文件:** `app_flashdb.h` / `app_flashdb.c`（与D20同文件）

基于 FAL 的时序数据库，存储采样/送样/留样日志记录：

需实现：
- `tsdb_init()` 初始化 TSDB
- `tsdb_append(event_type, data, len)` 追加记录
- `tsdb_query(start_time, end_time, callback)` 按时间范围查询
- 缓存写入机制（批量刷写）
- 记录类型编码：0x00F0-0x00FF(采样), 0x00E0-0x00EF(送样), 0x00D0-0x00DF(留样)

#### D22 — ERTC 实时时钟驱动

**文件:** `bsp_rtc.h` / `bsp_rtc.c`

AT32F435 内置 ERTC，电池供电保持：

需实现：
- `rtc_get_time(year, month, day, hour, min, sec)` 读取时间
- `rtc_set_time(year, month, day, hour, min, sec)` 设置时间
- `rtc_get_timestamp()` 获取 Unix 时间戳
- 闹钟中断（可选，用于定时采样唤醒）

#### D23 — WDT 看门狗驱动

**文件:** 复用 `wk_config` 已有初始化

需实现：
- `wdt_feed()` 喂狗函数
- 在主循环或专用任务中定期调用
- 超时时间根据系统最长阻塞操作设定

#### D24 — USB OTG2 CDC 虚拟串口驱动（Device模式）

**文件:** `project/src/usb_app.c`, `project/src/cdc_class.c`, `project/src/cdc_desc.c`

USB OTG2 (PB14/PB15) 作为 CDC 虚拟串口 Device，用于PC端调试/配置：

已实现：
- OTG2 初始化为 CDC Device（USB_HIGH_SPEED_CORE_ID, USB_OTG2_ID）
- `usb_vcp_send_data()` / `usb_vcp_get_rxdata()` 收发接口
- 端点配置：INT=0x82, BULK_IN=0x81, BULK_OUT=0x01, 包大小64字节
- 默认线路编码：115200 8N1

#### D25 — USB OTG1 MSC Host 驱动 + U盘OTA升级

**文件:** `project/src/usb_app.c`, `project/src/usbh_user.c`, `project/src/usbh_msc_diskio.c`

USB OTG1 (PA11/PA12) 作为 MSC Host，插入U盘后自动检测并执行OTA升级：

已实现：
- OTG1 初始化为 MSC Host（USB_FULL_SPEED_CORE_ID, USB_OTG1_ID, uhost_msc_class_handler）
- FatFS 文件系统集成（只读模式，ff.c/ffsystem.c/ffunicode.c）
- USB MSC ↔ FatFS 磁盘IO桥接（usbh_msc_diskio.c）
- OTA升级状态机：U盘插入 → 挂载 → 查找 firmware.bin → 读取固件头 → CRC32校验
- 固件头格式：魔数(4B, "OTAF") + 版本号(4B) + 数据长度(4B) + CRC32(4B)
- `usb_ota_get_result()` 外部查询接口（0=未完成, 1=成功, 2=失败）

待完成：
- Flash写入逻辑（需确定Flash分区规划后实现）
- 升级完成后系统重启逻辑
- 双分区或标志位断电保护方案

#### D26 — Bootloader/OTA驱动

**文件:** `bsp_bootloader.h` / `bsp_bootloader.c`

固件升级支持（参考 samplingB 中已有 bootloader 工程）：

需实现：
- Flash 分区：Bootloader区 + App区 + Download区
- `bootloader_check_update()` 检查是否有待升级固件
- `bootloader_write_firmware(offset, buf, len)` 写入固件数据
- `bootloader_verify_firmware()` 校验固件完整性（CRC32）
- `bootloader_jump_to_app()` 跳转到应用程序
- 支持 UART/USB/4G 多通道升级

#### D27 — CRC 校验驱动

**文件:** 复用硬件 CRC 外设

需实现：
- `hw_crc32_calc(buf, len)` 硬件加速 CRC32 计算
- 用于固件校验、通信数据校验

#### D28 — Modbus 协议栈（多区域变体）

**文件:** `bsp_modbus.h` / `bsp_modbus.c` + 各变体子文件

统一 Modbus RTU 协议栈，支持多区域变体：

需实现：
- Modbus RTU 帧封装/解析（CRC16校验）
- 大岳协议变体（USART2 485模式）
- 大湖协议变体（USART2 485模式）
- 四川协议变体（USART2 485模式）
- 西安协议变体（UART5 专用）
- 寄存器映射表（读/写寄存器定义）
- 从站地址通过拨码开关(DIP SW1-SW3)配置

---

## 四、第二阶段：业务逻辑与流程图

本阶段在所有驱动测试通过后开展，梳理完整业务流程。

### 4.1 核心业务流程

#### 4.1.1 采样流程

```
开始 → 检查前置条件(液位/阀门状态)
     → 开进水阀 → 启动采样泵(CAN电机)
     → 等待采样体积达标(流量计/定时)
     → 停泵 → 关进水阀
     → 反吹清管
     → 记录日志(TSDB) → 更新屏幕状态
     → 结束
```

#### 4.1.2 送样流程

```
开始 → 检查送样液位
     → 开送留样阀(送样方向)
     → 启动送样泵(CAN电机)
     → 等待送样完成(液位/定时)
     → 停泵 → 关送留样阀
     → 记录日志(TSDB) → 更新屏幕状态
     → 结束
```

#### 4.1.3 留样流程

```
开始 → 检查留样液位
     → 转盘定位到目标瓶(CAN电机 + 瓶原点/瓶到位传感器)
     → 开送留样阀(留样方向)
     → 启动留样泵(CAN电机)
     → 等待留样完成(液位/定时)
     → 停泵 → 关送留样阀
     → 记录日志(TSDB) → 更新屏幕状态
     → 结束
```

#### 4.1.4 瞬时采样流程

```
开始 → 开瞬时阀(瞬时方向)
     → 启动采样泵
     → 等待采样完成
     → 停泵 → 关瞬时阀
     → 直接送样/留样（不经过桶）
     → 记录日志(TSDB)
     → 结束
```

### 4.2 辅助业务流程

#### 4.2.1 排水流程

```
开始 → 开排水阀(A或B)
     → 启动搅拌(可选)
     → 等待排水完成(定时)
     → 关排水阀 → 停搅拌
     → 结束
```

#### 4.2.2 瓶排空流程

```
开始 → 启动H桥电机(排空方向 PE14=H,PE15=L)
     → 监测电流(ADC1_CH2)判断完成
     → 停止电机(PE14=L,PE15=L)
     → 结束
```

#### 4.2.3 门禁/刷卡流程

```
开始 → Wiegand中断接收卡号
     → 校验卡号(KVDB中存储的授权列表)
     → 授权通过 → 开锁(PD10) → 记录日志
     → 授权失败 → 屏幕提示
     → 结束
```

#### 4.2.4 定时调度流程

```
系统启动 → 读取KVDB中的采样计划配置
        → 根据ERTC时间判断是否到达采样时刻
        → 到达 → 触发采样流程
        → 未到达 → 继续等待
        → 循环
```

### 4.3 待细化内容

以下内容在第二阶段实施时需要详细展开：

- 采样模式切换逻辑（定时/定量/混合/瞬时）
- A/B桶轮换策略
- 异常处理流程（阀门故障、泵堵转、液位异常）
- 电流监测报警阈值与处理
- 温度监控与冰箱控制逻辑
- 4G数据上报协议与时序
- 数采仪通信协议交互细节

---

## 五、第三阶段：FreeRTOS 任务规划与整体代码

### 5.1 任务初步规划

当前框架已预留 8 个任务、9 个队列、6 个二值信号量、8 个互斥量、1 个事件组。

**初步任务分配方案（待第二阶段完成后细化）：**

| 任务 | 功能 | 优先级 | 备注 |
|------|------|--------|------|
| Task01 | 采样/送样/留样主控 | 高 | 核心业务状态机 |
| Task02 | 串口屏通信 | 中高 | 屏幕收发与消息分发 |
| Task03 | 数采仪/Modbus通信 | 中 | USART2 485/232 |
| Task04 | 4G模块通信 | 中 | 数据上报 |
| Task05 | ADC采集与监控 | 中 | 电流/温度/4-20mA |
| Task06 | CAN电机控制 | 中高 | 蠕动泵控制 |
| Task07 | 系统管理 | 低 | WDT喂狗/KVDB刷写/日志 |
| Task08 | 蓝牙/备用通信 | 低 | USART3/UART5/UART7/UART8 |

### 5.2 IPC 机制规划

| IPC 类型 | 用途 |
|----------|------|
| 队列 Queue01-07 | 各 UART 接收数据传递到对应处理任务 |
| 队列 Queue08 | CAN 接收数据传递 |
| 队列 Queue09 | 屏幕发送队列（带重试） |
| 二值信号量 Sem01-06 | UART 空闲中断通知、屏幕ACK确认等 |
| 互斥量 Mutex01-08 | KVDB访问保护、共享状态保护等 |
| 事件组 Event01 | 采样/送样/留样流程状态事件标志 |

### 5.3 待确定事项

- 各任务栈大小（根据实际使用调整）
- 任务优先级精确数值
- 队列深度与消息结构体定义
- 定时器任务是否独立或合并到系统管理任务
- 中断优先级分组与 FreeRTOS configMAX_SYSCALL_INTERRUPT_PRIORITY 配置

---

## 六、第四阶段：集成测试与 Bug 修复

### 6.1 测试策略

| 测试类别 | 内容 | 方法 |
|----------|------|------|
| 单元测试 | 各驱动独立功能验证 | 第一阶段已完成 |
| 集成测试 | 多驱动协同工作 | 组合场景测试 |
| 流程测试 | 完整采样/送样/留样流程 | 端到端验证 |
| 压力测试 | 长时间连续运行 | 72小时不间断 |
| 异常测试 | 故障注入与恢复 | 断电/断线/堵转 |

### 6.2 重点测试场景

1. **采样全流程**：定时触发 → 采样 → A/B桶切换 → 排水 → 循环
2. **送样全流程**：触发送样 → 泵送 → 液位检测 → 完成
3. **留样全流程**：触发留样 → 转盘定位 → 泵送 → 瓶排空 → 完成
4. **瞬时采样**：瞬时触发 → 直通采样 → 送样/留样
5. **通信并发**：屏幕操作 + 数采仪查询 + 4G上报同时进行
6. **断电恢复**：采样过程中断电，上电后状态恢复
7. **看门狗复位**：任务死锁时WDT复位，系统恢复正常
8. **Flash存储**：KVDB/TSDB长期读写，掉电数据完整性

### 6.3 Bug 跟踪

- 使用统一的 Bug 记录表（可在本文档追加或单独文件）
- 每个 Bug 记录：编号、现象、复现步骤、原因分析、修复方案、验证结果
- 优先修复影响核心流程的 Bug

---

## 附录：文件结构规划

```
samp/
├── project/
│   ├── inc/                          # 头文件
│   │   ├── freertos_app.h
│   │   ├── at32f435_437_wk_config.h
│   │   ├── usb_app.h                # USB应用接口
│   │   ├── usb_conf.h               # USB配置（OTG1 Host + OTG2 Device）
│   │   ├── cdc_class.h / cdc_desc.h # CDC Device类
│   │   ├── usbh_user.h              # USB Host用户回调（OTA升级）
│   │   └── ffconf.h                  # FatFS配置（只读模式）
│   ├── src/
│   │   ├── usb_app.c                # USB初始化与主循环
│   │   ├── cdc_class.c / cdc_desc.c # CDC Device实现
│   │   ├── usbh_user.c              # MSC Host用户回调 + OTA升级逻辑
│   │   ├── usbh_msc_diskio.c        # FatFS ↔ USB MSC 磁盘IO桥接
│   │   ├── freertos_app.c
│   │   └── at32f435_437_wk_config.c
│   └── MDK_V5/
│       └── samp.uvprojx             # Keil工程文件
├── middlewares/
│   ├── bsp/                          # 板级支持包（驱动层）
│   │   ├── bsp_io.h/c               # D01/D02/D03 GPIO输入输出驱动 ✅
│   │   ├── bsp_adc.h/c              # D04-D07 ADC驱动 ✅
│   │   ├── bsp_qspi_flash.h/c       # D18 QSPI Flash驱动 ✅
│   │   ├── fal/                      # D19 FAL抽象层 ✅
│   │   │   ├── fal.c, fal_flash.c, fal_partition.c
│   │   │   ├── fal_flash_AT32_port.c
│   │   │   └── fal_flash_qspi_port.c
│   │   ├── flashDB/                  # D20/D21 FlashDB ✅
│   │   │   ├── app_flashdb.h/c      # KVDB/TSDB应用接口
│   │   │   ├── fdb.c, fdb_kvdb.c, fdb_tsdb.c
│   │   │   └── fdb_utils.c, fdb_file.c
│   │   └── ...                       # 其他驱动（待开发）
│   ├── usb_drivers/                  # USB底层驱动（AT32 SDK）
│   │   ├── inc/ src/                 # usb_core, usbd_core, usbh_core 等
│   ├── usbh_class/                   # USB Host类驱动
│   │   ├── usbh_msc/                # MSC Host类 ✅（已集成到工程）
│   │   ├── usbh_hid/                # HID Host类（已从工程移除，源文件保留）
│   │   └── usbh_cdc/                # CDC Host类（未使用）
│   ├── usbd_class/                   # USB Device类驱动（未使用）
│   ├── 3rd_party/
│   │   └── fatfs/source/             # FatFS文件系统库 ✅
│   │       ├── ff.c, ff.h, diskio.h
│   │       ├── ffsystem.c, ffunicode.c
│   │       └── diskio.c
│   └── freertos/                     # FreeRTOS
└── docs/
    ├── master_plan.md                # 本文档
    ├── usb_driver_plan.md            # USB驱动改造详细计划
    └── screen_refactor.md            # 串口屏重构文档
```

---

## 七、开发进度记录

### 第一阶段进度总览

| 编号 | 驱动名称 | 状态 | 备注 |
|------|----------|------|------|
| D01 | GPIO 输出驱动（继电器/阀门） | ✅ 已完成 | bsp_io.h/c |
| D02 | GPIO 输出驱动（H桥瓶排空电机） | ✅ 已完成 | bsp_io.h/c |
| D03 | GPIO 输入驱动（液位/触发/拨码/锁） | ✅ 已完成 | bsp_io.h/c |
| D04 | ADC1 DMA 连续扫描驱动（电流监测） | ✅ 已完成 | bsp_adc.h/c |
| D05 | ADC1 按需转换驱动（4-20mA） | ✅ 已完成 | bsp_adc.h/c |
| D06 | ADC2 DMA 连续扫描驱动（NTC温度） | ✅ 已完成 | bsp_adc.h/c |
| D07 | ADC 校准模块（2.5V基准） | ✅ 已完成 | bsp_adc.h/c |
| D08 | USART1 调试打印驱动 | ✅ 已完成 | wk_system.c已实现fputc重定向 |
| D09 | USART2 485/232可切换驱动 | ✅ 已完成 | bsp_uart_collector.h/c，PA15模式切换 |
| D10 | USART3 蓝牙驱动 | ✅ 已完成 | bsp_uart_bluetooth.h/c，占位符 |
| D11 | UART4 串口屏协议驱动 | ✅ 已完成 | bsp_screen.h/c，DWIN 5A A5协议帧层 |
| D12 | UART5 西安485 Modbus驱动 | ✅ 已完成 | bsp_uart_xian.h/c，传输层；Modbus协议待D28 |
| D13 | USART6 4G模块驱动 | ✅ 已完成 | bsp_uart_4g.h/c，AT指令框架+URC回调 |
| D14 | UART7 485备用驱动 | ✅ 已完成 | bsp_uart_spare.h/c，通用收发 |
| D15 | UART8 AD模块解析驱动 | ✅ 已完成 | bsp_uart_admodule.h/c，接收回调框架 |
| D16 | CAN1 电机控制驱动 | ✅ 已完成 | bsp_can_motor.h/c，4电机CAN帧控制+状态接收 |
| D17 | Wiegand 输入驱动 | ✅ 已完成 | bsp_wiegand.h/c，26/34位协议+TMR7超时检测 |
| D18 | QSPI2 Flash驱动 | ✅ 已完成 | bsp_qspi_flash.h/c |
| D19 | FAL 抽象层适配 | ✅ 已完成 | fal/ 目录 |
| D20 | FlashDB KVDB驱动 | ✅ 已完成 | flashDB/ 目录 |
| D21 | FlashDB TSDB驱动 | ✅ 已完成 | flashDB/ 目录 |
| D22 | ERTC 实时时钟驱动 | ✅ 已完成 | bsp_rtc.h/c，BPR魔数+北京时间 |
| D23 | WDT 看门狗驱动 | ✅ 已完成 | bsp_wdt.h/c |
| D24 | USB OTG2 CDC虚拟串口 | ✅ 已完成 | OTG2 Device CDC |
| D25 | USB OTG1 MSC Host + OTA | ✅ 已完成 | OTG1 Host MSC + FatFS，Flash写入待实现 |
| D26 | Bootloader/OTA驱动 | ⬜ 待开发 | 依赖D25的Flash写入逻辑 |
| D27 | CRC 校验驱动 | ✅ 已完成 | bsp_crc.h/c，硬件CRC16 Modbus |
| D28 | Modbus 协议栈 | ⬜ 待开发 | |

### 修改日志

#### 会话1 — BSP驱动 + FAL + FlashDB

**新增文件：**
- `middlewares/bsp/bsp_io.h/c` — GPIO输入输出驱动（继电器/H桥/传感器输入）
- `middlewares/bsp/bsp_adc.h/c` — ADC驱动（DMA扫描/4-20mA/NTC温度/2.5V校准）
- `middlewares/bsp/bsp_qspi_flash.h/c` — QSPI Flash驱动
- `middlewares/bsp/fal/` — FAL抽象层（5个.c文件，含AT32内部Flash和QSPI Flash端口）
- `middlewares/bsp/flashDB/` — FlashDB（6个.c文件，KVDB+TSDB+应用接口）

**修改文件：**
- `project/MDK_V5/samp.uvprojx` — 添加bsp/fal/flashDB的include路径和源文件组

**编译修复：**
- `app_flashdb.c`: KV键名从 `static const char*` 改为 `#define` 宏（ARM V5限制）
- `app_flashdb.c`: `feed_dog()` 改为static函数调用 `wdt_counter_reload()`
- `app_flashdb.c`: `tsdb_time_cb()` 改用 `ertc_calendar_get()` 替代不存在的 `ertc_counter_get()`
- `fal_partition.c`: 添加末尾换行符
- `bsp_adc.c`: `adc2_ch_map` 类型从 `uint8_t` 改为 `adc_channel_select_type`

**编译结果：** 0错误0警告，Code=39750

#### 会话2 — USB驱动改造（OTG1 HID→MSC, OTA升级）

**新增文件：**
- `middlewares/3rd_party/fatfs/source/` — FatFS库（从D:\MCU_program\BMS复制，6个文件）
- `project/inc/ffconf.h` — FatFS配置（只读模式，FF_CODE_PAGE=437）
- `project/src/usbh_msc_diskio.c` — FatFS↔USB MSC磁盘IO桥接
- `docs/usb_driver_plan.md` — USB驱动改造详细计划文档

**修改文件：**
- `project/src/usb_app.c`:
  - `#include "usbh_hid_class.h"` → `#include "usbh_msc_class.h"`
  - `otg_core_struct_fs1` 从 static 改为非static（供diskio外部引用）
  - `uhost_hid_class_handler` → `uhost_msc_class_handler`
- `project/src/usbh_user.c`: 完全重写，实现OTA升级状态机
  - U盘插入→挂载FatFS→查找firmware.bin→读取固件头→CRC32校验
  - 固件头：魔数"OTAF"+版本号+数据长度+CRC32
  - `usb_ota_get_result()` 外部查询接口
- `project/inc/usbh_user.h`: 添加 `usb_ota_get_result()` 声明
- `middlewares/usbh_class/usbh_msc/usbh_msc_class.c`: 添加 `(void)msize` 消除警告
- `project/MDK_V5/samp.uvprojx`:
  - 添加include路径：usbh_msc, fatfs/source
  - usbh_class组：HID文件替换为MSC文件（usbh_msc_class.c, usbh_msc_bot_scsi.c, usbh_msc_diskio.c）
  - 新增middlewares/fatfs组（ff.c, ffsystem.c, ffunicode.c）

**删除文件：**
- `project/src/usbh_hid_class.c` — HID Host类（应用层副本）
- `project/src/usbh_hid_keyboard.c` — HID键盘解码
- `project/src/usbh_hid_mouse.c` — HID鼠标解码

**编译结果：** 0错误0警告，Code=47418

#### 会话3 — 批次1基础驱动（D08/D22/D23/D27）

**新增文件：**
- `middlewares/bsp/bsp_rtc.h/c` — ERTC实时时钟驱动（BPR魔数判断首次初始化，北京时间UTC+8，时间戳转换）
- `middlewares/bsp/bsp_wdt.h/c` — WDT看门狗驱动（enable/feed/is_reset）
- `middlewares/bsp/bsp_crc.h/c` — 硬件CRC16 Modbus驱动（逐字节写CRC->dt寄存器）

**修改文件：**
- `project/src/main.c`: 替换 `wk_ertc_init()` 为 `bsp_rtc_init()`（避免每次上电重置时间），添加 `#include "bsp_rtc.h"`
- `project/MDK_V5/samp.uvprojx`: bsp组添加 bsp_rtc.c/bsp_wdt.c/bsp_crc.c

**D08说明：** USART1调试打印已在 `wk_system.c` 中实现（fputc重定向到USART1），无需额外开发

**编译结果：** 0错误0警告，Code=47514

### 待续工作

1. **D25 Flash写入逻辑** — OTA升级的实际Flash写入，需先确定Flash分区规划
2. **D26 Bootloader** — 独立Bootloader工程，配合OTA升级
3. **其余驱动开发** — D08-D17, D22-D23, D27-D28

---

## 附录B：CAN电机控制协议

> 本附录描述上位机（AT32F435，samp工程）与电机控制板（AT32F422，MOTRO4工程）之间的CAN通信协议。
> 电机板固件已完成，上位机按此协议发送/接收即可控制4路步进电机。

### B.1 总线参数

| 参数 | 值 |
|------|-----|
| CAN外设 | 上位机CAN1 (PD0/PD1)，电机板CAN1 |
| 波特率 | 500 kbps |
| 帧类型 | 标准帧（11位ID） |
| 数据长度 | 8字节 |

### B.2 电机ID定义

| motor_id | 电机 | 步进脉冲引脚 | 定时器 | 备注 |
|----------|------|-------------|--------|------|
| 0 | 采样电机 (STEP1) | PB9 | TMR17_CH1 | 独立定时器 |
| 1 | 送留样电机 (STEP2) | PB7 | TMR4_CH2 | 独立定时器 |
| 2 | 留样转盘 (STEP3) | PB5 | TMR3_CH2 | 与STEP4共享TMR3，互斥 |
| 3 | 加药泵 (STEP4) | PB4 | TMR3_CH1 | 与STEP3共享TMR3，互斥 |

> **互斥约束：** STEP3和STEP4共享TMR3，同一时刻只能运行其中一个。如果STEP3正在运行时发送STEP4启动命令，电机板会忽略该命令。上位机应先停止STEP3再启动STEP4（反之亦然）。两者可以使用不同的转速。

### B.3 帧ID分配

| 方向 | 帧ID | 用途 |
|------|------|------|
| 上位机 → 电机板 | 0x200 | 控制电机0（采样电机） |
| 上位机 → 电机板 | 0x201 | 控制电机1（送留样电机） |
| 上位机 → 电机板 | 0x202 | 控制电机2（留样转盘） |
| 上位机 → 电机板 | 0x203 | 控制电机3（加药泵） |
| 上位机 → 电机板 | 0x300 | 查询所有电机状态 |
| 电机板 → 上位机 | 0x100 | 电机0状态应答 |
| 电机板 → 上位机 | 0x101 | 电机1状态应答 |
| 电机板 → 上位机 | 0x102 | 电机2状态应答 |
| 电机板 → 上位机 | 0x103 | 电机3状态应答 |
| 预留 | 0x400-0x4FF | HX711 称重模块 ×2 |
| 预留 | 0x500-0x5FF | NFC 模块 |

### B.4 控制帧格式（上位机 → 电机板，0x200+motor_id）

| 字节 | 字段 | 类型 | 取值范围 | 说明 |
|------|------|------|----------|------|
| data[0] | cmd | uint8 | 0x01/0x02/0x03 | 命令码 |
| data[1] | dir | uint8 | 0x00/0x01 | 方向：0=正转CW, 1=反转CCW |
| data[2] | rpm_h | uint8 | 0-255 | 目标转速高字节 |
| data[3] | rpm_l | uint8 | 0-255 | 目标转速低字节 |
| data[4] | accel_h | uint8 | 0-255 | 加速度高字节（0=不修改） |
| data[5] | accel_l | uint8 | 0-255 | 加速度低字节（0=不修改） |
| data[6] | — | — | — | 保留 |
| data[7] | — | — | — | 保留 |

**命令码定义：**

| cmd | 名称 | 说明 |
|-----|------|------|
| 0x01 | RUN | 启动/变速。电机从当前速度加减速到目标rpm |
| 0x02 | STOP | 减速停止。电机按加速度参数减速到0后停止 |
| 0x03 | IMMEDIATE_STOP | 立即停止。电机立刻停止脉冲输出（急停） |

**参数说明：**
- `rpm`：目标转速，有效范围 1-600 RPM（电机板内部限幅）
- `accel`：加速度，单位 RPM/s，有效范围 10-2000。设为0表示不修改当前加速度（默认200 RPM/s）
- `dir`：仅在 RUN 命令时有效，STOP/IMMEDIATE_STOP 忽略此字段
- RPM与脉冲频率换算：`freq_hz = rpm × 3200 / 60`（3200脉冲/转，16细分×200步）

### B.5 查询帧格式（上位机 → 电机板，0x300）

| 字节 | 说明 |
|------|------|
| data[0-7] | 任意值（电机板不解析数据域） |

发送此帧后，电机板会依次回复4个状态应答帧（0x100-0x103）。

### B.6 状态应答帧格式（电机板 → 上位机，0x100+motor_id）

| 字节 | 字段 | 类型 | 取值 | 说明 |
|------|------|------|------|------|
| data[0] | state | uint8 | 0-3 | 电机状态 |
| data[1] | dir | uint8 | 0/1 | 当前方向：0=CW, 1=CCW |
| data[2] | rpm_h | uint8 | — | 当前实际转速高字节 |
| data[3] | rpm_l | uint8 | — | 当前实际转速低字节 |
| data[4] | current_h | uint8 | — | 电机电流高字节（mA） |
| data[5] | current_l | uint8 | — | 电机电流低字节（mA） |
| data[6] | monitor | uint8 | 0-2 | 监控状态 |
| data[7] | fault | uint8 | 位域 | 故障码 |

**state 电机状态：**

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | STOPPED | 已停止 |
| 1 | ACCELERATING | 加速中 |
| 2 | RUNNING | 匀速运行 |
| 3 | DECELERATING | 减速中 |

**monitor 监控状态：**

| 值 | 名称 | 说明 |
|----|------|------|
| 0 | NORMAL | 正常 |
| 1 | WARNING | 警告（电流偏高/偏低但未超限） |
| 2 | FAULT | 故障（已触发保护，电机被停止） |

**fault 故障码（位域，可组合）：**

| 位 | 名称 | 说明 |
|----|------|------|
| bit0 | OVERCURRENT | 过流（电流超过上限阈值） |
| bit1 | UNDERCURRENT | 欠流/断线（运行中电流低于下限阈值） |
| bit2 | STALL | 堵转（电流持续超限） |

### B.7 通信示例

**示例1：启动电机0，正转300RPM，加速度500RPM/s**

```
上位机发送: ID=0x200, DLC=8
data = [0x01, 0x00, 0x01, 0x2C, 0x01, 0xF4, 0x00, 0x00]
        cmd   dir   rpm=300      accel=500
```

**示例2：电机0减速停止**

```
上位机发送: ID=0x200, DLC=8
data = [0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
        cmd   (其余字段忽略)
```

**示例3：查询所有电机状态**

```
上位机发送: ID=0x300, DLC=8
data = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]

电机板回复4帧:
  ID=0x100: [0x02, 0x00, 0x01, 0x2C, 0x01, 0x90, 0x00, 0x00]
             运行   正转  rpm=300      400mA    正常  无故障
  ID=0x101: [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
             停止
  ID=0x102: [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
             停止
  ID=0x103: [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
             停止
```

### B.8 上位机开发指南

#### CAN过滤器配置

上位机需配置CAN过滤器接收电机状态应答帧（0x100-0x103）：

```c
/* AT32F435 CAN1 过滤器配置示例 */
/* 接收 0x100-0x103 状态应答帧 */
can_filter.filter_id_high    = (0x100 << 5);  /* ID=0x100 */
can_filter.filter_id_low     = 0;
can_filter.filter_mask_high  = (0x7FC << 5);  /* 掩码：匹配0x100-0x103 */
can_filter.filter_mask_low   = 0;
can_filter.filter_mode       = CAN_FILTER_MODE_ID_MASK;
```

#### 发送控制帧示例

```c
void can_motor_send_run(uint8_t motor_id, uint16_t rpm,
                        uint8_t dir, uint16_t accel)
{
    can_message_type tx_msg;
    tx_msg.standard_id = 0x200 + motor_id;
    tx_msg.extended_id = 0;
    tx_msg.id_type     = CAN_ID_STANDARD;
    tx_msg.frame_type  = CAN_TFT_DATA;
    tx_msg.dlc         = 8;

    tx_msg.data[0] = 0x01;                  /* CMD: RUN */
    tx_msg.data[1] = dir;                   /* 0=CW, 1=CCW */
    tx_msg.data[2] = (uint8_t)(rpm >> 8);   /* RPM高字节 */
    tx_msg.data[3] = (uint8_t)(rpm & 0xFF); /* RPM低字节 */
    tx_msg.data[4] = (uint8_t)(accel >> 8); /* 加速度高字节 */
    tx_msg.data[5] = (uint8_t)(accel & 0xFF);
    tx_msg.data[6] = 0;
    tx_msg.data[7] = 0;

    can_message_transmit(CAN1, &tx_msg);
}
```

> **注意：** 上位机AT32F435的CAN API（`can_message_type`、`can_message_transmit`）与电机板AT32F422的CAN API（`can_txbuf_type`、`can_txbuf_write`）不同，请参考AT32F435 SDK头文件。

#### 状态轮询建议

- 建议每200-500ms发送一次查询帧（0x300）获取电机状态
- 收到应答帧后解析 `monitor` 和 `fault` 字段，及时处理故障
- 发送控制命令后可立即查询确认电机状态变化
- STEP3/STEP4互斥：发送启动命令前先查询对方状态，确认已停止再启动

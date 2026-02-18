# samp 驱动逐步测试执行计划

> 适用项目：`D:\MCU_program3\samp`（AT32F435/437 + FreeRTOS）
> 编写日期：2026-02-18
> 基于：`driver_test_plan_2026-02-18.md`
> 约束：**不修改现有业务代码**，仅在 `freertos_app.c` 的 `my_task02_func` 中写入临时测试代码

---

## 1. 测试方法说明

### 1.1 核心思路

在 FreeRTOS 的 **my_task02_func**（当前为空任务）中写入临时测试代码，通过 **USART1 printf** 输出测试结果到串口终端。每个步骤完成一个最小目标，测试通过后**删除当前测试代码**，替换为下一步的测试代码。

### 1.2 为什么选择 my_task02_func

- `freertos_app.c` 中 `my_task02_func` 当前循环体为空（仅 `vTaskDelay(1)`）
- 不影响 task01（USB）及其他任务的正常运行
- 在 FreeRTOS 调度器启动后执行，所有外设已初始化完毕
- 栈大小 128 words = 512 bytes，对简单测试足够

### 1.3 测试代码模板

每个步骤的测试代码都遵循以下模式，写入 `freertos_app.c` 的 `my_task02_func` 中：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 X） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));  /* 等待系统稳定 */
  printf("\r\n===== 测试步骤 X: 标题 =====\r\n");

  // ... 具体测试代码 ...

  printf("===== 测试步骤 X 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

### 1.4 执行流程（每个步骤重复）

1. 将测试代码写入 `freertos_app.c` 的 `my_task02_func`
2. 编译：`"C:\Keil_v5\UV4\UV4.exe" -b samp.uvprojx -o build_log.txt -j0`（0错误0警告）
3. 下载到板子，打开串口终端连接 USART1（PA9, 9600, 8N1）
4. 复位板子，观察串口输出
5. 根据通过标准判定 PASS/FAIL
6. 记录结果，删除测试代码，写入下一步

### 1.5 需要添加的头文件

在 `freertos_app.c` 顶部的 `/* add user code begin private includes */` 区域，根据当前测试步骤添加对应头文件。测试完成后一并删除。

---

## 2. 测试步骤总览

| 步骤 | 测试目标 | 驱动编号 | 预计难度 |
|------|----------|----------|----------|
| S01 | printf 串口输出验证 | D08 | 简单 |
| S02 | CRC16 Modbus 已知向量 | D27 | 简单 |
| S03 | RTC 时间设置与读取 | D22 | 简单 |
| S04 | GPIO 输出 - 全关闭基线 | D01 | 简单 |
| S05 | GPIO 输出 - 逐路继电器开关 | D01 | 中等 |
| S06 | H桥电机方向控制 | D02 | 简单 |
| S07 | GPIO 输入读取 | D03 | 简单 |
| S08 | 拨码开关组合值 | D03 | 简单 |
| S09 | ADC1 DMA 扫描启动 | D04 | 简单 |
| S10 | 2.5V 基准校准 | D07 | 简单 |
| S11 | 电流通道趋势验证 | D04 | 中等 |
| S12 | NTC 温度读取 | D06 | 简单 |
| S13 | ADC2 4-20mA 读取 | D05 | 中等 |
| S14 | UART 回环 - 短帧 | D09 | 中等 |
| S15 | UART 回环 - 长帧边界 | D09 | 中等 |
| S16 | 串口屏帧发送 | D11 | 简单 |
| S17 | CAN 发送帧 | D16 | 中等 |
| S18 | QSPI Flash 读ID | D18 | 简单 |
| S19 | QSPI Flash 擦写读一致性 | D18 | 中等 |
| S20 | FAL 初始化与分区表 | D19 | 简单 |
| S21 | KVDB 初始化与读写 | D20 | 中等 |
| S22 | TSDB 初始化与追加 | D21 | 中等 |
| S23 | Wiegand 刷卡读取 | D17 | 需外设 |
| S24 | WDT 看门狗复位 | D23 | 特殊 |
| S25 | USB CDC 回显 | D24 | 黑盒 |
| S26 | USB MSC Host OTA | D26 | 黑盒 |

---

## 3. 详细测试步骤

---

### S01: printf 串口输出验证

**目标**：确认 USART1 printf 通道正常工作，这是后续所有测试的基础。

**目的**：验证 `wk_system.c` 中 `fputc` 重定向到 USART1 是否生效，串口终端能否正确接收中文和数字。

**需要添加的头文件**：无（`stdio.h` 已通过 `wk_system.h` 包含）

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S01） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S01: printf 串口输出验证 =====\r\n");
  printf("整数测试: %d\r\n", 12345);
  printf("浮点测试: %.2f\r\n", 3.14f);
  printf("十六进制: 0x%08X\r\n", 0xDEADBEEF);
  printf("字符串: Hello AT32F435\r\n");
  printf("===== S01 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：
1. 写入代码，编译下载
2. 串口终端连接 USART1（PA9, 9600, 8N1）
3. 复位板子

**通过标准**：
- 串口终端显示 `===== S01: printf 串口输出验证 =====`
- 整数显示 `12345`，浮点显示 `3.14`，十六进制显示 `0xDEADBEEF`
- 无乱码

**失败处理**：检查 USART1 波特率配置、TX 引脚连线、fputc 重定向实现。

---

### S02: CRC16 Modbus 已知向量验证

**目标**：验证硬件 CRC16 Modbus 计算结果正确。

**目的**：CRC16 是 Modbus 通信的基础，用标准测试向量 `"123456789"` 验证算法正确性，期望值 `0x4B37`。

**需要添加的头文件**：`#include "bsp_crc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S02） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S02: CRC16 Modbus 验证 =====\r\n");

  uint8_t test_data[] = "123456789";
  uint16_t crc = crc16_modbus(test_data, 9);
  printf("输入: \"123456789\" (9字节)\r\n");
  printf("计算CRC16: 0x%04X\r\n", crc);
  printf("期望CRC16: 0x4B37\r\n");
  printf("结果: %s\r\n", (crc == 0x4B37) ? "PASS" : "FAIL");

  /* 附加：Modbus帧测试 */
  uint8_t d2[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
  uint16_t crc2 = crc16_modbus(d2, 6);
  printf("Modbus帧 [01 03 00 00 00 01] CRC: 0x%04X\r\n", crc2);
  printf("期望: 0x840A\r\n");
  printf("结果: %s\r\n", (crc2 == 0x840A) ? "PASS" : "FAIL");

  printf("===== S02 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- 第一组 CRC 输出 `0x4B37`，显示 `PASS`
- 第二组 CRC 输出 `0x840A`，显示 `PASS`

**失败处理**：检查 `bsp_crc.c` 中多项式和初始值配置，确认硬件 CRC 模块时钟已使能。

---

### S03: RTC 时间设置与读取

**目标**：验证 ERTC 时间设置和读取一致性。

**目的**：确认 `rtc_set_time` 写入的时间能通过 `rtc_get_time` 正确读回，验证 BPR 魔数机制和日期时间转换。

**需要添加的头文件**：`#include "bsp_rtc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S03） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S03: RTC 时间设置与读取 =====\r\n");

  rtc_set_time(26, 2, 18, 14, 30, 0);
  vTaskDelay(pdMS_TO_TICKS(100));

  rtc_datetime_t dt;
  rtc_get_time(&dt);
  printf("设置: 2026-02-18 14:30:00\r\n");
  printf("读回: 20%02d-%02d-%02d %02d:%02d:%02d 周%d\r\n",
         dt.year, dt.month, dt.day, dt.hour, dt.min, dt.sec, dt.week);

  uint8_t pass = (dt.year == 26 && dt.month == 2 && dt.day == 18 &&
                  dt.hour == 14 && dt.min == 30 && dt.sec <= 1);
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");

  vTaskDelay(pdMS_TO_TICKS(2000));
  rtc_get_time(&dt);
  printf("2秒后: %02d:%02d:%02d\r\n", dt.hour, dt.min, dt.sec);
  printf("秒在走: %s\r\n", (dt.sec >= 2) ? "PASS" : "FAIL");

  printf("===== S03 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- 读回的年月日时分与设置值一致，秒数差 ≤ 1
- 2秒后再读，秒数有增长（≥2）

**失败处理**：检查 ERTC 时钟源（LEXT/LICK）是否起振，BPR 寄存器访问是否正确。

---

### S04: GPIO 输出 - 全关闭安全基线

**目标**：验证 `relay_all_off()` 能将所有继电器输出置为关闭状态。

**目的**：建立安全基线，确认所有输出可控且默认关闭。同时验证 `relay_get_state` 读回状态一致。

**需要添加的头文件**：`#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S04） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S04: GPIO全关闭基线 =====\r\n");

  relay_all_off();
  vTaskDelay(pdMS_TO_TICKS(100));

  const char *names[] = {
    "进水阀","瞬时阀","送留样阀","A排水","B排水",
    "A搅拌","B搅拌","出水阀A","出水阀B","锁",
    "外接泵","备用1","备用2","备用3"
  };

  uint8_t all_off = 1;
  for(int i = 0; i < RELAY_COUNT; i++) {
    uint8_t st = relay_get_state(i);
    printf("  [%02d] %-8s 状态=%d %s\r\n", i, names[i], st,
           st == 0 ? "OK" : "异常!");
    if(st != 0) all_off = 0;
  }

  motor_dir_t mdir = bottle_motor_get_dir();
  printf("  H桥电机方向=%d (期望0=STOP) %s\r\n", mdir,
         mdir == MOTOR_STOP ? "OK" : "异常!");
  if(mdir != MOTOR_STOP) all_off = 0;

  printf("结果: %s\r\n", all_off ? "PASS" : "FAIL");
  printf("===== S04 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。同时用万用表测量各输出引脚对地电压。

**通过标准**：
- 所有 14 路继电器状态 = 0，H桥电机方向 = 0
- 万用表确认各引脚为低电平（≈0V）

**失败处理**：检查 `bsp_io.c` 中 `relay_all_off` 实现，确认 GPIO 初始化配置。

---

### S05: GPIO 输出 - 逐路继电器开关验证

**目标**：逐一开关每路继电器，验证 `relay_set` 和 `relay_get_state` 配合正确。

**目的**：确认每路继电器的 GPIO 映射正确，开/关状态可控且读回一致。需人工配合万用表或观察继电器动作。

**需要添加的头文件**：`#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S05） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S05: 逐路继电器开关 =====\r\n");

  relay_all_off();
  vTaskDelay(pdMS_TO_TICKS(200));

  const char *names[] = {
    "进水阀","瞬时阀","送留样阀","A排水","B排水",
    "A搅拌","B搅拌","出水阀A","出水阀B","锁",
    "外接泵","备用1","备用2","备用3"
  };

  uint8_t pass = 1;
  for(int i = 0; i < RELAY_COUNT; i++) {
    relay_set((relay_id_t)i, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t on = relay_get_state((relay_id_t)i);

    relay_set((relay_id_t)i, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    uint8_t off = relay_get_state((relay_id_t)i);

    uint8_t ok = (on == 1 && off == 0);
    printf("  [%02d] %-8s 开=%d 关=%d %s\r\n",
           i, names[i], on, off, ok ? "PASS" : "FAIL");
    if(!ok) pass = 0;
  }

  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S05 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。听继电器咔哒声或看指示灯逐路亮灭。

**通过标准**：
- 每路继电器 `开=1 关=0` 且显示 `PASS`
- 人工确认每路有明显吸合/释放动作

**失败处理**：若某路不一致，检查该路 GPIO 引脚定义和 `relay_set` 中的 switch-case 映射。

---

### S06: H桥电机方向控制

**目标**：验证 H桥瓶排空电机三种状态（排空/复位/停止）的引脚互斥。

**目的**：确认 PE14/PE15 不会同时为高（避免 H桥短路），三种方向切换正确。

**需要添加的头文件**：`#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S06） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S06: H桥电机方向控制 =====\r\n");

  uint8_t pass = 1;

  bottle_motor_set(MOTOR_EMPTY);
  vTaskDelay(pdMS_TO_TICKS(500));
  motor_dir_t d1 = bottle_motor_get_dir();
  uint8_t pe14 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_14);
  uint8_t pe15 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_15);
  printf("  EMPTY:   dir=%d PE14=%d PE15=%d", d1, pe14, pe15);
  if(d1==MOTOR_EMPTY && pe14==1 && pe15==0) printf(" PASS\r\n");
  else { printf(" FAIL\r\n"); pass=0; }

  bottle_motor_set(MOTOR_RESTORE);
  vTaskDelay(pdMS_TO_TICKS(500));
  d1 = bottle_motor_get_dir();
  pe14 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_14);
  pe15 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_15);
  printf("  RESTORE: dir=%d PE14=%d PE15=%d", d1, pe14, pe15);
  if(d1==MOTOR_RESTORE && pe14==0 && pe15==1) printf(" PASS\r\n");
  else { printf(" FAIL\r\n"); pass=0; }

  bottle_motor_set(MOTOR_STOP);
  vTaskDelay(pdMS_TO_TICKS(500));
  d1 = bottle_motor_get_dir();
  pe14 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_14);
  pe15 = gpio_output_data_bit_read(GPIOE, GPIO_PINS_15);
  printf("  STOP:    dir=%d PE14=%d PE15=%d", d1, pe14, pe15);
  if(d1==MOTOR_STOP && pe14==0 && pe15==0) printf(" PASS\r\n");
  else { printf(" FAIL\r\n"); pass=0; }

  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S06 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口。若接了实际电机，观察转向。

**通过标准**：
- EMPTY: dir=1, PE14=1, PE15=0
- RESTORE: dir=2, PE14=0, PE15=1
- STOP: dir=0, PE14=0, PE15=0
- **关键**：任何状态下 PE14 和 PE15 不同时为 1

**失败处理**：检查 `bottle_motor_set` 中是否先关闭再开启（防止瞬间同时高）。

---

### S07: GPIO 输入读取

**目标**：验证各路数字输入能正确读取外部电平。

**目的**：确认 `input_read()` 返回值与实际引脚电平一致。需人工用跳线将输入引脚拉高/拉低。

**需要添加的头文件**：`#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S07） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S07: GPIO输入读取 =====\r\n");
  printf("请在10秒内改变输入电平，每秒刷新:\r\n");

  const char *names[] = {
    "采样液位","送样液位","留样液位","采样触发","送样触发",
    "留样触发","锁状态","瓶原点","瓶到位","备用输入"
  };

  for(int t = 0; t < 10; t++) {
    printf("  [%ds] ", t);
    for(int i = 0; i < INPUT_COUNT; i++)
      printf("%s=%d ", names[i], input_read((input_id_t)i));
    printf("\r\n");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  printf("===== S07 完成（人工判定） =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。10秒内用跳线改变某路输入电平。

**通过标准**：
- 至少验证 2~3 路：拉高读到 1，拉低读到 0
- 输出值随外部电平实时变化

**失败处理**：检查 GPIO 是否配置为输入模式，确认上拉/下拉电阻配置。

---

### S08: 拨码开关组合值

**目标**：验证 `input_get_dip_switch()` 能正确读取 PE11/PE12/PE13 三位拨码组合。

**目的**：拨码开关用于设备地址/模式选择，需确认 3 位组合值（0~7）与物理拨码一致。

**需要添加的头文件**：`#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S08） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S08: 拨码开关组合值 =====\r\n");
  printf("请在15秒内拨动开关(PE11=bit0,PE12=bit1,PE13=bit2):\r\n");

  for(int t = 0; t < 15; t++) {
    uint8_t val = input_get_dip_switch();
    printf("  [%2ds] 拨码值=%d (二进制:%d%d%d)\r\n",
           t, val, (val>>2)&1, (val>>1)&1, val&1);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  printf("===== S08 完成（人工判定） =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。15秒内拨动开关到不同组合。

**通过标准**：
- 全关(000)=0，全开(111)=7
- 至少验证 3 种不同组合，值与物理位置一致

**失败处理**：检查 PE11/PE12/PE13 的 bit 顺序映射是否与硬件一致。

---

### S09: ADC1 DMA 扫描启动

**目标**：验证 ADC1 DMA 连续扫描正在运行，`adc1_dma_buf` 数据在动态更新。

**目的**：ADC1 负责 12 通道采集（电流/温度/基准），需确认 DMA 搬运正常、数据非全零且有噪声抖动。

**需要添加的头文件**：`#include "bsp_adc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S09） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S09: ADC1 DMA扫描启动 =====\r\n");

  bsp_adc1_dma_start();
  vTaskDelay(pdMS_TO_TICKS(500));

  const char *ch[] = {"VREF","瓶电机","出水A","搅拌B","搅拌A","排水B",
                       "排水A","进水阀","瞬时阀","送样阀","温度1","温度2"};
  uint16_t prev[ADC1_CHANNEL_COUNT] = {0};
  uint8_t changed = 0;

  for(int t = 0; t < 3; t++) {
    printf("  第%d次:\r\n", t+1);
    for(int i = 0; i < ADC1_CHANNEL_COUNT; i++) {
      uint16_t raw = adc1_get_raw((adc1_ch_index_t)i);
      printf("    [%02d] %-6s raw=%4d\r\n", i, ch[i], raw);
      if(t > 0 && raw != prev[i]) changed = 1;
      prev[i] = raw;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  printf("数据非全零: %s\r\n", (prev[0] > 0) ? "PASS" : "FAIL");
  printf("数据有变化: %s\r\n", changed ? "PASS" : "注意(可能正常)");
  printf("===== S09 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- VREF 通道 raw 值在 2800~3200 范围（约 2.5V）
- 数据非全零
- 多次采样间至少部分通道有微小变化

**失败处理**：检查 DMA2_CHANNEL1 配置、ADC1 通道扫描顺序。

---

### S10: 2.5V 基准校准

**目标**：验证 `adc_cal_run()` 校准流程，校准后 VREF 通道电压接近 2500mV。

**目的**：2.5V 基准是所有 ADC 计算的基础，校准系数直接影响后续测量精度。

**需要添加的头文件**：`#include "bsp_adc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S10） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S10: 2.5V基准校准 =====\r\n");

  bsp_adc1_dma_start();
  vTaskDelay(pdMS_TO_TICKS(1000));

  printf("校准前: valid=%d factor=%.4f\r\n",
         adc_cal.valid, adc_cal.vref_factor);
  float mv_before = adc1_get_voltage_mv(ADC_CH_VREF);
  printf("  VREF voltage=%.1fmV\r\n", mv_before);

  adc_cal_run();

  printf("校准后: valid=%d factor=%.4f\r\n",
         adc_cal.valid, adc_cal.vref_factor);
  float mv_after = adc1_get_voltage_mv(ADC_CH_VREF);
  printf("  VREF voltage=%.1fmV\r\n", mv_after);

  uint8_t pass = (adc_cal.valid == 1) &&
                 (mv_after > 2400.0f && mv_after < 2600.0f);
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S10 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- `adc_cal.valid` = 1
- 校准后 VREF 电压在 2400~2600mV 范围

**失败处理**：检查 2.5V 基准电路是否正常供电，确认 PA0 连接正确。

---

### S11: 电流通道趋势验证

**目标**：开关一路继电器，观察对应电流通道 ADC 值是否有明显变化。

**目的**：验证电流采样通道与继电器对应关系正确。注意：电流绝对值系数当前为假设值，本步骤只验趋势。

**需要添加的头文件**：`#include "bsp_adc.h"` 和 `#include "bsp_io.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S11） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S11: 电流通道趋势 =====\r\n");

  bsp_adc1_dma_start();
  adc_cal_run();
  vTaskDelay(pdMS_TO_TICKS(1000));

  relay_all_off();
  vTaskDelay(pdMS_TO_TICKS(500));
  float off_ma = adc1_get_current_ma(ADC_CH_INLET_VALVE);
  uint8_t off_act = adc1_is_active(ADC_CH_INLET_VALVE);
  printf("进水阀 关: %.2fmA active=%d\r\n", off_ma, off_act);

  relay_set(RELAY_INLET_VALVE, 1);
  vTaskDelay(pdMS_TO_TICKS(1000));
  float on_ma = adc1_get_current_ma(ADC_CH_INLET_VALVE);
  uint8_t on_act = adc1_is_active(ADC_CH_INLET_VALVE);
  printf("进水阀 开: %.2fmA active=%d\r\n", on_ma, on_act);

  relay_set(RELAY_INLET_VALVE, 0);

  uint8_t pass = (on_ma > off_ma + 1.0f);
  printf("趋势(开>关): %s\r\n", pass ? "PASS" : "FAIL(需接负载)");
  printf("===== S11 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。**前提**：进水阀需接实际负载。

**通过标准**：
- 关闭时电流接近 0mA，`active=0`
- 开启时电流明显上升，`active=1`
- 若未接负载，记录为"需接负载后复测"

**失败处理**：确认负载已接、采样电阻电路正常、ADC 通道映射正确。

---

### S12: NTC 温度读取

**目标**：验证两路 NTC 温度传感器读数合理。

**目的**：确认 `adc_get_temp()` 返回的温度值在室温范围内。

**需要添加的头文件**：`#include "bsp_adc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S12） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S12: NTC温度读取 =====\r\n");

  bsp_adc1_dma_start();
  adc_cal_run();
  vTaskDelay(pdMS_TO_TICKS(1000));

  for(int t = 0; t < 5; t++) {
    float t1 = adc_get_temp(0);
    float t2 = adc_get_temp(1);
    printf("  [%d] 温度1=%.1f°C 温度2=%.1f°C\r\n", t, t1, t2);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  float temp = adc_get_temp(0);
  uint8_t pass = (temp > -10.0f && temp < 60.0f);
  printf("温度1在合理范围(-10~60): %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S12 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。可用温度计对比。

**通过标准**：
- 温度值在 -10°C ~ 60°C 范围
- 多次读数稳定（波动 < 2°C）

**失败处理**：检查 NTC 探头是否连接、查表算法参数是否匹配实际 NTC 型号。

---

### S13: ADC2 4-20mA 读取

**目标**：验证 ADC2 两路 4-20mA 输入能正确读取。

**目的**：确认 `adc2_get_420ma_current()` 返回合理值。若无电流源，仅验证函数可调用且不崩溃。

**需要添加的头文件**：`#include "bsp_adc.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S13） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S13: ADC2 4-20mA读取 =====\r\n");

  for(int t = 0; t < 5; t++) {
    uint16_t raw1 = adc2_read_raw(ADC_420MA_CH1);
    uint16_t raw2 = adc2_read_raw(ADC_420MA_CH2);
    float ma1 = adc2_get_420ma_current(ADC_420MA_CH1);
    float ma2 = adc2_get_420ma_current(ADC_420MA_CH2);
    printf("  [%d] CH1:raw=%4d %.2fmA CH2:raw=%4d %.2fmA\r\n",
           t, raw1, ma1, raw2, ma2);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  printf("===== S13 完成（人工判定） =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位。有电流源时分别设定 4/12/20mA 记录读数。

**通过标准**：
- 无电流源时读数接近 0mA（< 1mA）
- 有电流源时误差 < ±5%
- 函数不崩溃，raw 在 0~4095

**失败处理**：检查 ADC2 初始化、PC2/PA1 引脚、INA180A2 放大电路。

---

### S14: UART 回环 - 短帧

**目标**：验证 UART DMA+IDLE 收发框架，通过 TX-RX 短接回环测试。

**目的**：确认 `bsp_uart_send` 发送的数据能通过 DMA+IDLE 正确接收。这是所有串口通信的基础。

**前提**：将 UART7 的 TX 和 RX 引脚短接。

**需要添加的头文件**：`#include "bsp_uart.h"` 和 `#include <string.h>`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S14） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S14: UART回环短帧 =====\r\n");
  printf("请确保 UART7 TX-RX 已短接\r\n");

  uint8_t tx[] = "HELLO_UART7";
  uint8_t rx[64] = {0};

  bsp_uart_send(UART_PORT_SPARE485, tx, sizeof(tx)-1);
  vTaskDelay(pdMS_TO_TICKS(200));

  uint16_t rx_len = bsp_uart_get_rxdata(UART_PORT_SPARE485, rx, 64);
  printf("发送: \"%s\" (%d字节)\r\n", tx, (int)(sizeof(tx)-1));
  printf("接收: %d字节\r\n", rx_len);
  if(rx_len > 0) { rx[rx_len]='\0'; printf("内容: \"%s\"\r\n", rx); }

  uint8_t pass = (rx_len==sizeof(tx)-1) && (memcmp(tx,rx,rx_len)==0);
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S14 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：UART7 TX-RX 短接，写入代码，编译下载，复位

**通过标准**：
- 接收长度 = 11，内容 = `"HELLO_UART7"`

**失败处理**：检查 UART7 DMA 通道配置、IDLE 中断使能、TX/RX 是否短接。

---

### S15: UART 回环 - 长帧边界

**目标**：验证 DMA 缓冲区边界（256字节）附近的收发行为。

**目的**：确认 200/255 字节能完整收发，260 字节的截断行为可控。

**需要添加的头文件**：`#include "bsp_uart.h"` 和 `#include <string.h>`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S15） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S15: UART回环长帧边界 =====\r\n");
  printf("请确保 UART7 TX-RX 已短接\r\n");

  uint8_t tx[280], rx[280];
  uint16_t lens[] = {200, 255, 260};

  for(int t = 0; t < 3; t++) {
    uint16_t len = lens[t];
    for(int i = 0; i < len; i++) tx[i] = (uint8_t)(i & 0xFF);
    bsp_uart_get_rxdata(UART_PORT_SPARE485, rx, 280);
    memset(rx, 0, 280);

    bsp_uart_send(UART_PORT_SPARE485, tx, len);
    vTaskDelay(pdMS_TO_TICKS(500));

    uint16_t rx_len = bsp_uart_get_rxdata(UART_PORT_SPARE485, rx, 280);
    uint8_t match = (rx_len <= len) ? (memcmp(tx, rx, rx_len) == 0) : 0;
    printf("  发送%3d -> 接收%3d 内容%s %s\r\n",
           len, rx_len, match?"一致":"不一致",
           (rx_len==len && match) ? "PASS" : "注意");
  }

  printf("===== S15 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：UART7 TX-RX 短接，写入代码，编译下载，复位

**通过标准**：
- 200 字节：接收 200，内容一致 PASS
- 255 字节：接收 255，内容一致 PASS
- 260 字节：记录实际接收长度（可能截断为 256）

**失败处理**：若 200/255 也截断，检查 `UART_DMA_BUF_SIZE` 定义。

---

### S16: 串口屏帧发送

**目标**：验证串口屏协议帧格式正确（5A A5 帧头）。

**目的**：通过逻辑分析仪抓取 UART4 TX 输出，确认帧符合迪文屏协议。

**需要添加的头文件**：`#include "bsp_screen.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S16） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S16: 串口屏帧发送 =====\r\n");

  screen_init();
  vTaskDelay(pdMS_TO_TICKS(200));

  printf("发送 screen_write_u16(0x1000, 0x1234)...\r\n");
  screen_write_u16(0x1000, 0x1234);
  vTaskDelay(pdMS_TO_TICKS(200));

  printf("发送 screen_switch_page(1)...\r\n");
  screen_switch_page(1);

  printf("请用逻辑分析仪抓取UART4 TX确认帧格式\r\n");
  printf("期望: 5A A5 开头, CMD=0x82\r\n");
  printf("===== S16 完成（人工判定） =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：逻辑分析仪夹在 UART4 TX 上，写入代码，编译下载，复位

**通过标准**：
- 抓到帧头 `5A A5`
- `screen_write_u16`: CMD=`0x82`，地址 `0x1000`，数据 `0x1234`

**失败处理**：检查 UART4 波特率、`screen_write_var` 帧组装逻辑。

---

### S17: CAN 发送帧

**目标**：验证 CAN1 能成功发送电机控制帧到总线。

**目的**：确认 `can_motor_set_speed` 能发送 ID=0x100 的标准帧，CAN 物理层无错误。

**需要添加的头文件**：`#include "bsp_can_motor.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S17） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S17: CAN发送帧 =====\r\n");

  printf("发送 set_speed(0, 3000, CW)...\r\n");
  can_motor_set_speed(0, 3000, MOTOR_DIR_CW);
  vTaskDelay(pdMS_TO_TICKS(500));

  printf("发送 stop(0)...\r\n");
  can_motor_stop(0);
  vTaskDelay(pdMS_TO_TICKS(500));

  motor_status_t st = can_motor_get_status(0);
  printf("电机0状态: speed=%d dir=%d running=%d err=%d\r\n",
         st.speed, st.direction, st.running, st.error);

  printf("请用USB-CAN抓包确认ID=0x100帧\r\n");
  printf("===== S17 完成（人工判定） =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：USB-CAN 适配器连接 CAN1（确保 120Ω 终端电阻），写入代码，编译下载，复位

**通过标准**：
- 抓到 ID=0x100 的标准数据帧
- CAN 总线无错误帧风暴

**失败处理**：检查 CAN 波特率、终端电阻、收发器芯片供电。

---

### S18: QSPI Flash 读ID

**目标**：验证 QSPI2 外部 Flash 通信正常，能读到正确芯片 ID。

**目的**：确认 QSPI 总线初始化正确，这是 FAL/FlashDB 的物理层基础。

**需要添加的头文件**：`#include "bsp_qspi_flash.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S18） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S18: QSPI Flash读ID =====\r\n");

  uint8_t ok = qspi_flash_init();
  printf("qspi_flash_init: %s\r\n", ok ? "OK" : "FAIL");

  uint16_t id = qspi_flash_read_id();
  printf("Flash ID: 0x%04X\r\n", id);

  const char *name = "未知";
  if(id==0xBA16) name="ZD25Q64B";
  else if(id==0xBA17) name="ZD25Q128";
  else if(id==0xEF16) name="W25Q64";
  else if(id==0xEF17) name="W25Q128";
  printf("芯片: %s\r\n", name);

  uint8_t pass = (id==0xBA16||id==0xBA17||id==0xEF16||id==0xEF17);
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S18 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- Flash ID 为已知型号之一

**失败处理**：检查 QSPI2 引脚配置、Flash 芯片焊接、供电。

---

### S19: QSPI Flash 擦写读一致性

**目标**：验证 QSPI Flash 扇区擦除、写入、读回数据完全一致。

**目的**：确认擦写读三个操作配合正确，包括跨 256B 页边界的写入。

**需要添加的头文件**：`#include "bsp_qspi_flash.h"` 和 `#include <string.h>`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S19） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S19: QSPI Flash擦写读 =====\r\n");

  qspi_flash_init();
  uint32_t addr = QFLASH_CHIP_SIZE - QFLASH_SECTOR_SIZE; /* 末尾扇区 */
  uint8_t wr[512], rd[512];
  for(int i = 0; i < 512; i++) wr[i] = (uint8_t)(i & 0xFF);

  printf("擦除扇区 0x%08X...\r\n", (unsigned int)addr);
  qspi_flash_erase_sector(addr);
  vTaskDelay(pdMS_TO_TICKS(200));

  printf("写入512字节(跨2页)...\r\n");
  qspi_flash_write(addr, wr, 512);
  vTaskDelay(pdMS_TO_TICKS(100));

  memset(rd, 0, 512);
  qspi_flash_read(addr, rd, 512);

  uint16_t err = 0;
  for(int i = 0; i < 512; i++) {
    if(wr[i] != rd[i]) {
      if(err < 5) printf("  [%d] 写=0x%02X 读=0x%02X\r\n", i, wr[i], rd[i]);
      err++;
    }
  }
  printf("不一致: %d/512\r\n", err);
  printf("结果: %s\r\n", (err == 0) ? "PASS" : "FAIL");
  printf("===== S19 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- 512 字节全部一致，不一致数 = 0

**失败处理**：检查跨页写入逻辑（256B 页边界处理）、擦除是否完成。

---

### S20: FAL 初始化与分区表

**目标**：验证 FAL 层初始化成功，分区表与 `fal_cfg.h` 一致。

**目的**：FAL 是 FlashDB 的底层抽象，需确认片内 Flash 和 QSPI Flash 两个设备都注册成功。

**需要添加的头文件**：`#include "fal_cfg.h"` 和 `#include "fal.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S20） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S20: FAL初始化与分区表 =====\r\n");

  int ret = fal_init();
  printf("fal_init: %d\r\n", ret);

  const struct fal_partition *p1 = fal_partition_find("fdb_kvdb");
  const struct fal_partition *p2 = fal_partition_find("fdb_tsdb");
  uint8_t pass = 1;

  if(p1) {
    printf("fdb_kvdb: flash=%s off=0x%X len=0x%X\r\n",
           p1->flash_name, (unsigned)p1->offset, (unsigned)p1->len);
    if(p1->len != 512*1024) pass = 0;
  } else { printf("fdb_kvdb: 未找到!\r\n"); pass = 0; }

  if(p2) {
    printf("fdb_tsdb: flash=%s off=0x%X len=0x%X\r\n",
           p2->flash_name, (unsigned)p2->offset, (unsigned)p2->len);
    if(p2->len != 8*1024*1024) pass = 0;
  } else { printf("fdb_tsdb: 未找到!\r\n"); pass = 0; }

  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S20 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- `fdb_kvdb`: flash=AT32_onchip, len=512KB
- `fdb_tsdb`: flash=qspi_nor, len=8MB

**失败处理**：检查 `fal_cfg.h` 分区表、Flash 设备注册文件。

---

### S21: KVDB 初始化与读写

**目标**：验证 FlashDB KVDB 能初始化、写入、读回数据一致。

**目的**：确认 `settings_init_load` 和 `cfg_save/load_sample` 在 FreeRTOS 运行态下工作正常。

**需要添加的头文件**：`#include "app_flashdb.h"` 和 `#include <string.h>`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S21） ===== */
  vTaskDelay(pdMS_TO_TICKS(3000));
  printf("\r\n===== S21: KVDB初始化与读写 =====\r\n");

  settings_init_load();
  vTaskDelay(pdMS_TO_TICKS(500));

  /* 写入测试数据 */
  uint8_t wr[32], rd[32];
  memset(wr, 0xA5, 32);
  wr[0] = 0x01; wr[31] = 0xFE;

  uint8_t sv = cfg_save_sample(wr);
  printf("cfg_save_sample: %s\r\n", sv ? "OK" : "FAIL");

  memset(rd, 0, 32);
  uint8_t ld = cfg_load_sample(rd);
  printf("cfg_load_sample: %s\r\n", ld ? "OK" : "FAIL");

  uint8_t match = (memcmp(wr, rd, 32) == 0);
  printf("数据比对: %s\r\n", match ? "一致" : "不一致");
  printf("  写入前8字节: ");
  for(int i=0;i<8;i++) printf("%02X ", wr[i]);
  printf("\r\n  读回前8字节: ");
  for(int i=0;i<8;i++) printf("%02X ", rd[i]);
  printf("\r\n");

  uint8_t pass = sv && ld && match;
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S21 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- `cfg_save_sample` 和 `cfg_load_sample` 均返回成功
- 读回 32 字节与写入完全一致

**失败处理**：检查 KVDB 分区是否已擦除初始化、FAL 层是否正常。

---

### S22: TSDB 初始化与追加

**目标**：验证 FlashDB TSDB 能初始化、追加事件、查询就绪状态。

**目的**：确认时序数据库在 FreeRTOS 运行态下可正常追加记录。

**需要添加的头文件**：`#include "app_flashdb.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S22） ===== */
  vTaskDelay(pdMS_TO_TICKS(3000));
  printf("\r\n===== S22: TSDB初始化与追加 =====\r\n");

  settings_init_load();
  vTaskDelay(pdMS_TO_TICKS(500));
  fdb_start_tasks();
  vTaskDelay(pdMS_TO_TICKS(1000));

  uint8_t ready = tsdb_is_ready();
  printf("tsdb_is_ready: %d %s\r\n", ready, ready ? "OK" : "FAIL");

  /* 追加测试事件 */
  uint8_t body[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t r1 = tsdb_event_append(0x0001, body, 4);
  printf("追加事件1: %s\r\n", r1 ? "OK" : "FAIL");

  uint8_t r2 = tsdb_event_append(0x0002, body, 4);
  printf("追加事件2: %s\r\n", r2 ? "OK" : "FAIL");

  uint8_t pass = ready && r1 && r2;
  printf("结果: %s\r\n", pass ? "PASS" : "FAIL");
  printf("===== S22 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位观察串口

**通过标准**：
- `tsdb_is_ready` 返回 1
- 两次追加均返回成功

**失败处理**：检查 TSDB 分区（QSPI Flash）、`fdb_start_tasks` 内部初始化日志。

---

### S23: Wiegand 刷卡读取

**目标**：验证 Wiegand 读头能正确读取卡号。

**目的**：确认 EXINT4/7 中断驱动的 Wiegand 26/34 位解析正确，`wiegand_get_card_id` 能返回稳定卡号。

**前提**：需要 Wiegand 读头连接（D0→PD4，D1→PD7）和实体卡。

**需要添加的头文件**：`#include "bsp_wiegand.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S23） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S23: Wiegand刷卡读取 =====\r\n");
  printf("请在30秒内刷卡...\r\n");

  uint32_t card_id = 0;
  for(int t = 0; t < 30; t++) {
    if(wiegand_get_card_id(&card_id)) {
      printf("  读到卡号: 0x%08X (%u)\r\n",
             (unsigned)card_id, (unsigned)card_id);
      printf("结果: PASS\r\n");
      break;
    }
    if(t % 5 == 0) printf("  [%ds] 等待刷卡...\r\n", t);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  if(card_id == 0) printf("结果: FAIL(超时未读到卡)\r\n");

  printf("===== S23 完成 =====\r\n");
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：写入代码，编译下载，复位后30秒内刷卡

**通过标准**：
- 刷卡后读到非零卡号
- 多次刷同一张卡，卡号一致

**失败处理**：检查 EXINT4/7 中断配置、D0/D1 引脚连线、`WIEGAND_TIMEOUT_MS` 是否合理。

---

### S24: WDT 看门狗复位

**目标**：验证看门狗使能后不喂狗会触发系统复位，且复位后能识别复位来源。

**目的**：确认 `bsp_wdt_enable` / `bsp_wdt_feed` / `bsp_wdt_is_reset` 三个接口工作正常。

**特殊说明**：本步骤会导致系统复位，属于破坏性测试。测试代码分两阶段：第一次运行启用 WDT 并故意不喂狗；复位后第二次运行检查复位标志。

**需要添加的头文件**：`#include "bsp_wdt.h"`

**测试代码**：

```c
void my_task02_func(void *pvParameters)
{
  /* ===== 临时测试代码 START（步骤 S24） ===== */
  vTaskDelay(pdMS_TO_TICKS(2000));
  printf("\r\n===== S24: WDT看门狗复位 =====\r\n");

  uint8_t was_wdt = bsp_wdt_is_reset();
  printf("WDT复位标志: %d\r\n", was_wdt);

  if(was_wdt) {
    printf("检测到WDT复位! 结果: PASS\r\n");
    printf("===== S24 完成 =====\r\n");
  } else {
    printf("首次运行: 启用WDT后故意不喂狗...\r\n");
    printf("系统将在几秒后复位\r\n");
    bsp_wdt_enable();
    /* 故意不喂狗，等待WDT超时复位 */
    while(1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
  }
  /* ===== 临时测试代码 END ===== */

  while(1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}
```

**执行方式**：
1. 写入代码，编译下载，复位
2. 第一次运行：串口显示"首次运行"后系统自动复位
3. 复位后自动进入第二次运行：检查 WDT 复位标志

**通过标准**：
- 第一次运行后系统自动复位（几秒内）
- 复位后串口显示 `WDT复位标志: 1` 和 `PASS`

**失败处理**：若不复位，检查 WDT 时钟配置和超时时间；若复位但标志为 0，检查 `bsp_wdt_is_reset` 读取的寄存器位。

---

### S25: USB CDC 回显

**目标**：验证 USB OTGFS2 CDC 虚拟串口回显功能正常。

**目的**：确认 `usb_app.c` 中的 CDC echo 逻辑工作正常，PC 发送数据能原样回显。

**特殊说明**：这是黑盒测试，**不需要写临时测试代码**。当前固件 `usb_app.c` 已实现 CDC echo，只需用 PC 端脚本验证。

**需要添加的头文件**：无

**测试方式（PC 端 Python 脚本）**：

```python
import os, serial

PORT = "COM20"   # 改成实际枚举的CDC虚拟串口
BAUD = 115200

payload = os.urandom(256)
with serial.Serial(PORT, BAUD, timeout=2) as ser:
    ser.reset_input_buffer()
    ser.write(payload)
    echo = ser.read(len(payload))
    if echo == payload:
        print("S25: PASS - CDC回显256字节一致")
    else:
        print(f"S25: FAIL - 发送{len(payload)} 接收{len(echo)}")
```

**执行方式**：
1. OTGFS2 连接 PC，确认设备枚举出 CDC 虚拟串口（设备管理器可见 COM 口）
2. 运行上述 Python 脚本

**通过标准**：
- 256 字节随机数据回显完全一致
- 脚本输出 `PASS`

**失败处理**：检查 USB 枚举是否成功、CDC 端口号是否正确、`usb_app.c` 中 echo 逻辑。

---

### S26: USB MSC Host OTA

**目标**：验证 USB OTGFS1 Host 模式能挂载 U盘并完成固件文件 CRC 校验。

**目的**：确认 `usbh_user.c` 中的 OTA 校验流程正常：挂载 → 读文件 → 校验魔数/大小/CRC32。

**特殊说明**：这是黑盒测试，**不需要写临时测试代码**。当前固件已实现 OTA 校验逻辑（写 Flash 为 TODO），通过串口日志观察结果。

**需要添加的头文件**：无

**测试准备（PC 端生成测试固件文件）**：

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
print(f"firmware.bin: {len(data)}字节 CRC32=0x{crc32:08X}")
```

将生成的 `firmware.bin` 拷贝到 FAT32 格式 U盘根目录。

**执行方式**：
1. 串口终端连接 USART1（观察 `[USB]`/`[OTA]` 日志）
2. 将 U盘插入 OTGFS1 接口
3. 观察串口输出

**通过标准**：
- 串口出现 U盘挂载成功日志
- 正确文件：出现 `[OTA] CRC校验通过`
- 错误文件（可选测试）：能打印明确错误（魔数/大小/CRC 不匹配）

**失败处理**：检查 OTGFS1 硬件连接、U盘格式（必须 FAT32）、`firmware.bin` 文件格式。

---

## 4. 测试记录模板

每个步骤完成后，建议按以下格式记录结果：

| 步骤 | 日期 | 固件版本(commit) | 结果 | 关键数据 | 备注 |
|------|------|-------------------|------|----------|------|
| S01 | 2026-02-xx | xxxxxxx | PASS/FAIL | 串口输出截图 | |
| S02 | | | | CRC=0x4B37 | |
| ... | | | | | |

---

## 5. 注意事项

1. **严格按顺序执行**：S01 是所有后续步骤的基础（printf 通道），必须先通过。
2. **每步只改 `freertos_app.c`**：测试代码写入 `my_task02_func`，头文件加在文件顶部的 user code 区域。
3. **通过后立即清理**：使用 `git restore samp/project/src/freertos_app.c` 恢复，再写入下一步。
4. **编译必须 0 错误 0 警告**：每步写入代码后先编译确认再下载。
5. **S25/S26 为黑盒测试**：不需要修改固件代码，直接用外部工具验证。
6. **S24 会导致复位**：测试前确保其他重要数据已保存。

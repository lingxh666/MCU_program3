# ADC 6通道 4-20mA 采集方案 — 实施计划

## 一、现有框架分析

### 已完成（WorkBench生成）
| 模块 | 配置 | 状态 |
|------|------|------|
| 系统时钟 | HICK→PLL×30 = 120MHz | ✅ |
| ADC1 | 7通道序列(CH0/1/3/4/5/6/9)，软件触发，**repeat_mode=FALSE** | ⚠️ 需改为TRUE |
| DMA1_CH1 | 外设→内存，半字，循环模式，**buffer_size=0** | ⚠️ 需配置 |
| USART1 | PA9/PA10, 9600baud, TX+RX | ✅ 用于数据帧发送 |
| USART2 | PA2, 115200baud, printf重定向 | ✅ 调试用 |
| TMR3 | 1ms时基（wk_delay_ms） | ✅ |
| TMR6 | 分频119，周期999 → 1ms中断 | ✅ 用于ADC采样节拍 |
| TMR16 | 分频11999，周期9999 → 1s中断 | ✅ 用于触发串口发送 |
| GPIO | PA0/1/3/4/5/6(模拟), PB1(模拟) | ✅ |
| NVIC | TMR3/6/16中断已使能 | ⚠️ 需加DMA1_CH1中断 |

### 通道映射
| 序号 | ADC通道 | GPIO | 用途 |
|------|---------|------|------|
| 1 | CH0 | PA0 | 4-20mA 通道1 |
| 2 | CH1 | PA1 | 4-20mA 通道2 |
| 3 | CH3 | PA3 | 4-20mA 通道3 |
| 4 | CH4 | PA4 | 4-20mA 通道4 |
| 5 | CH5 | PA5 | 4-20mA 通道5 |
| 6 | CH6 | PA6 | 4-20mA 通道6 |
| 7 | CH9 | PB1 | 2.5V校准基准 |

---

## 二、整体架构设计

### 数据流
```
ADC1(7ch连续转换) → DMA1_CH1(循环模式) → adc_dma_buf[7×512]
                                              ↓
                              DMA半传输中断 → 前半区(256组)就绪 → 滤波处理
                              DMA全传输中断 → 后半区(256组)就绪 → 滤波处理
                                              ↓
                                    filtered_adc[7] (滤波后ADC值)
                                              ↓
                              TMR16(1s中断) → 设置发送标志
                                              ↓
                              主循环 → ADC值→电压→电流→组帧→USART1发送
```

### 双缓冲方案（DMA半传输/全传输中断）
```
DMA循环缓冲区: adc_dma_buf[3584] (7通道 × 512组)
┌──────────────────────┬──────────────────────┐
│   Buffer A (前半区)    │   Buffer B (后半区)    │
│  7ch × 256组 = 1792   │  7ch × 256组 = 1792   │
│  DMA半传输中断时就绪    │  DMA全传输中断时就绪    │
└──────────────────────┴──────────────────────┘
处理Buffer A时，DMA正在填充Buffer B，反之亦然 → 无竞争条件
```

### 内存占用估算
- DMA缓冲区: 7 × 512 × 2 = 7168 字节
- 滤波结果: 7 × 2 = 14 字节
- 发送缓冲区: 16 字节
- 其他变量: ~50 字节
- **总计: ~7.3KB / 16KB RAM (45.6%)**

---

## 三、滤波算法

极值剔除法，每通道256个采样点：
```c
/* 数据在DMA缓冲区中交织存放: CH0,CH1,CH3,CH4,CH5,CH6,CH9,CH0,CH1,... */
/* 提取第ch个通道的第i个采样: buf[i * 7 + ch] */
for each channel (ch = 0..6):
    sum = 0, max = 0, min = 4095
    for i = 0..255:
        v = buf[i * 7 + ch]
        sum += v
        if v > max: max = v
        if v < min: min = v
    filtered[ch] = (sum - max - min) / 254
```

---

## 四、电流计算与校准

### CH9校准原理
- CH9外部接2.5V基准电压
- 理论ADC值 = 2.5 / 3.3 × 4095 ≈ 3103
- 实际Vref = 2.5 × 4095 / filtered[6]  (CH9是第7个通道，索引6)

### 硬件电路参数
- 采样电阻: R = 3Ω
- 检测芯片: INA180A2IDBVR，增益 = 50倍
- 4-20mA × 3Ω = 12~60mV（电阻两端）
- 经50倍放大后: 0.6V ~ 3.0V（进入ADC）

### 电流计算公式
```
V_adc = filtered[ch] × Vref_actual / 4095
V_resistor = V_adc / 50          (INA180放大倍数)
I = V_resistor / 3               (采样电阻)
I_mA = I × 1000

合并简化（整数运算，避免浮点）:
send_value = filtered[ch] × 50000 / (filtered_ch9 × 3)

验证: 4mA → ADC≈745, CH9≈3103 → 745×50000/(3103×3) = 4002 → 4.002mA ✓
验证: 20mA → ADC≈3723, CH9≈3103 → 3723×50000/(3103×3) = 19997 → 19.997mA ✓
注意: 4095×50000=204,750,000 < 2^32，uint32_t不会溢出 ✓
```

---

## 五、串口帧格式

通过USART1(9600baud)发送，每秒1帧，共16字节：
```
字节序号:  [0]  [1]  [2]  [3]  [4]  [5]  [6]  [7]  [8]  [9] [10] [11] [12] [13] [14] [15]
内容:      0x6B 0xB6 CH0H CH0L CH1H CH1L CH3H CH3L CH4H CH4L CH5H CH5L CH6H CH6L 0x8C 0xC8
           帧头        通道1    通道2    通道3    通道4    通道5    通道6    帧尾
```
- 帧头: 0x6B 0xB6
- 数据: 6通道 × 2字节(大端序) = 12字节，值为电流mA×1000
- 帧尾: 0x8C 0xC8
- 示例: 19.025mA → 19025 → 0x4A 0x51

---

## 六、实施任务清单

### 任务1: 修改 `at32f421_wk_config.h`
- 定义DMA缓冲区大小宏: `ADC_CHANNEL_COUNT 7`, `ADC_SAMPLE_COUNT 256`, `ADC_DMA_BUF_SIZE (7*512)`
- 更新 `DMA1_CHANNEL1_BUFFER_SIZE` 为 `ADC_DMA_BUF_SIZE`
- 定义INA180增益宏 `INA180_GAIN 50`
- 定义采样电阻宏 `SAMPLE_RESISTOR_OHM 3`
- 定义校准基准电压宏 `VREF_CALIBRATION_MV 2500`
- 定义帧头帧尾宏

### 任务2: 修改 `at32f421_wk_config.c`
- `wk_adc1_init()`: 将 `repeat_mode` 改为 `TRUE`
- `wk_dma1_channel1_init()`: 使能DMA半传输中断(`DMA_HDT_INT`)和全传输中断(`DMA_FDT_INT`)
- `wk_nvic_config()`: 添加 `DMA1_Channel1_IRQn` 中断使能

### 任务3: 修改 `main.c`
- 声明全局变量:
  - `uint16_t adc_dma_buf[ADC_DMA_BUF_SIZE]` — DMA目标缓冲区
  - `volatile uint16_t filtered_adc[7]` — 滤波后ADC值
  - `volatile uint8_t buf_ready_flag` — 缓冲区就绪标志(1=前半区, 2=后半区)
  - `volatile uint8_t send_flag` — 1s发送标志
- 更新DMA配置调用，传入正确的缓冲区地址和大小
- 启动ADC软件触发: `adc_ordinary_software_trigger_enable(ADC1, TRUE)`
- 主循环实现:
  - 检查 `buf_ready_flag` → 调用滤波处理函数
  - 检查 `send_flag` → 计算电流值、组帧、USART1发送
  - 喂狗(如启用WDT)
- 实现 `adc_filter_process(uint16_t *buf_start)` 滤波函数
- 实现 `usart1_send_frame()` 组帧发送函数
- 实现 `usart1_send_byte()` 单字节发送函数

### 任务4: 修改 `at32f421_int.c`
- 添加 `DMA1_Channel1_IRQHandler()`:
  - 半传输中断: 清标志，设 `buf_ready_flag = 1`
  - 全传输中断: 清标志，设 `buf_ready_flag = 2`
- 修改 `TMR16_GLOBAL_IRQHandler()`:
  - 设 `send_flag = 1`
- TMR6中断保持现状（暂不使用，预留）

### 任务5: 编译验证
- Keil编译，确保0错误0警告

### 任务6: 代码简化
- 使用code-simplifier优化代码
- 再次编译确认

---

## 七、定时器分工

| 定时器 | 周期 | 用途 |
|--------|------|------|
| TMR3 | 1ms | 系统时基(wk_delay_ms) |
| TMR6 | 1ms | 预留（可用于看门狗喂狗等） |
| TMR16 | 1s | 触发串口数据帧发送 |

---

## 八、注意事项

1. ADC连续转换+DMA循环模式，只需启动一次软件触发
2. DMA缓冲区必须声明为全局变量（不能在栈上）
3. 滤波处理在主循环中执行，不在中断中（避免长时间占用中断）
4. USART1发送使用轮询方式（16字节@9600baud约16.7ms，可接受）
5. 硬件参数: 3Ω采样电阻 + INA180A2(×50) → ADC输入0.6~3.0V
6. CH9校准通道的滤波结果用于动态校准Vref
7. 电流整数公式: `send_value = adc × 50000 / (ch9 × 3)`，全程uint32_t无浮点

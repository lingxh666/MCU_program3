# samp 自动调度器设计文档

日期: 2026-02-24
状态: 已批准

## 1. 概述

为 samp 工程（AT32F435, FreeRTOS）实现全自动运行能力，支持5种采样触发模式 + 超标留样。

### 1.1 新增模块

| 文件 | 职责 |
|------|------|
| app_scheduler.h/c | 自动调度器（5种模式状态机 + 链式编排） |
| app_adc_module.h/c | AD模块数据接收（UART8 16字节帧解析 + 流量检测） |
| app_retain_judge.h/c | 留样判定（7种留样模式）+ 留样执行 + 排水执行 |

### 1.2 修改模块

| 文件 | 修改内容 |
|------|---------|
| app_config.h/c | 扩展配置结构体（通道限值、通信参数、流量阈值等） |
| app_screen.h/c | 屏幕分发器重写 + 状态页回写（复用samplingB地址） |
| app_modbus.h/c | 新增通信触发寄存器映射 |
| freertos_app.c | Task02/04/05 集成 |
| samp.uvprojx | 添加新文件到Keil工程 |

## 2. 整体架构

### 2.1 任务分工

```
Task02 (100ms, 非阻塞):
  ├── scheduler_run()        — 调度器主循环（5种模式分发）
  ├── sampling_step()        — 采样步进
  └── delivery_step()        — 送样步进

Task04 (50ms):
  ├── Modbus从站通信
  ├── 留样判定 + 排水执行（xTaskNotify接收）
  └── 通信触发请求入口

Task05 (200ms):
  ├── adc_module_poll()      — AD模块数据接收（UART8）
  └── 4G UART数据处理

Task03 (100ms):
  ├── screen_poll_commands() — 屏幕命令处理
  └── screen_write_status_page() — 状态回写（每500ms）
```

### 2.2 数据流

```
UART8(AD模块) → app_adc_module → g_adc_module.raw[6] / flow_value
                                    ↓
                              app_scheduler 读取流量值判断流量触发
                              app_retain_judge 读取6通道判断超标

迪文屏 → app_screen → 配置写入 app_config
                     → 手动命令 → app_scheduler / sampling / delivery
                     ← 状态回写 ← g_state + g_adc_module

数采仪 → app_modbus → 寄存器读写
                     → 通信触发 → g_comm_trigger_request → app_scheduler
                     → 通信留样 → retain_judge / retention_execute
                     ← 状态/数据 ← g_state + g_adc_module
```

## 3. AD模块数据接收 (app_adc_module)

### 3.1 帧协议

```
[0x6B][0xB6][CH1_H][CH1_L]...[CH6_H][CH6_L][0x8C][0xC8]
16字节, 1Hz, 每通道 = mA × 1000 (uint16_t, 大端序)
```

通道映射: CH1=COD, CH2=氨氮, CH3=总磷, CH4=总氮, CH5=流速, CH6=流量

### 3.2 流量值换算

```c
current_ma = raw / 1000.0f;
i_lower = (FlowADLower == 0) ? 0.0f : 4.0f;
ratio = (current_ma - i_lower) / (20.0f - i_lower);
flow_value = clamp(ratio, 0, 1) × FlowMeterBase;  // m³/h
```

### 3.3 流量边沿检测（迟滞比较）

```
上升沿: !flow_active && flow >= FlowStart → flow_active=1, scheduler_notify_flow(1)
下降沿: flow_active && flow <= FlowStop  → flow_active=0, scheduler_notify_flow(0)
```

### 3.4 接口

```c
void     adc_module_init(void);
void     adc_module_poll(void);           // Task05调用
uint16_t adc_module_get_raw(uint8_t ch);
float    adc_module_get_ma(uint8_t ch);
float    adc_module_get_flow(void);       // 换算后流量(m³/h)
uint8_t  adc_module_is_flow_active(void);
uint8_t  adc_module_is_valid(void);       // 2秒超时
```

## 4. 调度器 (app_scheduler)

### 4.1 核心结构

```c
typedef enum {
    SCHED_MODE_TIME_PROP=0, SCHED_MODE_FIXED_TIME=1,
    SCHED_MODE_FLOW=2, SCHED_MODE_SWITCH=3, SCHED_MODE_COMM=4
} sched_mode_t;

typedef enum {
    PHASE_IDLE=0, PHASE_STARTUP, PHASE_CYCLING, PHASE_STOPPED
} sched_phase_t;
```

状态结构体包含公共字段（mode, phase, active_bucket, cycle_idx, sample_done_mask等）和模式专用联合体。

### 4.2 接口

```c
void scheduler_init(sched_mode_t mode);
void scheduler_start(void);
void scheduler_stop(void);
void scheduler_run(void);                // Task02调用, 100ms
void scheduler_notify_flow(uint8_t active);
void scheduler_notify_switch(void);
void scheduler_notify_comm(CommTriggerRequestType req, uint8_t bucket, uint16_t vol);
```

### 4.3 时间等比模式

- 3种启动模式: FULL_SAMPLING / SKIP_TO_CYCLE / INSTANT_SAMPLING
- 周期循环: cycle_idx计算, AB桶交替, 多次累积采样
- 送样: delivery_hour = (cycle_start + (idx+1)×hours - 1) % 24
- delay机制: CycleTime==60时基于AnalysisTime延迟

### 4.4 定时触发模式

- fixedhour[24]非均匀触发, fixedmin分钟
- 无启动阶段, 直接进入CYCLING
- cycle_start: fixedmin>=50同小时, <50上一小时
- 水量>0即送样（用户要求）

### 4.5 流量触发模式

- 事件驱动: scheduler_notify_flow(1/0)
- 3阶段启动: 瞬时送样→满量采样(A桶)→等整点
- 流量停止: 中止采样→送样→notify_task4(0xFF)→排水→STOPPED
- 流量恢复: 重置, 从phase 0重新开始

### 4.6 开关量触发模式

- GPIO低电平触发
- 简化启动: 等下一整点进入周期
- 窗口检测: 采样时间点±1分钟内检测GPIO
- 恢复: 重新走首次触发流程

### 4.7 通信触发模式

- 纯命令驱动, 无自主调度
- COMM_REQ_SAMPLING / COMM_REQ_DELIVERY / COMM_REQ_DRAIN
- 桶切换按采样次数(samples_per_cycle)

## 5. 留样判定 (app_retain_judge)

### 5.1 留样模式（7种）

| Mode | 名称 | 触发方式 |
|------|------|---------|
| 0 | 超标留样 | 6通道模拟量超标边沿触发 |
| 1 | 直接留样 | 每次送样后都留样 |
| 2 | 比对留样 | 暂未实现 |
| 3 | 通信触发 | Modbus写寄存器直接触发 |
| 4 | 同步留样 | 暂未实现 |
| 5 | 只送不留 | 已废弃(用EnableSample) |
| 6 | 开关量触发 | GPIO信号触发 |

### 5.2 判定流程

```
retain_judge_commit(bucket, timestamp):
  if !EnableSample → 不留样
  if modbus_triggered → 留样（最高优先级）
  switch Mode:
    0: check_analog() 6通道边沿检测
    1: 直接返回1
    3: 不经过此函数(直接执行)
    6: 检查switch_triggered标志
```

### 5.3 留样执行流程

```
互斥等待(采样送样完成) → 阀位切换 → 平行留样循环:
  瓶位移动 → 阀位稳定(10s) → 混样循环:
    反吹清线 → 混合电机 → 管存 → 加酸(可选) → 回抽
  → 瓶位标记满, 移到下一空瓶
→ 排空桶
```

### 5.4 Task04集成

送样完成通知编码: value=bucket+1(正常), value=0xFF(流量停止)
判定窗口: 20min ~ AnalysisTime
窗口内每秒调用retain_judge_commit, 触发则执行retention_execute

## 6. 屏幕分发器 (app_screen)

### 6.1 地址体系（复用samplingB）

- 单点控制: cmd_type=0x52
- 设置类: cmd_type=0x50 (采样0x10-0x1C, 送样0x40-0x46, 留样0x60-0x69, 通道0x6A-0x81, 通讯0xB0-0xBB)
- 状态页: 128字节帧, 0x5200起始

### 6.2 ISR改造

从addr+value(4字节)改为缓存完整帧(最大20字节), 支持samplingB变长帧格式。

## 7. 配置扩展 (app_config)

### 7.1 SamplingConfig 新增

flow_start, flow_stop

### 7.2 DeliveryConfig 新增

fixedhour[24], fixedmin

### 7.3 RetainConfig 新增

parallel_count, mix_count, enable_acid

### 7.4 新增结构体

CommConfig: protocol, device_addr, flow_ad_lower, flow_meter_base
ChannelLimitConfig[6]: enable, factor_type, lower_limit, upper_limit

## 8. 实施批次

| 批次 | 内容 | 依赖 |
|------|------|------|
| 1 | app_config扩展 + app_adc_module | 无 |
| 2 | app_scheduler框架 + 时间等比模式 + Task02集成 | 批次1 |
| 3 | 定时/流量/开关量/通信触发模式 | 批次2 |
| 4 | app_retain_judge + Task04集成 | 批次1,2 |
| 5 | app_screen重写 + app_modbus扩展 | 批次1 |
| 6 | Keil工程更新 + 全模式编译验证 | 全部 |

# 软件整体说明书（以源码为准）

## 1. 系统目标与范围
系统用于水质采样设备的自动采样、送样、留样与瓶盘管理，包含门禁事件记录与多协议 Modbus 通讯。软件以“采样 → 送样 → 留样 → 排空”为主流程，确保采样周期一致性、留样可追溯性和对外接口一致性。

## 2. 总体架构与模块划分
### 2.1 任务层（FreeRTOS）
主要任务定义于 `project/src/freertos_app.c`：
- Task01：看门狗与基础初始化/配置重载。
- Task02：MQTT/OTA 处理与系统协调。
- Task03：调度器主循环、采样/送样状态机推进、系统复位 FSM（100ms 周期）。
- Task04：留样判定与执行协调（事件驱动 + 1s 轮询）。
- Task05：屏幕分发与刷新。
- Task06：ADC 采集 + 留样判定数据处理（25ms 周期）。
- Task07：485 协议处理（Dayue/Dahu/Sichuan）。
- Task08：Wiegand 门禁读卡与事件处理（10ms 周期）。
- Task09：西安协议（UART7，若启用）。
- Task10：屏幕命令队列与 KVDB/TSDB 刷写（2s 周期刷写）。

### 2.2 业务层
- 采样/送样状态机：`middlewares/bsp/sampling.c`。
- 留样判定与执行：`middlewares/bsp/retain_judge.c`。
- 调度器：
  - 时间等比：`middlewares/bsp/sampling_time.c`
  - 定时采样：`middlewares/bsp/Timetrigger.c`
  - 流量触发：`middlewares/bsp/Flowtrigger.c`
  - 开关触发：`middlewares/bsp/Switchtrigger.c`
  - 通讯触发：`middlewares/bsp/Commtrigger.c`
- 瓶盘与执行器：`middlewares/bsp/work.c`。

### 2.3 数据与记录
- KVDB（FlashDB）：配置保存/加载（采样、送样、留样、通讯、系统）。
- TSDB：事件记录（采样开始/完成、送样开始/完成、留样、弃样、门禁）。
- record_cache：为协议读取提供滑窗缓存与一致性。

### 2.4 通讯层
- Modbus 四协议：大岳/大湖/四川/西安（`middlewares/bsp/freemodbus/mb_reg_*.c`）。
- 协议选择由 `g_CommSettingConfig.AutoCalibration` 决定。

## 3. 关键配置结构
### 3.1 SampleConfig（采样配置）
字段（单位）：
- `BucketAB`：AB 桶选择
- `SamplingMode`：采样模式（见第 4 节）
- `SamplingImproveTime`（s）
- `SampleInterval`（min）
- `TubeHoldTime`（s）
- `SampleVolume`（mL）
- `CycleTime`（min）
- `BlowbackTime`（s）
- `BucketDrainTime`（s）
- `AnalysisTime`（min）
- `DischargeVolume`（m3）
- `FlowRatio`（mL/m3）
- `FlowStart/FlowStop`（m3/h）

运行默认值（`project/src/freertos_app.c`）：
- `SamplingMode=0`、`SampleInterval=15`、`SampleVolume=500`、`CycleTime=60`
- `SamplingImproveTime=15`、`TubeHoldTime=15`、`BlowbackTime=15`
- `BucketDrainTime=60`、`AnalysisTime=55`
- `DischargeVolume=3`、`FlowRatio=25`、`FlowStart=15`、`FlowStop=5`

校验与回退（`middlewares/bsp/flashDB/app_flashdb.c`）：
- `SampleInterval` 1~1440；`SampleVolume` 10~2000；`CycleTime` ≥ `SampleInterval` 且 ≤ 1440。
- `SamplingMode` > 3 时回退默认 0。
- 校验默认值与运行默认值不同（例如 `SamplingImproveTime=30`、`BlowbackTime=10`、`BucketDrainTime=10`、`AnalysisTime=30`、`DischargeVolume=100`、`FlowRatio=1000`、`FlowStart=100`、`FlowStop=10`），最终以 `g_SampleConfig` 为准。

### 3.2 SampleDeliveryIntervalConfig（送样配置）
- `Enable`：是否启用定时送样
- `StartHour/StartMin`：送样起始时间
- `Duration`：送样持续时间（s）
- `EndHour/EndMin`：送样结束时间（由软件计算）
- `Interval`：送样回抽间隔（s）
- `fixedhour[24]`、`fixedmin`：定时采样辅助参数

### 3.3 RetainSampleModeConfig（留样配置）
- `Mode`：留样模式（见第 4 节）
- `bottleNumber`：当前准备好的空瓶号（1~24）
- `EnableSample`：是否留样（0/1）
- `EnableAcid`：是否加酸（0/1）
- `EnableVacuum`：废水自动排空（0/1）
- `SampleVolume`（mL）、`ParallelCount`（瓶）、`MixCount`（次）
- `TubeHoldTime`、`BlowbackTime`、`BackdrawTime`（s）
- `channelLimits[6]`：因子阈值/平行数/间隔/启用
- `channelData[9]` / `channelCurrent[9]`：ADC 通道数据
- `channelCals[9]`：校准参数

默认值（`project/src/freertos_app.c`）：
- `Mode=3`、`bottleNumber=1`、`EnableSample=1`
- `EnableAcid=0`、`EnableVacuum=0`
- `SampleVolume=200`、`ParallelCount=1`、`MixCount=1`
- `TubeHoldTime=15`、`BlowbackTime=15`、`BackdrawTime=15`

### 3.4 通讯与系统配置
- `CommSettingConfig`：`Protocol`、`DeviceAddr`、`AutoCalibration`、`FlowADUpper/Lower`、`FlowMeterBase`、`IPSET/IDSET`。
- `SystemSettingConfig`：RTC 时间、`AutoRunMode`、`WaterStationMode`、版本号、卡号、`Motorspeed`。
- `CalibrationParams_t`：采样/留样/加酸/温度的校准时间-体积映射。

## 4. 模式与调度处理方式
### 4.1 采样模式（SamplingMode）
- 0 时间等比：`sampling_time.c` 计算日程表，按 `CycleTime`/`SampleInterval` 分配采样时刻，并根据 `g_DeliveryConfig` 生成送样时点。
- 1 定时采样：`Timetrigger.c` 按固定时刻触发采样/送样。
- 2 通讯触发：上位机写寄存器，生成 `g_comm_trigger_request`，由 `Commtrigger.c` 执行。
- 3 流量触发：`retain_judge.c` 通过 4-20mA 采样流量，触发 `Flowtrigger.c`。
- 4 开关触发：`Switchtrigger.c` 读取 GPIO 触发信号（采样/送样/留样）。

### 4.2 留样模式（RetainSampleConfig.Mode）
- 0 超标留样：通道阈值判定触发。
- 1 直接留样：送样完成即留样。
- 3 通讯触发留样：由 Modbus 命令触发留样窗。
- 6 开关量触发留样。
- 5 “只送不留”已废弃，改用 `EnableSample=0` 控制。

### 4.3 启动与调度器
- `AutoRunMode=1`：开机排空 → 初始化 UART4 → `sample_id` 生成器 → 启动对应调度器。
- 时间等比模式支持三种启动策略（补采样/立即采样/等待整点，详见 `sampling_time.c`）。

## 5. 采样状态机（Sampling FSM）
实现于 `middlewares/bsp/sampling.c`：
- `SAMP_DELAY_VALVE_SETUP`：阀门就位延时 10s
- `SAMP_PRE_BLOWBACK`：前反吹（反转，`BlowbackTime`）
- `SAMP_DELAY_500MS_AFTER_PRE_BLOW`：方向切换保护（代码为 1000ms）
- `SAMP_EXTERNAL_PUMP`：外接泵提升（`SamplingImproveTime`）
- `SAMP_TUBE_HOLD`：管存（正转，`TubeHoldTime`）
- `SAMP_DELAY_200MS_AFTER_TUBE_HOLD`：200ms 保护延时
- `SAMP_MEASURE`：计量采样（时长由 `calc_sampling_time_by_volume` 计算）
- `SAMP_DELAY_500MS_AFTER_MEASURE`：500ms 保护延时
- `SAMP_POST_BLOWBACK`：后反吹（`BlowbackTime`）
- `SAMP_COMPLETED` / `SAMP_ABORTED`

中止：`g_manual_operation_abort_flag` 置位时调用 `_sampling_abort()`。

## 6. 送样状态机（Delivery FSM）
实现于 `middlewares/bsp/sampling.c`：
- `DELIV_DELAY_VALVE_SETUP`：阀门就位 10s
- `DELIV_PRE_BLOWBACK` → `DELIV_DELAY_500MS_AFTER_PRE_BLOW`
- `DELIV_STABILIZE`：稳定等待 2s
- `DELIV_START_MIX`：开启混样
- `DELIV_MEASURE`：计量送样
- `DELIV_DELAY_500MS_AFTER_MEASURE`
- `DELIV_BACKDRAW`
- `DELIV_COMPLETED` / `DELIV_ABORTED`

## 7. 留样流程（Retention）
`retain_judge.c:retention_execute` 主流程：
1. 等待采样/送样完成（最多 300s）
2. 检查 `EnableSample` 与 `Mode`，Mode=5 直接返回
3. 切换阀门（出水三通到桶、采样三通到留样）
4. 若瓶位不确定先回 1 号瓶
5. 阀门稳定延时 10s
6. 对每个平行样瓶执行混样循环：
   - 反吹：瞬时阀置瞬时，电机反转 `BlowbackTime`
   - 混合：启动桶混样电机
   - 管存：电机正转 `TubeHoldTime`
   - 留样计量：按 `SampleVolume` 计量（可选加酸）
   - 回抽：电机反转 `BackdrawTime`
7. 更新瓶号、TSDB 记录、`g_RetainBottleState`，完成后排空桶

中止：`scheduler_is_emergency_active()` 或 `g_retention_abort_flag` 触发 abort。

## 8. 瓶盘系统
`middlewares/bsp/work.c`：
- 归零 FSM：`bottle_home_to_1_start/check/stop`，PD0 为 1 号瓶原点传感器。
- 移动 FSM：`bottle_move_to_start/check/stop`，PD1 为位置脉冲。
- 未初始化时自动寻原点；若已在原点则先离开再寻原点。
- Task03 每 300s 自动恢复瓶盘故障，最多 10 次。

## 9. 任务时序与通知
- Task03 在送样完成后调用 `notify_task4_delivery_complete()` 通知 Task04。
- Task06 每 25ms 处理 ADC 与留样判定输入（`retain_judge_process`）。
- 看门狗通过 EventGroup bits 监控 Task2~Task10 心跳。

## 10. 数据存储与记录
- KVDB：配置、瓶盘状态、通讯与系统参数。
- TSDB：采样/送样/留样/弃样/门禁事件。
- `record_cache` 为协议读取提供最近事件窗口。

## 11. 通讯协议
四套协议的寄存器映射与读写规则见 `docs/generated/protocol_registers.md`。

## 12. 启动与复位
- AutoRunMode=1：开机排空（A/B 同时）→ 调度器启动 → 屏幕刷新。
- 系统复位：`system_reset_start()` 进入复位 FSM，Task03 周期更新。
- 手动/远程命令可触发采样/送样/留样/复位。

## 13. 关键 I/O 信号
- 瓶盘：PD0 原点、PD1 位置脉冲
- 液位/触发：PD4 采样液位、PB5 回流液位、PB6 送样液位、PB7 送样触发、PE2 留样触发、PE3 采样触发
- 门禁：PE12 门禁状态
- 废水排空：PE4 浮子开关（`EnableVacuum=1` 时启用）

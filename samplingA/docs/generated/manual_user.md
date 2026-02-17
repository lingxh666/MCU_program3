# 客户使用说明书（以代码行为为准）

## 1. 上电前检查
- 确认阀门、泵、电机与瓶盘联线正常，瓶盘已装好空瓶并关闭门禁。
- 若启用废水自动排空（`EnableVacuum=1`），确保 PE4 浮子开关与排水阀接线正确。

## 2. 基础配置
### 2.1 时间与通讯
- 设置系统时间（屏幕或 Modbus 校时）。
- 配置设备地址 `DeviceAddr` 与协议类型（Dayue/Dahu/Sichuan/Xian）。
- 如需远程联动，确认上位机协议与本机一致。

### 2.2 自动运行模式
- `AutoRunMode=1`：系统上电后自动排空并启动调度。
- `AutoRunMode=0`：保持待机，需要手动或上位机触发启动。

## 3. 采样设置
核心参数来自 `SampleConfig`：
- 采样模式 `SamplingMode`
- 单次采样量 `SampleVolume`（10~2000 mL）
- 采样间隔 `SampleInterval`（1~1440 min）
- 周期时间 `CycleTime`（≥ 间隔，≤ 1440 min）
- 反吹/提升/管存时间：`BlowbackTime`、`SamplingImproveTime`、`TubeHoldTime`
- 分析时间 `AnalysisTime`、排空时间 `BucketDrainTime`
- 流量触发阈值 `FlowStart/FlowStop`、流量比例 `FlowRatio`

## 4. 送样设置
`SampleDeliveryIntervalConfig`：
- `Enable`：是否启用定时送样
- `StartHour/StartMin`：送样起始时间
- `Duration`：送样持续时间（s）
- `Interval`：送样回抽间隔（s）

时间等比模式下，送样时间点由系统根据周期自动计算并写入调度表。

## 5. 留样设置
`RetainSampleModeConfig`：
- `EnableSample`：是否留样（0=不留样）
- `Mode`：留样模式（0 超标 / 1 直接 / 3 通讯触发 / 6 开关触发）
- `SampleVolume`：单次留样量
- `ParallelCount`：平行样瓶数（1~24）
- `MixCount`：混样次数
- `EnableAcid`：是否加酸
- `TubeHoldTime`、`BlowbackTime`、`BackdrawTime`
- `bottleNumber`：当前准备好的空瓶号（1~24）
- 通道阈值：`channelLimits[1..6]` 的因子类型、上/下限、启用标志

提示：Mode=5“只送不留”已废弃，请用 `EnableSample=0` 控制。

## 6. 运行模式说明
### 6.1 时间等比（SamplingMode=0）
- 设置 `CycleTime` 与 `SampleInterval` 后自动生成全天采样计划。
- 送样时刻由系统计算，送样完成后触发留样判定。

### 6.2 定时采样（SamplingMode=1）
- 设定固定时刻；系统到点触发采样与送样。

### 6.3 通讯触发（SamplingMode=2）
- 上位机通过协议寄存器触发采样/送样/排空/留样。
- 推荐在通讯触发模式下使用寄存器控制桶动作。

### 6.4 流量触发（SamplingMode=3）
- 通过 4-20mA 流量测量，达到 `FlowStart` 进入采样调度，低于 `FlowStop` 停止并触发留样判定。

### 6.5 开关触发（SamplingMode=4）
- 采样/送样/留样由外部开关量触发（PE3/PE2/PB7）。
- 注意：配置保存时校验限制 `SamplingMode<=3`，如写入 4，重启可能回退为 0。

## 7. 手动操作与维护
- 屏幕命令支持：手动采样、手动送样、瞬时送样、手动留样、瞬时留样、瓶盘复位、系统复位。
- 系统复位进行中将忽略瓶盘复位；瓶盘复位进行中将拒绝系统复位。

## 8. 瓶盘维护
- 瓶盘复位：将瓶盘归零到 1 号瓶，并更新保存。
- 瓶盘满瓶：协议状态中“已用瓶数”达到 24 时需更换空瓶。
- 协议支持单瓶/全瓶复位与排空（详见协议对照表）。

## 9. 门禁与权限
- 门禁开关记录进入 TSDB，部分协议提供动态密码（999999 - DDHHMI）。
- 门禁连续打开超过 30 分钟会触发报警状态（Dayue/Dahu）。

## 10. 常见行为提示
- 若 `EnableSample=0`，送样完成后将直接排空，不执行留样。
- 留样执行前会等待采样/送样完成，最长 300s。
- 留样过程中再次触发采样会被桶状态阻止，需等待留样结束。

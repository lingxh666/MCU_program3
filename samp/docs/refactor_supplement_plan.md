# samp 重构补充计划 — 未完成项全量清单

更新时间：2026-02-25
基于：`samp/docs/compare_samp_vs_samplingB.md` + 深度代码分析

---

## 0. 总览

原重构计划（6批次22任务）已完成"8任务骨架 + 4个核心业务模块拆分"。
本补充计划覆盖 **所有尚未对齐 samplingB 的能力模块**，按依赖关系分为 8 个批次。

### 缺失分类

| 类别 | 数量 | 说明 |
|------|------|------|
| 完全缺失 | 8项 | samplingB 有明确模块，samp 无对应物 |
| 部分完成（重大差距） | 6项 | samp 有骨架但能力显著缩水 |

---

## 1. 完全缺失项（按优先级排序）

### 1.1 4G模组连接 + AT指令引擎（app_4g_modem）
- **samplingB参考**: `ota.c` 中的 `MqttInit()`, `ResetModle()`, `TestATCommand()`, `timesyc()`, `filter_spaces()`, `extract_number_from_response()`
- **samp现状**: `my_task05_func` 仅有空壳轮询，无AT指令发送/解析
- **需实现**:
  - AT指令发送与应答解析引擎（超时+重试）
  - 4G模组上电初始化序列（AT→CPIN→CSQ→CGATT→...）
  - 信号质量查询
  - 时间同步（`timesyc` → RTC校准）
  - IP/APN配置管理

### 1.2 MQTT连接与通信（app_mqtt）
- **samplingB参考**: `ota.c` 中的 `MqttSend()`, `MqttSendStatusAll()`, `MqttSendSettingsAll()`, `mqtt_publish_message()`, `mqtt_check_connected()`, `mqtt_send_sample_config()` 等12个mqtt_send_*函数
- **samp现状**: 完全缺失
- **需实现**:
  - MQTT连接/断开/重连状态机（3次重试 + 60秒后台重连）
  - 状态上报（15分钟周期）：采样状态、送样状态、留样状态、水量状态
  - 设置上报（1小时周期）：采样配置、送样配置、留样基础、通道阈值、通道校准
  - 远程命令接收与解析（SET_ID、SET_IP、调试指令）
  - 调试模式（`CheckDebugCommand`, `IsInDebugMode`, `ProcessDebugCache`, `MqttDebugSend`）

### 1.3 OTA升级（app_ota）— 4G + USB 双通道
- **samplingB参考**: `ota.c/h`（8状态OTA状态机）+ `usbh_user.c`
- **samp现状**: `usbh_user.c` 仅有U盘固件头校验，Flash写入未实现
- **需实现**:
  - **4G OTA**: MQTT接收OTA数据包 → Base64解码 → 分包校验 → Flash写入 → 固件验证 → 系统重启
  - **USB OTA**: U盘固件读取 → CRC32校验 → Flash写入 → 系统重启
  - OTA状态机（IDLE→INIT→WAIT_DATA→PROCESS_PACKET→WRITE_FLASH→VERIFY→COMPLETE/ERROR）
  - OTA期间挂起其他任务 + 120秒超时保护
  - ACK/NACK应答机制

### 1.4 Bootloader工程
- **samplingB参考**: `samplingB/BOOTLOADER/` 完整Keil工程（AT32F403A）
- **samp现状**: 完全缺失
- **需实现**:
  - AT32F435 Bootloader工程（独立Keil工程）
  - 升级标志检测（flash_upgrade_flag_read）
  - 固件搬运（从OTA分区拷贝到APP分区）
  - 跳转到APP执行
  - Flash分区规划（Bootloader区 / APP区 / OTA暂存区 / KVDB区 / TSDB区）

### 1.5 内部Flash读写工具（bsp_flash）
- **samplingB参考**: `flash.c/h` — `flash_read()`, `flash_write()`, `flash_2kb_write()`, `flash_upgrade_flag_read()`, `write_upgrade_flag()`
- **samp现状**: 缺失（仅有QSPI外部Flash驱动）
- **需实现**:
  - AT32F435内部Flash读/写/擦除封装
  - 2KB页写入（带擦除保护）
  - 升级标志读写（OTA与Bootloader共享）

### 1.6 样本ID生成器（app_sample_id）
- **samplingB参考**: `sample_id.c/h` — 线程安全，基于RTC时间戳+序列号
- **samp现状**: 完全缺失
- **需实现**:
  - `SampleIdGenerator` 结构体（last_seq, last_timestamp, mutex）
  - `sample_id_generator_init()` — 从TSDB恢复最后序列号
  - `generate_sample_id()` — 线程安全生成唯一ID
  - `sample_id_get_current_seq()` / `sample_id_generator_reset()`

### 1.7 屏幕缓存机制（app_screen_cache）
- **samplingB参考**: `screen_cache.c/h` — KVDB脏标记 + TSDB环形缓存 + 定期刷写
- **samp现状**: 完全缺失（KVDB直接写入，无缓存层）
- **需实现**:
  - KVDB缓存：7类脏标记（采样/送样/留样/留样状态/通讯/系统/校准）
  - TSDB缓存：环形缓冲区 + mutex保护
  - `kvdb_cache_init/mark_dirty/flush_all/has_dirty`
  - `tsdb_cache_init/append/flush_all/count`
  - 定期刷写任务（2秒周期）

### 1.8 门锁管理 + 韦根刷卡增强
- **samplingB参考**: `task08_func` — 卡号授权检查 + 开门控制 + 门锁统计 + `door_event_process()`
- **samp现状**: `bsp_wiegand.c` 有底层驱动，但无业务层（授权/开门/统计/TSDB记录）
- **需实现**:
  - 卡号授权检查（对比 `SystemSettingConfig.CardId[]`）
  - 开门控制（DoorRun/DoorStop + 持续时间记录）
  - 门锁事件TSDB记录（`door_event_process`）
  - 门锁统计查询（`door_get_stats`）

---

## 2. 部分完成项（重大差距）

### 2.1 留样判定增强（app_retain_judge）
- **samp现状**: 基础接口（init/commit/reset/notify_switch/notify_modbus/retention_execute/drain_execute_blocking）
- **samplingB额外能力**:
  - `retain_judge_process()` — 10ms周期ADC采集+双级滤波（L2/L3缓冲区）
  - `retain_judge_check_analog()` — 模拟量超标检测（6通道，上下限）
  - `retain_judge_check_flow()` — 流量超标检测
  - TSDB留样记录写入（`_retain_tsdb`）
  - 屏幕电流值推送（`retain_send_current_values_to_screen`）
  - 瓶位管理（`retain_clear_all_bottles`, `retain_set_bottle_position_uncertain`, `retain_get_bottle_status`）
  - 留样统计（`RetainJudgeStats`）
  - 水量变化记录（`_record_water_volume_change`）

### 2.2 采样/送样状态机增强（app_sampling）
- **samp现状**: 基础非阻塞状态机（sampling/delivery/retain/drain 4个step函数）
- **samplingB额外能力**:
  - `sample_id` 集成（每次采样/送样/留样生成唯一ID）
  - TSDB日志记录结构体：`SamplingStartRecord`, `SamplingCompleteRecord`, `DeliveryStartRecord`, `DeliveryCompleteRecord`, `RetainLogRecord`
  - `log_retain_record()` — 留样完整日志
  - `WaterSampleContext`（A/B桶水样就绪跟踪）
  - `ManualOperationImpact`（手动操作影响追踪）
  - `system_reset_start/update/is_active` — 系统复位状态机
  - `update_global_state_time()`, `update_all_timers()`
  - `analysis_report_switch/modbus/analog` — 分析报告触发
  - `update_bucket_state()`, `update_system_running_state()`
  - `log_water_volume_change()`, `log_manual_operation_impact()`, `log_cycle_task_skipped()`

### 2.3 调度器增强（app_scheduler）
- **samp现状**: 5种模式框架（time_prop/fixed_time/flow/switch/comm），但每种模式内部逻辑简化
- **samplingB额外能力**:
  - **时间等比**: `DailyTimeSchedule`（全天时间表构建）、`FirstBucketASchedule`、`TpOperationSlot`、4种启动模式（FULL_SAMPLING/INSTANT_DELIVERY/SKIP_TO_CYCLE/INSTANT_SAMPLING）、推迟采样累计、手动影响水量追踪
  - **定时采样**: `FixedTimeSchedulerState`（多送样时间点、送样索引、周期管理）
  - **流量触发**: `FlowTriggerSchedulerState`（流量开始/停止通知、瞬时送样、留样完成回调、延迟偏移）
  - **开关量触发**: `SwitchTriggerSchedulerState`（GPIO信号检测、窗口检查、首次触发处理、采样暂停/恢复）
  - **通讯触发**: `CommTriggerSchedulerState`（已部分存在，需补齐窗口留样联动）
  - `scheduler_dispatcher()` — 统一分发器（根据模式调用对应调度函数）

### 2.4 串口屏增强（app_screen）
- **samp现状**: 基础框架（变量地址表 + 命令处理 + 状态回写）
- **samplingB额外能力**:
  - `screen_message_dispatcher()` — 屏幕消息分发器（10ms周期）
  - `Screen_init()` — 完整屏幕初始化（写入所有配置页面初始值）
  - `write_begin_page()` — 主页刷新（1秒周期）
  - `screen_is_on_home_page()` — 页面状态跟踪
  - 屏幕就绪事件位（`SCREEN_READY_BIT`）
  - 手动操作命令队列（`ScreenCommand` + `queue_screen_cmd`）：
    - 手动采样/送样/瞬时送样/留样/瞬时留样
    - 瓶盘复位（非阻塞FSM）
    - TSDB格式化
    - 系统复位/系统复位状态机
  - 屏幕命令处理任务（samplingB task10）— 独立任务处理长耗时屏幕命令

### 2.5 记录查询/缓存增强（app_record_query）
- **samp现状**: 5类记录分页查询 + 固定页缓存 + 同步查询
- **samplingB额外能力**:
  - `RecordCacheManager` — 统一缓存管理器（5类缓存：采样/送样/留样/掉电/门锁）
  - 滑动窗口（`cache_slide_window_forward/backward`）
  - 智能预加载（`decide_preload` + `preload_task` 异步任务）
  - Mutex保护（`cache_manager.mutex`）
  - `cache_rebuild_window_around_page()` — 页面跳转时重建窗口
  - `cache_add_*()` — 实时追加新记录到缓存
  - `cache_get_stats()` / `cache_clear()` / `cache_clear_all()`

### 2.6 配置结构体补全（app_config）
- **samp现状**: 5个配置结构体（SystemState, SamplingConfig, DeliveryConfig, RetainConfig, CommConfig）+ ChannelLimitConfig
- **samplingB额外字段/结构体**:
  - `SystemSettingConfig` — 系统设置（AutoRunMode, CardId[10], BucketDrainTime等）
  - `CalibrationParams` — 校准参数
  - `SingleSampleTest` — 单次测试参数
  - `RetainBottleState` — 瓶位状态（currentBottle + 24瓶使用位图）
  - `FactorDataFromHost` / `FactorCount` — 上位机因子数据
  - `CommSettingConfig` 扩展字段（IDSET, IPSET, Protocol, DeviceAddr, FlowMeterType）
  - `State` 结构体扩展（samplingB的State有40+字段，samp的SystemState仅23字段）
  - `cfg_save_retain()`, `cfg_save_retain_state()`, `cfg_save_comm()`, `cfg_load_retain_state()` 等分类保存函数

---

## 3. 批次执行计划

### Batch S1: 基础设施层（无外部依赖）
**预计工作量**: 中等

| 任务 | 新建文件 | 说明 |
|------|----------|------|
| S1-1 | `bsp_flash.c/h` | AT32F435内部Flash读写封装（OTA前置依赖） |
| S1-2 | `app_sample_id.c/h` | 线程安全样本ID生成器 |
| S1-3 | `app_screen_cache.c/h` | KVDB脏标记 + TSDB环形缓存 + 定期刷写 |
| S1-4 | 修改 `app_config.c/h` | 补全配置结构体（SystemSettingConfig, CalibrationParams, RetainBottleState等） |

**验收标准**: Keil编译0错误0警告，各模块init函数可独立调用

### Batch S2: 采样/留样能力增强（依赖 S1）
**预计工作量**: 大

| 任务 | 修改文件 | 说明 |
|------|----------|------|
| S2-1 | `app_sampling.c/h` | 集成sample_id、TSDB日志记录、WaterSampleContext、系统复位状态机 |
| S2-2 | `app_retain_judge.c/h` | 增加10ms周期ADC处理、双级滤波、TSDB记录、屏幕电流推送、瓶位管理 |
| S2-3 | `app_sampling.c/h` | ManualOperationImpact、analysis_report_*、update_bucket_state |

**验收标准**: 采样/送样/留样全流程带TSDB日志记录，retain_judge_process可10ms周期调用

### Batch S3: 调度器深度增强（依赖 S2）
**预计工作量**: 大

| 任务 | 修改文件 | 说明 |
|------|----------|------|
| S3-1 | `app_scheduler.c/h` | 时间等比增强：DailyTimeSchedule构建、4种启动模式、推迟累计 |
| S3-2 | `app_scheduler.c/h` | 定时采样增强：FixedTimeSchedulerState、多送样时间点 |
| S3-3 | `app_scheduler.c/h` | 流量触发增强：FlowTriggerSchedulerState、瞬时送样、留样回调 |
| S3-4 | `app_scheduler.c/h` | 开关量触发增强：SwitchTriggerSchedulerState、GPIO信号、窗口检查 |

**验收标准**: 5种调度模式均可独立init/start/stop，scheduler_dispatcher正确分发

### Batch S4: 串口屏 + 记录查询增强（依赖 S1, S2）
**预计工作量**: 中等

| 任务 | 修改文件 | 说明 |
|------|----------|------|
| S4-1 | `app_screen.c/h` | 屏幕消息分发器、主页刷新、页面状态跟踪、屏幕就绪事件 |
| S4-2 | `app_screen.c/h` | 手动操作命令队列（ScreenCommand）+ 屏幕命令处理逻辑 |
| S4-3 | `app_record_query.c/h` | 滑动窗口、智能预加载、Mutex保护、实时追加 |

**验收标准**: 屏幕命令队列可接收/处理，记录查询支持前后翻页预加载

### Batch S5: 4G模组 + MQTT（依赖 S1）
**预计工作量**: 大

| 任务 | 新建/修改文件 | 说明 |
|------|--------------|------|
| S5-1 | 新建 `app_4g_modem.c/h` | AT指令引擎、4G模组初始化序列、信号查询、时间同步 |
| S5-2 | 新建 `app_mqtt.c/h` | MQTT连接状态机、状态/设置上报、远程命令解析 |
| S5-3 | 修改 `freertos_app.c` Task05 | 集成4G+MQTT到任务循环，替换当前空壳 |

**验收标准**: 4G模组可完成初始化序列，MQTT可连接/发送/接收

### Batch S6: OTA升级（依赖 S1, S5）
**预计工作量**: 大

| 任务 | 新建/修改文件 | 说明 |
|------|--------------|------|
| S6-1 | 新建 `app_ota.c/h` | OTA状态机（8状态）、Base64解码、分包校验、Flash写入 |
| S6-2 | 修改 `usbh_user.c` | 补全USB OTA的Flash写入逻辑 |
| S6-3 | 修改 `freertos_app.c` Task05 | OTA流程集成（任务挂起/恢复、超时保护） |

**验收标准**: 4G OTA可接收固件包并写入Flash，USB OTA可从U盘读取固件并写入Flash

### Batch S7: Bootloader工程 + 门锁增强（依赖 S1, S6）
**预计工作量**: 中等

| 任务 | 新建/修改文件 | 说明 |
|------|--------------|------|
| S7-1 | 新建 `samp/BOOTLOADER/` 工程 | AT32F435 Bootloader（升级标志检测→固件搬运→跳转APP） |
| S7-2 | 修改 `fal_cfg.h` | Flash分区规划对齐（Bootloader/APP/OTA/KVDB/TSDB） |
| S7-3 | 修改 `freertos_app.c` Task08 | 门锁业务层（卡号授权、开门控制、TSDB记录、统计） |

**验收标准**: Bootloader工程独立编译通过，门锁刷卡→授权→开门→记录全流程

### Batch S8: 任务集成 + 首次开机序列（依赖 S1-S7 全部）
**预计工作量**: 中等

| 任务 | 修改文件 | 说明 |
|------|----------|------|
| S8-1 | `freertos_app.c` | 首次开机初始化序列：瓶盘复位→排空→sample_id初始化→缓存管理器初始化→调度器自动启动 |
| S8-2 | `freertos_app.c` | 断电恢复序列：KVDB加载瓶位→瓶盘位置恢复→system_start_sequence(POWER_RECOVERY) |
| S8-3 | `freertos_app.c` | 任务间IPC完善：SCREEN_READY_BIT、KVDB_READY_BIT、TSDB_READY_BIT事件同步 |
| S8-4 | `freertos_app.c` | 看门狗增强：超时任务分析、连续超时计数、TSDB事件记录、自动复位 |

**验收标准**: 上电→初始化→排空→调度器启动全流程自动执行，断电恢复正确

---

## 4. 批次依赖关系

```
S1 (基础设施) ──┬──→ S2 (采样/留样增强) ──→ S3 (调度器增强)
                │                           │
                ├──→ S4 (屏幕+记录增强) ←───┘
                │
                ├──→ S5 (4G+MQTT) ──→ S6 (OTA) ──→ S7 (Bootloader+门锁)
                │
                └──→ S8 (任务集成) ←── 依赖 S1~S7 全部
```

可并行路径：
- 路径A: S1 → S2 → S3 → S4（业务增强线）
- 路径B: S1 → S5 → S6 → S7（通信/OTA线）
- 汇合: S8（全部完成后集成）

---

## 5. 新建文件清单

| 文件 | 批次 | 说明 |
|------|------|------|
| `middlewares/bsp/bsp_flash.c/h` | S1 | AT32F435内部Flash读写 |
| `middlewares/bsp/app_sample_id.c/h` | S1 | 样本ID生成器 |
| `middlewares/bsp/app_screen_cache.c/h` | S1 | KVDB/TSDB缓存层 |
| `middlewares/bsp/app_4g_modem.c/h` | S5 | 4G模组AT指令引擎 |
| `middlewares/bsp/app_mqtt.c/h` | S5 | MQTT通信模块 |
| `middlewares/bsp/app_ota.c/h` | S6 | OTA升级状态机 |
| `BOOTLOADER/` 整个工程 | S7 | AT32F435 Bootloader |

共新建 **6对.c/.h文件 + 1个独立工程**

---

## 6. 需要确认的决策点

### 决策1: 触发器模块化 vs 内聚到 app_scheduler
- **已确认**: 方案A — 保持触发逻辑内聚在 `app_scheduler.c`，补齐边界条件和状态管理

### 决策2: FreeModbus 库 vs 自实现协议栈
- **已确认**: 使用 `freemodbus/` 库，多实例支持（大岳/大湖/西安3个实例），从samplingB移植

### 决策3: 4G模组型号
- **已确认**: 与samplingB完全相同的4G模组，AT指令集完全一致，直接移植

### 决策4: Flash分区规划
- **已确认**: 与samplingB完全相同的分区方案，保留samp已有的断电存储分区

---

## 7. 总结

| 维度 | 数量 |
|------|------|
| 完全缺失模块 | 8 项 |
| 部分完成（重大差距） | 6 项 |
| 新建文件 | 6 对 .c/.h + 1 个 Bootloader 工程 |
| 修改文件 | 约 10 个现有文件 |
| 执行批次 | 8 批（S1~S8） |
| 可并行路径 | 2 条（业务增强线 + 通信/OTA线） |

执行顺序建议：先走 **路径A（S1→S2→S3→S4）** 完成业务能力对齐，再走 **路径B（S5→S6→S7）** 完成通信/OTA，最后 **S8** 做全量集成。

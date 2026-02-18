# Codex 分步驱动测试执行计划（临时测试任务“逐项插入/删除”）

> 适用工程：`D:\MCU_program3\samp`  
> 关联总计划：`samp/docs/driver_test_plan_2026-02-18.md`（本文件把它拆成更小、更可执行的步骤）  
> 约束：**本次仅产出执行计划文档**；真正执行测试时，允许“临时插入/删除测试任务代码”，每次只验证一个很小目标。  

---

## 0. 总原则（请先统一）

### 0.1 为什么要“临时测试任务”

很多驱动（GPIO/ADC/QSPI/FlashDB/UART）需要周期性运行、打印结果、或在 RTOS 环境下做互斥/等待。为了能在**不引入长期测试代码**的前提下获得自反馈，我们采用：

- 在 `FreeRTOS` 中临时增加/改写一个“Codex 测试任务”（使用已有空任务 `my_task02_func()`）
- 每一步只测一个小目标：**通过 → 删除本步测试代码 → 写入下一步测试代码**

### 0.2 每一步的统一验收方式

每一步都遵循同一套闭环：

1. **插入本步测试代码**（只改一个文件：建议固定改 `samp/project/src/freertos_app.c`）
2. **Keil 编译**（必须 0 Error / 0 Warning）
3. **下载运行**（或 Debug Run）
4. **采集反馈**（USART1 日志 / USB / CAN 抓包 / 万用表 / 逻辑分析仪）
5. **按“通过标准”判定**
6. **删除本步测试代码**（恢复到干净状态，再进入下一步）

> 建议把每一步的日志/截图按用例ID保存，避免“测过但没证据”。

### 0.3 推荐的“删除测试代码”方法（避免手工删错）

执行测试时（不是现在）建议使用 Git 做快速回滚：

```powershell
# 进入下一步前，确保只修改了 freertos_app.c
git diff

# 通过后：直接恢复该文件到仓库基线（删除本步测试代码）
git restore samp/project/src/freertos_app.c

# 确认工作区干净，再开始下一步
git status --ignore-submodules=dirty
```

> 如果某一步不得不改动多个文件：也必须在步骤里明确列出“要改的文件清单”，并在通过后逐个 restore。

---

## 1. 测试前置准备（一次性）

### Step P0：基线编译（证明工程可构建）

- 目标：确认当前工程能稳定编译；后续每一步都以这个基线为参考。
- 执行：

```powershell
& 'C:\Keil_v5\UV4\UV4.exe' -b 'D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx' -o build_log.txt -j0
```

- 通过标准：`build_log.txt` 显示 **0 Error / 0 Warning**

### Step P1：USART1 日志通路确认（必须）

现有 `printf` 已重定向到 USART1（`samp/project/src/wk_system.c`）。

- 目标：能持续抓到日志（后续所有自反馈都依赖它）
- 连接：PA9(USART1_TX) → USB-TTL RX（3.3V），波特率 9600
- 通过标准：上电/运行期间能看到至少一条日志（或你在后续步骤里打印出的测试 banner）

> 如果当前固件没有打印任何内容：在后续步骤的“临时测试任务”里会打印 banner 来验证链路。

---

## 2. 临时测试任务框架（每一步都以它为载体）

### 2.1 固定改动位置（执行测试时）

仅修改：`samp/project/src/freertos_app.c` 的 `my_task02_func()`

理由：
- `my_task02_func()` 当前为空循环，适合临时承载测试逻辑
- 不需要改任务创建结构（已有 `my_task02` 被创建）

### 2.2 通用模板（每一步都从这里开始粘贴/替换）

执行测试时，把 `my_task02_func()` 的 while(1) 内容替换为下述“单次运行后挂起”的结构（然后在每一步里填入具体测试代码）：

```c
/* 仅供执行测试时临时插入：my_task02_func 内部模板 */
printf("\r\n[CODEX][STEP] <填入步骤编号/名称>\r\n");

/* TODO: 本步骤测试代码写在这里 */

printf("[CODEX][STEP] DONE\r\n");
vTaskSuspend(NULL); /* 通过后挂起，避免重复执行影响硬件 */
```

### 2.3 本计划默认的“执行/观察”方式

- 执行：下载后上电运行（或 Debug Run）
- 观察：串口抓 `[CODEX]` 前缀日志；必要时配合万用表/LA/CAN 抓包

---

## 3. 分步测试清单（从小到大，逐步推进）

下面每个 Step 都是一个“小目标”。**必须按顺序执行**：前一步没通过，不进入下一步。

> 说明：每个 Step 里我都写清楚：目标/目的、要临时写入的测试代码要点、如何执行、通过标准、通过后如何清理。

---

### Step T00：Codex 测试任务能运行（最小自检）

- 目标：证明 `my_task02_func()` 能打印并运行到结束。
- 目的：排除“任务没跑/printf 不通/系统没进调度器”等基础问题。
- 临时代码（写入 `my_task02_func()`）：
  - 打印 banner
  - `vTaskSuspend(NULL)`
- 执行：编译→下载→运行
- 通过标准：串口出现：
  - `[CODEX][STEP] T00 ...`
  - `[CODEX][STEP] DONE`
- 通过后清理：`git restore samp/project/src/freertos_app.c`

---

### Step T01：GPIO 输出（继电器/阀门）单点验证（先测一个输出）

- 目标：只验证 1 路输出（建议从“安全负载/不接大功率”的一路开始）
- 目的：先证明 `relay_set()` 能驱动到真实引脚电平变化，再扩大到全量。
- 临时代码要点：
  1. `#include "bsp_io.h"`
  2. `relay_set(RELAY_INLET_VALVE, 1);` 延时 1s → `relay_set(...,0);`
  3. 打印 `relay_get_state()` 返回值
- 执行：
  - 运行时用万用表测对应 IO 电平，或看继电器指示灯/听吸合声
- 通过标准：
  - 电平确实从低→高→低（或继电器吸合→释放）
  - `relay_get_state()` 与实际一致
- 通过后清理：restore `freertos_app.c`

---

### Step T02：GPIO 输出全量扫描（逐路开关）

- 目标：把 `relay_id_t` 0..`RELAY_COUNT-1` 全扫一遍。
- 目的：快速发现“引脚表/枚举顺序/硬件连线”错误。
- 临时代码要点：
  - for 循环：每路 ON 500ms、OFF 500ms
  - 每路打印 `id` 与状态
- 执行：建议你把高压/大电流负载先断开或用假负载。
- 通过标准：
  - 无异常复位/卡死
  - 每一路都能观察到对应的电平/继电器动作（至少抽查 30%+）
- 通过后清理：restore

---

### Step T03：H桥瓶排空电机（PE14/PE15）三态验证

- 目标：验证 `MOTOR_EMPTY/MOTOR_RESTORE/MOTOR_STOP` 的互斥与电平组合。
- 目的：避免 H 桥双高导致硬件风险。
- 临时代码要点：
  - `bottle_motor_set(MOTOR_EMPTY);` 延时 → `MOTOR_RESTORE` → `MOTOR_STOP`
  - 每步打印 `bottle_motor_get_dir()`
- 你需要如何确认：
  - 逻辑分析仪夹 PE14/PE15（或万用表），确认三态电平组合与文档一致
- 通过标准：
  - 不出现 PE14=H 且 PE15=H
  - `get_dir()` 返回符合预期
- 通过后清理：restore

---

### Step T04：GPIO 输入读取（先测 1 路，再扩展全量）

- 目标：先选 1 路输入做高低翻转测试（比如拨码/触发开关）。
- 目的：确认 `input_read()` 的电平语义（1=SET？）与你的硬件一致。
- 临时代码要点：
  - 每 100ms 打印一次 `input_read(INPUT_XXX)`
  - 或者只在状态变化时打印（更利于观察）
- 执行：
  - 你手动拨动开关/触发传感器，观察日志变化
- 通过标准：
  - 输入变化能被稳定读到（允许抖动，但基本逻辑一致）
- 通过后清理：restore

---

### Step T05：拨码开关组合值（PE11/12/13）

- 目标：验证 `input_get_dip_switch()` 返回 0~7 的组合值。
- 临时代码要点：
  - 每 200ms 打印一次 `input_get_dip_switch()`
- 通过标准：
  - 你切换拨码后，返回值按 bit0/1/2 正确变化
- 通过后清理：restore

---

### Step T06：ADC1 DMA 启动 + 2.5V 校准（只验证 VREF）

- 目标：只验证 `bsp_adc1_dma_start()` + `adc_cal_run()` 工作正常，VREF≈2.5V。
- 目的：先把 ADC1 DMA“跑起来”，后续电流/温度才有意义。
- 临时代码要点：
  1. `#include "bsp_adc.h"`
  2. 调用 `bsp_adc1_dma_start();` 延时几百 ms
  3. 调用 `adc_cal_run();`
  4. 打印 `adc_cal.vref_raw`、`adc_cal.vref_factor`、`adc1_get_voltage_mv(ADC_CH_VREF)`
- 通过标准：
  - `adc_cal.valid==1`
  - VREF 电压接近 2500mV（先按 ±100mV 宽松门槛；后续再收紧）
- 通过后清理：restore

---

### Step T07：ADC1 温度两路（只看“合理性”）

- 目标：打印 `adc_get_temp(0/1)`，确认不为 -999 且数值合理。
- 目的：先证“温度通道没开路/短路”，再做精度对比。
- 临时代码要点：
  - 先确保执行过 T06（ADC1 DMA 在跑）
  - 每 1s 打印温度 10 次
- 通过标准：
  - 不出现 -999
  - 室温附近数值合理（比如 0~50°C）
- 通过后清理：restore

---

### Step T08：ADC2 4-20mA（单通道三点）

- 目标：先只测 1 个通道（PC2 或 PA1），在 4/12/20mA 三点读数。
- 目的：先证公式与硬件链路可用，再扩展第二通道。
- 临时代码要点：
  - 每 500ms 调用 `adc2_get_420ma_current(ADC_420MA_CHx)` 打印
- 执行：
  - 你调电流源到 4→12→20mA，每个点停留 5s
- 通过标准：
  - 读数随电流单调变化，且误差在你可接受范围（建议先 ±5%）
- 通过后清理：restore

---

### Step T09：UART 回环（先做 1 个端口：USART2）

- 目标：先验证 `UART_PORT_COLLECTOR(USART2)` 的 DMA+IDLE 收包链路。
- 目的：验证 `bsp_uart_send/get_rxdata` 的核心框架可用。
- 你需要怎么连线：
  - 短接 PD5(TX) ↔ PD6(RX)
- 临时代码要点：
  1. `#include "bsp_uart.h"`
  2. 发送固定字符串（如 `"HELLO"`）
  3. 延时 50~200ms 后读回 `bsp_uart_get_rxdata()`
  4. 比较并打印 PASS/FAIL
- 通过标准：
  - 读回长度正确、内容完全一致
- 通过后清理：restore

> 其他 UART 端口（UART4/5/6/7/8、USART3）建议拆成 T10/T11/... 每步只测一个端口，避免连线/配置混淆。

---

### Step T10：UART 回环（UART4 串口屏端口）

- 目标：验证 `UART_PORT_SCREEN(UART4)` 回环。
- 连线：短接 UART4 TX/RX（按硬件引脚定义）
- 其余同 T09。

---

### Step T11：串口屏发帧（只验证 TX 帧格式）

- 目标：调用 `screen_write_u16()`，用逻辑分析仪抓 UART4 TX 帧，确认 `5A A5` 帧头与长度/命令字段正确。
- 目的：不依赖屏资源，先验证协议封装。
- 临时代码要点：
  - `#include "bsp_screen.h"`
  - `screen_init();`
  - `screen_write_u16(0x1000, 0x1234);`
- 通过标准：
  - LA 抓到帧头、长度字段与 `bsp_screen.c` 组帧逻辑一致
- 通过后清理：restore

---

### Step T12：CAN 电机控制（先做“发帧可见”）

- 目标：调用 `can_motor_set_speed()` 后，用 CAN 抓包看到 ID=0x100 的数据帧。
- 目的：先证主控发帧链路与帧格式，再做状态回读/联动。
- 临时代码要点：
  - `#include "bsp_can_motor.h"`
  - 调用 `can_motor_set_speed(0, 3000, MOTOR_DIR_CW);`
- 通过标准：
  - 抓包能看到对应帧，字段与代码一致
- 通过后清理：restore

---

### Step T13：Wiegand（只做刷卡读出）

- 目标：刷卡后能读出 `card_id`。
- 临时代码要点：
  - 循环调用 `wiegand_get_card_id(&id)`，若返回 1 则打印并挂起
- 通过标准：
  - 多次刷卡能稳定读出（不要求与标签卡号一致，只要一致性+可区分）
- 通过后清理：restore

---

### Step T14：QSPI 读 ID

- 目标：打印 `qspi_flash_read_id()`，判断是否为允许 ID。
- 通过标准：
  - ID 在 `ZD25Q64/W25Q64/ZD25Q128/W25Q128` 之中
- 通过后清理：restore

---

### Step T15：QSPI 擦写读回（小数据）

- 目标：在安全地址执行“擦扇区→写 256B→读回比对”。
- 目的：先做最小写入验证，再扩大到跨页/跨扇区。
- 临时代码要点：
  - 选定测试地址（你确认不会破坏重要数据）
  - 写入 pattern（递增/固定 0xA5）
  - 读回逐字节比较，打印 PASS/FAIL
- 通过标准：逐字节一致
- 通过后清理：restore

---

### Step T16：FAL 初始化与分区查找

- 目标：`fal_init()` 成功，能找到 `fdb_kvdb` 和 `fdb_tsdb` 分区。
- 临时代码要点：
  - `#include "fal.h"`
  - `fal_init();`
  - `fal_partition_find("fdb_kvdb")` / `fal_partition_find("fdb_tsdb")` 打印 offset/len
- 通过标准：分区存在且长度合理
- 通过后清理：restore

---

### Step T17：FlashDB KVDB 最小读写回归

- 目标：初始化 KVDB，写入一段 blob，再读回一致。
- 目的：先证 KVDB 基础可用，再做 TSDB。
- 临时代码要点：
  - `#include "app_flashdb.h"`
  - 调用 `settings_init_load();`
  - 准备 32 字节数据：`cfg_save_sample()` → `cfg_load_sample()` 比对
- 通过标准：
  - 串口出现 `[FDB] KVDB init OK`
  - 读回数据一致
- 通过后清理：restore

---

### Step T18：FlashDB TSDB 初始化 + 追加一条 + 遍历确认

- 目标：`fdb_start_tasks()` 成功，`tsdb_event_append()` 返回 1，并能在 `tsdb_iter_range()` 回调里看到该事件。
- 临时代码要点：
  - 在 KVDB OK 的基础上调用 `fdb_start_tasks();`
  - `tsdb_event_append(0x1001, body, body_len);`
  - `tsdb_iter_range(from,to,cb,...)` 在 cb 里打印 event_type/body_len
- 通过标准：
  - `[FDB] TSDB init OK`
  - 迭代回调能看到刚追加的 event
- 通过后清理：restore

---

### Step T19：RTC（设置/读取 + 时间戳转换）

- 目标：`rtc_set_time`/`rtc_get_time` 一致；`rtc_get_timestamp`/`rtc_set_timestamp` 基本正确。
- 临时代码要点：
  - 设置到一个固定北京时间
  - 读回打印
  - 读 timestamp，再用 timestamp set 回去，再读回对比
- 通过标准：前后一致（允许秒级误差）
- 通过后清理：restore

---

### Step T20：CRC16(MODBUS) 已知向量

- 目标：对 `"123456789"` 计算结果为 `0x4B37`。
- 临时代码要点：
  - `#include "bsp_crc.h"`
  - 调用 `crc16_modbus()`
- 通过标准：结果 == 0x4B37
- 通过后清理：restore

---

## 4. 不需要临时代码的黑盒步骤（仍按“步骤化”推进）

### Step B01：USB CDC 回显

- 目标：PC 打开 CDC 虚拟串口，发送数据能原样回显（当前固件 `usb_app.c` 已实现）。
- 执行：用 `pyserial` 脚本随机发 256B 并断言回显一致（见 `driver_test_plan_2026-02-18.md` 的示例）
- 通过标准：脚本 PASS

### Step B02：USB Host(U盘) OTA CRC 校验

- 目标：插 U盘后能挂载，读取 `firmware.bin` 并完成 CRC 校验日志输出（当前 `usbh_user.c` 已实现校验，写 Flash 为 TODO）。
- 执行：制作 `firmware.bin`（头+数据+CRC32），插入 U盘，观察 `[OTA]` 日志
- 通过标准：出现 `[OTA] CRC校验通过` 或能准确报错（魔数/大小/CRC）

---

## 5. 执行时的“每一步最小记录”建议

每一步至少保留以下信息（建议建一个本地文件夹保存截图/日志，不强制进仓库）：

- 步骤编号（Txx/Bxx）
- 固件版本（git commit hash）
- 硬件连接说明（例如短接哪些引脚）
- 关键日志片段（含 `[CODEX]` 行）
- 若涉及波形：LA/示波器截图
- 结论：PASS/FAIL + 失败原因

---

## 6. 需要你确认的唯一问题（现在回复一句就行）

执行测试时，“临时测试任务”你更倾向用哪种方式清理？

1. **推荐**：每一步 PASS 后 `git restore samp/project/src/freertos_app.c` 清理  
2. 每一步 PASS 后再手工删除测试代码（不推荐，容易漏删）

你回复 `1` 或 `2`，我后续可以把文档里的清理动作统一成你选的方式。


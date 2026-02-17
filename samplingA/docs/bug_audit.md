# samplingB 潜在缺陷审计报告

范围
- 静态审查：固件（project/src、middlewares/bsp、BOOTLOADER）、OTA 工具（OTA/）和 Android App（androidApp/、bluetooth/）。
- 未进行运行测试或硬件验证。

发现问题

## 高
1. Sample ID 缓冲区少 1 字节，导致截断与潜在冲突。
   证据：`middlewares/bsp/sample_id.h`、`middlewares/bsp/sample_id.c`、`middlewares/bsp/sampling.h`。
   影响：`snprintf` 会截断序列号最后一位，导致样本 ID 重复、日志配对错误。
   建议方向：将所有 `sample_id` 缓冲区扩展为 19 字节，并统一修改相关长度校验。

2. ISR 更新的计数器与标志未声明为 volatile。
   证据：`project/src/freertos_app.c`、`project/src/at32f403a_407_int.c`。
   影响：任务读取到旧值，导致超时或循环判断失效；标志可能被优化掉。
   建议方向：为这些全局变量加 `volatile`，或在关键读取处使用临界区/原子访问。

3. OTA 校验算法在不同工具间不一致。
   证据：MCU 使用累加和 `middlewares/bsp/ota.c`；OTAServer 使用 CRC16-MODBUS `OTA/ota_server.py`；Android 使用 CRC16-MODBUS `androidApp/app/src/main/java/com/example/sampling/data/OtaHelper.kt`；BlueOTA 使用累加和 `bluetooth/blueota/app/src/main/java/com/blue/ota/MainActivity.kt`。
   影响：握手可能通过，但最终 CRC 校验失败，升级中断。
   建议方向：统一 OTA 校验算法并文档化，所有客户端一致。

4. UART6 DMA 缓冲区未做字符串终止且被直接 `strstr`，同时存在 DMA 写入与任务读取并发。
   证据：`project/src/at32f403a_407_int.c`（ISR 中直接 `strstr((char*)UART6_Buf, ...)`）、`project/src/freertos_app.c`（任务直接读 `UART6_Buf`）。
   影响：可能越界读取或误识别命令；DMA 重新使能后缓冲被覆盖，任务解析到混合数据。
   建议方向：在 ISR 处对 `UART6_Buf` 进行长度截断并补 `\0`，或复制到独立缓冲后再解析。

5. SPI NOR 容量识别/分区长度不一致，TSDB 存在越界擦写风险。
   证据：`middlewares/bsp/fal/fal_cfg.h`（fdb_tsdb 固定 8MB）、`middlewares/bsp/fal/fal_flash_AT32_port.c`（未知 ID 默认返回 8MB 且提示“2MB”；仅更新设备长度）、`middlewares/bsp/fal/fal_partition.c`（仅校验 offset，不校验 offset+len）、`middlewares/bsp/flashDB/app_flashdb.c`（tsdb_format_full 按分区长度整片擦除）。
   影响：当实际 SPI Flash 小于 8MB 或 ID 识别失败时，TSDB 初始化/格式化可能越界擦写，导致数据损坏或擦到其他区域。
   建议方向：初始化时同步修正分区长度或做 offset+len 校验；修正默认容量；格式化/擦除时使用实际 flash 长度上限。

6. UART2 DMA 接收长度可能超过 `UartMessage` 缓冲区，导致栈溢出/内存破坏。
   证据：`project/inc/freertos_app.h`（`UartMessage.data[100]`、`UART2_Buf[160]`）、`project/src/at32f403a_407_int.c`（USART2 ISR 直接 `memcpy(message.data, UART2_Buf, message.len)`）。
   影响：中断栈或队列消息被覆盖，出现随机崩溃或协议解析异常。
   建议方向：限制 `message.len` 不超过 `sizeof(message.data)`，或扩大消息结构体并同步调整队列大小。

7. 启动阶段采样间隔计算对 `sample_count == 1` 未保护，存在除零。
   证据：`middlewares/bsp/sampling_time.c`（`tp_compute_startup_interval` 中 `interval = idle_time / (sample_count - 1)`）。
   影响：配置为“一个周期仅一次采样”时可能触发除零，导致 HardFault 或调度失败。
   建议方向：`sample_count <= 1` 时直接返回或设置 interval=0，并走单次采样逻辑。

## 中
8. PacketHandler 校验逻辑与打包逻辑不一致，且启动命令解析按十进制解析 CRC。
   证据：`OTA/core/packet_handler.py`（打包用 8-bit 累加和，校验用 16-bit；CRC 解析未指定 16 进制）。
   影响：verify 失败、解析含 A-F 的 CRC 会抛异常或解析错误。
   建议方向：校验使用 `calculate_checksum_8bit`，CRC 解析使用 `int(x, 16)`。

9. OTA 数据包解析存在非对齐内存访问风险。
   证据：`middlewares/bsp/ota.c`（以 265 字节步进，直接把 `decoded_data + offset` 强转为结构体指针）。
   影响：在禁用非对齐访问的芯片上可能 HardFault。
   建议方向：用字节读取或 `memcpy` 到对齐结构体再解析。

10. BLE OTA ACK 可能被 SharedFlow 丢失。
    证据：`androidApp/app/src/main/java/com/example/sampling/data/BluetoothLeManager.kt`、`androidApp/app/src/main/java/com/example/sampling/viewmodel/BluetoothViewModel.kt`。
    影响：ACK 在收集开始前到达会被丢弃，触发误重传或失败。
    建议方向：设置 replay/缓冲，或改用 Channel/队列；先启动收集再发送。

11. 客户端未等待最终 CRC 结果 ACK（65535）。
    证据：MCU 发送 `ACK_65535_*` 在 `middlewares/bsp/ota.c`；Android/BlueOTA 在最后一个包就判成功。
    影响：UI 显示成功，但固件验证失败。
    建议方向：等待 `ACK_65535_OK` 再确认成功。

12. 配置结构体跨任务读写未同步。
    证据：写入在 `middlewares/bsp/screen.c`，读取在 `middlewares/bsp/ota.c`。
    影响：读到撕裂的字符串/字段，导致 MQTT Payload/OTA 命令异常。
    建议方向：用互斥锁保护，或在使用前复制快照。

13. UART6 任务通知值与命令码复用，可能误触发 OTA/锁机等命令。
    证据：`project/src/at32f403a_407_int.c`（默认用 `len` 作为 notify 值）、`project/src/freertos_app.c`（`notifyValue == 0x66/0xAA/0xAB/0xAC` 判断命令）。
    影响：当报文长度恰好等于这些值时会误触发对应命令。
    建议方向：使用独立标志位或结构体传参，避免长度与命令码冲突。

14. OTA 启动命令解析未校验 CRC/Size，解析失败仍返回成功。
    证据：`middlewares/bsp/ota.c` 的 `OTA_CheckStartCommand`。
    影响：`total_size`/`file_checksum` 可能保持 0 或旧值，导致擦写/校验异常。
    建议方向：解析失败应直接返回 0，并记录错误原因。

15. Base64 解码超出缓冲时静默截断且不报错。
    证据：`middlewares/bsp/ota.c` 的 `OTA_Base64DecodeLen`。
    影响：当载荷超过 2048 字节时会静默丢数据，导致 OTA 数据损坏且难以定位。
    建议方向：检测超出缓冲的情况并返回错误；或改为分片解码。

16. OTAServer 握手使用原始固件大小，但数据包固定 256 字节长度。
    证据：`OTA/ota_server.py`（握手用 `firmware_size`，打包 `data_len` 固定 `packet_size` 并填充）。
    影响：MCU 的 `received_size` 与握手 `total_size` 不一致，最终校验失败。
    建议方向：握手 size 与实际发送字节数保持一致（选择原始或填充后大小）。

17. 采样间隔可被运行期设置为 0，多个模块存在除零风险。
    证据：`middlewares/bsp/screen.c`（`SampleInterval` 直接赋值，无校验）；除法使用见 `middlewares/bsp/sampling_time.c`、`middlewares/bsp/sampling.c`、`middlewares/bsp/freemodbus/mb_reg_dayue.c` 等。
    影响：运行期崩溃或调度计算异常。
    建议方向：在入口处校验范围（>=1），或在使用前加保护。

18. Modbus 动态门禁密码使用 `calendar.day` 字段，但 RTC 结构体实际为 `date`。
    证据：`middlewares/bsp/freemodbus/mb_reg_dayue.c`、`middlewares/bsp/freemodbus/mb_reg_dahu.c`、`middlewares/bsp/freemodbus/mb_reg_sichuan.c`；结构体定义见 `project/inc/rtc.h`。
    影响：相关协议模块可能编译失败，或读取到未定义字段导致动态密码错误。
    建议方向：统一使用 `calendar.date`，并确保 `rtc_time_get()` 已刷新。

19. 大湖 Modbus 写寄存器直接改配置但无范围校验且未持久化。
    证据：`middlewares/bsp/freemodbus/mb_reg_dahu.c` 中 `dahu_holding_cb` 对 40384/40385/40386/40408 等寄存器直接赋值，未调用 `cfg_save_*`。
    影响：可写入 0 或超范围值触发除零/溢出；重启后配置丢失，导致行为不一致。
    建议方向：增加与屏幕/Dayue 协议一致的范围校验，并写入 KVDB。

20. 时间等比调度的采样槽容量固定 96，配置频率高时会静默丢采样。
    证据：`middlewares/bsp/sampling_time.h`（`bucket_a_slots[96]`/`bucket_b_slots[96]`）、`middlewares/bsp/sampling_time.c`（`if (*count_ptr >= capacity) continue;`）。
    影响：当采样间隔较小且日内周期较多时，超过 96 的采样任务被丢弃，导致实际采样次数不足。
    建议方向：扩大容量或按配置限制最小间隔；至少记录告警/错误。

21. 启动阶段采样间隔计算存在无符号下溢。
    证据：`middlewares/bsp/sampling_time.c` 的 `tp_compute_startup_interval` 中 `max_start_time = delivery_time_sec - buffer_before_delivery - time_per_sample - (sample_count - 1) * interval` 等无符号计算。
    影响：当距离首送样时间不足以覆盖缓冲/采样耗时时，`max_start_time` 可能下溢为极大值，导致送样后仍安排采样。
    建议方向：计算前校验 `seconds_to_delivery` 与所需最短时间；不足时直接降级策略或返回失败。

22. 大湖协议满水位判断使用 16 位乘积，配置过大时溢出。
    证据：`middlewares/bsp/freemodbus/mb_reg_dahu.c`（`full_volume = sample_count * SampleVolume`，`full_volume` 为 `uint16_t`，`sample_count` 未限幅）。
    影响：CycleTime 或 SampleVolume 较大时溢出，导致满水位状态误报。
    建议方向：使用 `uint32_t` 并做上限裁剪/校验。

23. 定时采样时间点计算使用 `uint8_t` 截断且仅处理 1 小时进位。
    证据：`middlewares/bsp/Timetrigger.h`（`sample_offsets[24]` 为 `uint16_t`）、`middlewares/bsp/Timetrigger.c`（`uint8_t sample_min = sample_offsets[i]` 且 `sample_min >= 60` 只 `hour+1`）。
    影响：当间隔较大或周期较长时，采样时间点被截断/偏移，触发错时或漏采。
    建议方向：使用 `uint16_t` 计算分钟数，并用 `hour += sample_min / 60; sample_min %= 60` 处理多小时进位。

24. 开关量触发窗口使用“日内秒”与 RTC epoch 秒混用，且窗口起点可下溢，触发标志无原子保护。
    证据：`middlewares/bsp/Switchtrigger.c`（`now_sec = rtc_counter_get()` 与 `cycle_start_sec_total = cycle_start_hour * 3600` 对比；`st_calculate_window` 中 `sample_time - 60`；ISR/任务对 `g_st_notify_flags` 读写）。
    影响：窗口判断基准不一致导致误触发/漏触发；午夜下溢导致窗口异常；触发标志可能丢失。
    建议方向：统一时间基准、对下溢做保护，并用临界区/原子操作保护 ISR 标志。

25. 通信触发请求只有单槽 pending，多个请求可能被覆盖丢失。
    证据：`middlewares/bsp/Commtrigger.c`（`g_comm_trigger_request` 单结构体）、`middlewares/bsp/freemodbus/mb_reg_sichuan.c`（`sichuan_request_comm` 直接覆盖并置 pending）。
    影响：上位机连续下发采样/送样指令时，早到请求被后到覆盖，导致动作缺失。
    建议方向：使用队列/环形缓冲保存请求，或在 pending 时返回忙/错误码。

26. Modbus CRC 校验对短帧未做长度保护，可能越界访问。
    证据：`middlewares/bsp/work.c`（`vCrc16Check` 使用 `length-2` 且读取 `data[length-1]`/`data[length-2]`，`Modbus()` 未先校验长度）。
    影响：收到长度过短的噪声帧时发生越界读，导致异常或崩溃。
    建议方向：在 CRC 校验前强制 `length >= 4`（或至少 >=2）并丢弃异常帧。

27. 串口屏消息分发未校验 `msg.len` 就访问固定下标。
    证据：`middlewares/bsp/screen.c`（`screen_message_dispatcher` 多处分支直接读取 `msg.data[6..8]`）、`project/inc/freertos_app.h`（`UartMessage` 携带 `len`）。
    影响：短帧/干扰帧可能导致越界读，出现误解析或异常崩溃。
    建议方向：在每个分支前校验 `msg.len` 满足最低长度，或统一做最小长度过滤。

28. KVDB 锁获取超时后仍继续访问数据库，潜在并发破坏。
    证据：`middlewares/bsp/flashDB/app_flashdb.c`（`kvdb_lock` 仅设置 `s_kvdb_lock_timeout_flag`，但 `cfg_save_sample`/`cfg_save_comm`/`cfg_load_*` 等未检查仍调用 `kv_set_blob/kv_get_blob`）。
    影响：锁竞争时可能出现并发读写、数据损坏或状态不一致。
    建议方向：让 `kvdb_lock` 返回状态并在所有调用点失败即退出，或改为阻塞/重试策略。

29. Bootloader 跳转只校验复位向量，未验证 MSP 是否在 SRAM 范围内。
    证据：`BOOTLOADER/bsp/bsp_ota.c`（`app_load` 仅检查 `app_addr + 4` 是否为 0x08xxxxxx）。
    影响：镜像损坏但复位向量看似有效时，可能设置非法 MSP 并 HardFault，导致无法启动。
    建议方向：校验首字（MSP）是否落在 SRAM 合法区间并满足对齐要求。

30. Wiegand 超时检查在中断中执行且包含大量 `printf`。
    证据：`project/src/at32f403a_407_int.c`（`TMR3_GLOBAL_IRQHandler` 调用 `wiegand_timeout_check`）、`middlewares/bsp/wiegand.c`（`wiegand_timeout_check` 内多次 `printf`）。
    影响：中断阻塞或优先级反转，导致系统时基抖动、串口拥塞或看门狗误复位。
    建议方向：ISR 只置标志/缓存数据，日志输出放到任务上下文。

31. 门锁按键事件在中断回调中执行 `printf`。
    证据：`project/src/at32f403a_407_int.c`（`TMR5_GLOBAL_IRQHandler` 调用 `button_ticks`）、`middlewares/bsp/bsp_button.c`（`BTN03_PRESS_DOWN_Handler/BTN03_PRESS_UP_Handler` 内 `printf`）。
    影响：中断执行时间过长，影响实时性并可能触发死锁或丢中断。
    建议方向：ISR 回调仅记录事件，日志放到任务里输出。

## 低
32. Bootloader 更新失败路径未 `flash_lock`。
    证据：`BOOTLOADER/bsp/bsp_flash.c`（`app_flash_update` 内比较失败直接 `return ERROR`，未执行 `flash_lock()`）。
    影响：更新失败后 Flash 处于解锁状态，存在误写风险。
    建议方向：在所有错误返回路径中确保 `flash_lock()`。

33. Wiegand 数据读取未做临界区保护，存在 64 位数据撕裂风险。
    证据：`middlewares/bsp/wiegand.c`（`g_wiegand_data.raw_data` 为 64 位，`wiegand_get_card_id`/`wiegand_parse_26bit` 直接读取）。
    影响：极端情况下可能读取到不一致的卡号或校验失败。
    建议方向：读取前复制快照并使用临界区/关中断保护。

34. Android BLE OTA 固定等待 5 秒再发送数据。
    证据：`androidApp/app/src/main/java/com/example/sampling/viewmodel/BluetoothViewModel.kt`。
    影响：Flash 擦除时间更长时，数据包可能过早发送导致丢包。
    建议方向：等待设备就绪 ACK（如 `ACK_0_OK`），或延长等待并重试。

35. 西安协议单寄存器写入直接返回成功但未执行任何处理。
    证据：`middlewares/bsp/freemodbus/mb_reg_xian.c`（`nregs == 1` 分支仅打印日志并返回 0）。
    影响：上位机认为写入成功，但设备无实际动作，易导致状态不一致或重试逻辑失效。
    建议方向：返回 `MB_EX_ILLEGAL_FUNCTION/ADDRESS` 或实现对应功能。

36. MB_MODE_BROADCAST 模式下仍对广播地址 0 应答。
    证据：`middlewares/bsp/freemodbus/mb.c`（广播模式忽略站号并用请求地址应答）、`middlewares/bsp/freemodbus/mb_reg_xian.c`（使用 MB_MODE_BROADCAST）。
    影响：总线有多台设备时可能发生应答冲突，违反 Modbus RTU 广播不应答规范。
    建议方向：广播模式下仍应忽略地址 0 的响应，或在协议层明确单机部署限制。

## 本轮追加（按模块）
37. 【高】【Flowtrigger】采样槽数组固定 24，但 `sample_count = CycleTime / SampleInterval` 未限幅，可能写越界并触发位掩码移位溢出。
    证据：`middlewares/bsp/Flowtrigger.h`（`sample_offsets[24]`）、`middlewares/bsp/Flowtrigger.c`（`ft_compute_sample_offsets` 直接循环到 `sample_count`，`ft_check_cycle_delivery_trigger` 使用 `(1u << sample_count)`）。
    影响：内存破坏、调度状态异常，导致漏采/误采或随机崩溃。
    建议方向：对 `sample_count` 做上限裁剪（<=24 且 <=31），或改用动态/更大数组并校验移位范围。

38. 【中】【Flowtrigger】流量开始/停止通知标志读改写无原子保护，可能丢事件或误清除。
    证据：`middlewares/bsp/Flowtrigger.c`（`flow_trigger_notify_start/stop` 中 `g_ft_notify_flags |= ...`，`handle_flow_trigger_notifications` 中 `g_ft_notify_flags &= ~...`）。
    影响：开始/停止通知被覆盖或丢失，触发链路错乱。
    建议方向：使用临界区/原子位操作，或改为队列/EventGroup 通知。

39. 【中】【Flowtrigger】周期起点计算使用 16 位总量，体积/频率偏大时溢出导致耗时估算偏小。
    证据：`middlewares/bsp/Flowtrigger.c`（`calculate_cycle_start_hour` 中 `uint16_t total_volume = single_volume * (cycle_time / interval)`）。
    影响：周期起点过早，可能与送样/采样冲突或触发时间错乱。
    建议方向：使用 32 位计算并限制参数范围，必要时记录告警。

40. 【中】【ScreenCache/TSDB】TSDB 环形缓存的 flush 未加锁，和 append 并发会破坏 head/tail。
    证据：`middlewares/bsp/screen_cache.c`（`tsdb_cache_append` 使用互斥锁，`tsdb_cache_flush_all` 未加锁直接读写 `s_tsdb_head/tail`）。
    影响：事件丢失、重复或顺序错乱。
    建议方向：flush 与 append 共用互斥锁，或在 flush 期间禁止写入。

41. 【中】【ScreenCache/KVDB】KVDB dirty 标志无同步，flush 期间新修改可能被清零丢失。
    证据：`middlewares/bsp/screen_cache.c`（`kvdb_cache_mark_dirty`/`kvdb_cache_flush_all` 直接读写 `s_kvdb_dirty`）。
    影响：配置改动未持久化，重启后回退。
    建议方向：用互斥锁/原子位图保护 dirty 标志。

42. 【中】【LBS】定位响应解析依赖 `strstr/strchr/atoi/atof`，未保证 UART 缓冲以 `\0` 结尾或长度受限。
    证据：`middlewares/bsp/lbs_location.c`（`LBS_CheckResponse`/`LBS_ParseResponse_Internal`）。
    影响：越界读取或误解析，定位失败或异常崩溃。
    建议方向：引入长度参数并做限长解析，或在 DMA 收包后补 `\0` 并校验长度。

43. 【低】【Flash】Flash 写入失败路径未重新加锁。
    证据：`middlewares/bsp/flash.c`（`flash_2kb_write`/`flash_write` 中多处 `return ERROR` 未调用 `flash_lock()`）。
    影响：Flash 处于解锁状态，增加误写风险。
    建议方向：所有错误返回路径统一 `flash_lock()`。

44. 【低】【Modbus/Dayue】浮点值以指针重解释读取，存在严格别名优化风险。
    证据：`middlewares/bsp/freemodbus/mb_reg_dayue.c`（`dayue_build_factor_data_block` 中 `uint32_t fval = *(uint32_t*)&g_FactorDataFromHost[i].factorValue`）。
    影响：特定编译优化下可能读取到错误的因子值。
    建议方向：使用 `memcpy` 或联合体 + `memcpy` 方式转换。

45. 【高】【采样/Modbus】`sample_id` 作为 C 字符串使用但长度不足，触发 `strcpy/strlen/printf("%s")` 越界。
    证据：`middlewares/bsp/sampling.h`（`sample_id[18]`）、`middlewares/bsp/sampling.c`（`strcpy(start_record.sample_id, sample_id)`、`strcpy(start_record.sample_id, water_ctx->sample_id)`）、`middlewares/bsp/freemodbus/mb_reg_sichuan.c`（`strlen(ctx->sample_id)`、`printf("...%s", ctx->sample_id)`）。
    影响：缓冲区溢出或越界读取，导致崩溃、日志污染或数据泄露。
    建议方向：统一扩展为 19 字节并确保 `\0`；或改为固定长度 `memcpy` 并避免 `strlen/%s`。

46. 【中】【Modbus/Dayue/RecordCache】最近门禁时间戳读取逻辑依赖未维护的 `window_end_idx`，缓存未满时返回错误记录。
    证据：`middlewares/bsp/freemodbus/mb_reg_dayue.c`（`dayue_get_last_door_timestamp` 使用 `g_cache_mgr.door.window_end_idx`）、`middlewares/bsp/record_cache.c`（`window_end_idx` 只初始化为 0，未更新）。
    影响：上位机看到的“最近门禁时间”错误，超时判断/告警误判。
    建议方向：改为基于 `count-1` 取最新记录并加锁，或维护 `window_end_idx`。

47. 【中】【FlashDB/ScreenCache】TSDB 缓存 flush 对写入失败不做处理，直接丢弃事件。
    证据：`middlewares/bsp/screen_cache.c`（`tsdb_cache_flush_all` 调用 `tsdb_event_append` 后无条件推进 `s_tsdb_head`）。
    影响：TSDB 未就绪或写入失败时，缓存事件静默丢失。
    建议方向：仅在写入成功时出队；失败时保留重试或上报告警。

48. 【中】【FlashDB/TSDB】写入前置时间阈值过严（2026 年），RTC 未校准时全部记录被拒绝。
    证据：`middlewares/bsp/flashDB/app_flashdb.c`（`TSDB_MIN_VALID_TIMESTAMP` 检查）。
    影响：断电复位或首次上电未校时期间，TSDB 记录全丢；配合缓存 flush 会放大丢失。
    建议方向：允许“未校时”标记记录或缓存等待 RTC 校准后再写入。

49. 【中】【RecordCache/TSDB】TSDB 读取回调直接把 `body` 强转为结构体，存在非对齐访问风险。
    证据：`middlewares/bsp/record_cache.c`（`_load_sampling_cb/_load_delivery_cb/_load_retain_cb` 使用 `*(const SamplingCompleteRecord *)body` 等）。
    影响：在开启非对齐访问陷阱的芯片上可能 HardFault 或数据异常。
    建议方向：用 `memcpy` 拷贝到对齐结构体再解析。

50. 【中】【Switchtrigger/Timetrigger】采样次数被固定上限 24，超出配置会静默丢采样。
    证据：`middlewares/bsp/Switchtrigger.c`、`middlewares/bsp/Timetrigger.c`（`sample_count > 24` 直接裁剪）。
    影响：周期较长且间隔较小的配置下，实际采样次数不足，数据缺失。
    建议方向：扩大数组容量或限制配置并给出告警。

51. 【中】【SampleId】样本 ID 生成依赖 `calendar`，但未在生成函数内刷新 RTC，可能使用过期时间导致 ID 重复或与真实时间不一致。
    证据：`middlewares/bsp/sample_id.c`（`generate_sample_id` 直接使用 `calendar` 字段，未调用 `rtc_time_get`）、`middlewares/bsp/sampling.c`（调用 `generate_sample_id` 前未刷新 RTC，只有跳过桶状态检查分支才 `rtc_time_get`）、`middlewares/bsp/retain_judge.c`（同样未刷新）。
    影响：在未及时刷新 `calendar` 的情况下，跨秒生成的 ID 可能时间戳不变、序号频繁归零，导致重复/错序。
    建议方向：在 `generate_sample_id` 内部刷新 RTC 或改为直接用 `rtc_counter_get` 推导时间字段；保证生成前强制同步。

52. 【低】【SampleId】`sample_id_generator_init` 非幂等，重复调用会重建互斥锁并重置序号，存在资源泄漏与 ID 重复风险。
    证据：`middlewares/bsp/sample_id.c`（每次都 `xSemaphoreCreateMutex`）、`middlewares/bsp/sampling.c` 与 `project/src/freertos_app.c` 中多处调用初始化。
    影响：系统启动序列或重启流程多次进入时，序号重置导致 ID 冲突；堆上互斥锁对象泄漏。
    建议方向：初始化前检测 `g_id_gen.mutex` 是否已创建，避免重复创建；或集中唯一初始化入口。

53. 【中】【Commtrigger】通信触发的 AB 自动切换仅在“进入新周期”时切换一次，未考虑跨多个周期/跨天导致的多次翻转。
    证据：`middlewares/bsp/Commtrigger.c`（`current_cycle != g_comm_scheduler_state.cycle_count` 时仅 `active_bucket = 1 - old_bucket`）。
    影响：长时间无请求后再次触发时，桶选择与实际周期奇偶不匹配，导致采样/送样用错桶。
    建议方向：根据 `current_cycle` 与初始周期的差值计算桶位（如 `current_cycle % 2`），或按差值翻转多次。

54. 【中】【OTA/AT命令】AT 响应解析使用 `strstr` 但未基于实际接收长度进行截断，存在越界读取风险。
    证据：`middlewares/bsp/ota.c`（`TestATCommand` 仅 `memset` 缓冲并调用 `strstr`，未用 `len` 设 `Buf[len] = '\0'`；`timesyc` 等函数依赖该缓冲解析）。
    影响：当响应长度接近缓冲上限或未以 `\0` 结尾时，`strstr` 可能越界读取，导致误判或异常。
    建议方向：在收到长度后强制 `Buf[min(len, buf_size-1)] = '\0'`，或改用带长度的匹配/解析函数。

55. 【低】【SPI Flash】底层读写接口未校验 `addr + length` 是否越界，错误参数可能擦写/读取到非法区域。
    证据：`middlewares/bsp/spi_flash.c`（`spiflash_write`/`spiflash_read` 未检查边界）、`middlewares/bsp/spi_flash.h`（定义 `SPIF_CHIP_SIZE` 但未使用）。
    影响：上层传入异常地址或长度时可能破坏其他分区或触发硬件异常。
    建议方向：在驱动层统一做边界校验，超界直接返回错误。

56. 【中】【留样/瓶位传感器】`bottle_sensor_is_working` 以 `s_is_initialized` 驱动“最近触发时间”，无法检测传感器停止脉冲的失效场景。
    证据：`middlewares/bsp/work.c`（`bottle_sensor_is_working` 中只要 `s_is_initialized` 为真就刷新 `last_sensor_time`，而该标志一旦置位不会因无脉冲而变化）。
    影响：传感器在初始化后失效时仍被判定为正常，故障检测失真。
    建议方向：在中断/传感器触发处更新 `last_sensor_time`，检测逻辑基于真实触发而非初始化标志。

57. 【低】【留样流程】瞬时留样采用递归处理多瓶，瓶数较大时存在堆栈占用增长风险。
    证据：`middlewares/bsp/work.c`（`instant_retention_execute` 在 `bottle_count > 1` 时递归调用自身）。
    影响：在嵌入式小栈配置下可能导致栈溢出或任务异常。
    建议方向：改为循环迭代处理，避免递归。

58. 【中】【OTA/调试日志】调试缓冲写入无锁，清理/发送时加锁，存在数据竞争与缓冲破坏风险。
    证据：`project/src/wk_system.c`（`PUTCHAR_PROTOTYPE` 直接写 `debug_buffer/debug_buffer_index`，未加锁）、`middlewares/bsp/ota.c`（`ProcessDebugCache`/`CheckDebugCommand` 使用 `debug_mutex` 清理与 `memmove`）。
    影响：调试日志可能被截断/错乱；并发 `memmove` 可能导致索引异常或覆盖。
    建议方向：统一写入与读取的同步策略（互斥锁或无锁环形缓冲），必要时将 `debug_buffer_index` 设为 `volatile` 并避免在 ISR 中写入。

59. 【高】【留样/送样校准】送样时间计算的临时数组溢出，可能造成栈内存破坏。
    证据：`middlewares/bsp/work.c`（`calc_delivery_time_by_volume` 中 `usable_t[2]/usable_v[2]`，但循环 `i < 3` 且对满足条件的点逐一写入，`usable_count` 可增长到 3）。
    影响：当 3 个校准点都满足 `v[i] >= 100` 时会越界写，导致随机崩溃或时间计算错误。
    建议方向：限制 `usable_count` 上限为 2 或扩容数组并明确选择策略。

60. 【低】【门禁事件】门禁事件的持续时间字段始终为 0，TSDB 记录缺失时长信息。
    证据：`middlewares/bsp/bsp_button.c`（`BTN03_PRESS_DOWN/UP` 将 `duration` 固定为 0，`door_event_process` 直接写入 `DoorEvent_t.duration`）、`middlewares/bsp/flashDB/app_flashdb.h`（`DoorEvent_t.duration` 定义为事件持续时长）。
    影响：后端/日志无法基于 TSDB 还原门禁开关持续时间，影响统计与告警判断。
    建议方向：在事件生成或落库前填充真实时长（可复用 `g_door_stats` 的统计）。

61. 【低】【时间等比调度】采样进度统计字段未维护，进度查询恒为 0。
    证据：`middlewares/bsp/sampling_time.c`（`tp_scheduler_get_sample_progress` 读取 `g_tp_scheduler.sample_count/sample_done_mask`；这些字段在该模块中仅初始化为 0，未见更新）。
    影响：若界面或上位机调用进度接口，将一直显示 0% 或异常。
    建议方向：在采样完成时更新 `sample_done_mask` 与 `sample_count`，或移除该进度接口。

## 修复优先级与影响评估表

评估口径
- 优先级定义：P0=崩溃/越界/不可用；P1=核心功能失败或数据丢失；P2=功能偏差或低概率异常；P3=统计展示或可绕过风险。
- 复杂度定义：低=局部校验或小改；中=跨模块调整或协议对齐；高=存储布局或调度体系调整。
- 回归风险定义：低=局部影响；中=多任务或数据链路；高=升级/存储/全局配置。
- 评估说明：基于静态审查与现有报告，实际优先级可结合现场配置与故障频率再细化。

| 编号 | 问题简述 | 严重性 | 优先级 | 影响范围 | 修复复杂度 | 回归风险 | 建议验证 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Sample ID 缓冲区少 1 字节 | 高 | P0 | 采样/记录/协议 | 中 | 高 | ID长度边界与历史数据兼容 |
| 2 | ISR 计数器未 volatile | 高 | P1 | 中断/调度 | 低 | 低 | 高中断频率与超时逻辑 |
| 3 | OTA 校验算法不一致 | 高 | P1 | OTA/客户端 | 高 | 高 | 全链路升级与 CRC |
| 4 | UART6 DMA 未终止/并发 | 高 | P0 | 串口/命令 | 中 | 中 | 非 NUL 帧与并发接收 |
| 5 | SPI NOR 容量/分区不一致 | 高 | P0 | FlashDB/存储 | 高 | 高 | 不同容量与格式化 |
| 6 | UART2 DMA 拷贝越界 | 高 | P0 | 串口/消息队列 | 低 | 中 | 长帧/异常帧 |
| 7 | 启动采样间隔除零 | 高 | P1 | 采样调度 | 低 | 低 | sample_count=1 配置 |
| 8 | OTA 工具校验/CRC 解析不一致 | 中 | P2 | OTA 工具 | 低 | 低 | 工具端单测与样例包 |
| 9 | OTA 包非对齐访问 | 中 | P1 | OTA/固件 | 中 | 中 | 未对齐访问场景 |
| 10 | BLE OTA ACK 丢失 | 中 | P2 | Android BLE OTA | 中 | 中 | ACK 抢跑/重连 |
| 11 | 客户端未等最终 CRC ACK | 中 | P2 | OTA 客户端 | 低 | 低 | 末包与 CRC ACK |
| 12 | 配置结构体跨任务无同步 | 中 | P2 | 配置/MQTT/OTA | 中 | 中 | 并发读写压力 |
| 13 | UART6 通知值与长度冲突 | 中 | P2 | 串口命令 | 低 | 低 | 长度=0x66/0xAA 等 |
| 14 | OTA 启动命令未校验 | 中 | P1 | OTA 启动 | 低 | 中 | 错误 CRC/长度 |
| 15 | Base64 解码超长截断 | 中 | P1 | OTA 数据 | 中 | 中 | 大包与边界长度 |
| 16 | OTAServer 握手 size 不一致 | 中 | P1 | OTA 服务器 | 中 | 中 | 实际发送字节数 |
| 17 | 采样间隔可为 0 | 中 | P1 | 采样/调度 | 低 | 中 | UI/Modbus 边界输入 |
| 18 | 动态门禁密码字段错误 | 中 | P2 | Modbus/门禁 | 低 | 低 | RTC 字段与密码 |
| 19 | 大湖写寄存器无校验/不持久化 | 中 | P1 | Modbus/配置 | 中 | 中 | 范围边界与重启 |
| 20 | 时间等比调度槽容量不足 | 中 | P2 | 采样调度 | 中 | 中 | 高采样频率 |
| 21 | 启动间隔计算下溢 | 中 | P1 | 启动调度 | 中 | 中 | 距离送样不足场景 |
| 22 | 大湖满水位 16 位溢出 | 中 | P2 | Modbus/水位 | 低 | 低 | 大样本量配置 |
| 23 | 定时采样时间截断 | 中 | P2 | Timetrigger | 中 | 中 | 大间隔与跨小时 |
| 24 | Switchtrigger 时间基准混用/标志非原子 | 中 | P1 | Switchtrigger | 中 | 高 | 跨天与高频触发 |
| 25 | 通信触发单槽覆盖 | 中 | P2 | Commtrigger | 中 | 中 | 连续下发指令 |
| 26 | Modbus CRC 短帧越界读 | 中 | P1 | Modbus 解析 | 低 | 低 | 短帧/噪声帧 |
| 27 | 屏幕消息长度未校验 | 中 | P1 | 屏幕协议 | 低 | 中 | 短帧/乱序帧 |
| 28 | KVDB 锁超时仍访问 | 中 | P1 | FlashDB/KV | 中 | 中 | 并发读写与断电 |
| 29 | Bootloader 未校验 MSP | 中 | P1 | Bootloader | 低 | 低 | 损坏镜像启动 |
| 30 | Wiegand 超时检查在 ISR 打印 | 中 | P2 | ISR/实时性 | 低 | 低 | 高中断负载 |
| 31 | 门锁按键 ISR 打印 | 中 | P2 | 门禁/ISR | 低 | 低 | 按键抖动/高频 |
| 32 | Bootloader 失败路径未锁 Flash | 低 | P3 | Bootloader/Flash | 低 | 低 | 更新失败路径 |
| 33 | Wiegand 读 64 位未加锁 | 低 | P3 | Wiegand | 低 | 低 | 读写并发 |
| 34 | Android BLE OTA 固定 5 秒等待 | 低 | P3 | Android BLE OTA | 低 | 低 | 擦除耗时较长 |
| 35 | 西安单寄存器写空实现 | 低 | P3 | Modbus/西安 | 低 | 低 | 单寄存器写入 |
| 36 | 广播地址仍应答 | 低 | P3 | Modbus/总线 | 低 | 低 | 多机广播 |
| 37 | Flowtrigger 采样槽越界/移位溢出 | 高 | P0 | Flowtrigger/采样 | 中 | 高 | 高采样频率与移位边界 |
| 38 | Flowtrigger 通知标志无原子 | 中 | P2 | Flowtrigger | 中 | 中 | 并发触发 |
| 39 | Flowtrigger 周期起点 16 位溢出 | 中 | P2 | Flowtrigger | 低 | 低 | 大体积/小间隔 |
| 40 | TSDB 缓存 flush 无锁 | 中 | P1 | ScreenCache/TSDB | 中 | 中 | flush 与 append 并发 |
| 41 | KVDB dirty 标志无同步 | 中 | P2 | ScreenCache/KV | 低 | 低 | 并发配置修改 |
| 42 | LBS 解析无 NUL/长度 | 中 | P1 | LBS/串口 | 中 | 低 | 长响应/无终止 |
| 43 | Flash 写失败未锁 | 低 | P3 | Flash | 低 | 低 | 写失败路径 |
| 44 | Dayue float strict aliasing | 低 | P3 | Modbus/Dayue | 低 | 低 | 高优化编译 |
| 45 | sample_id 字符串越界 | 高 | P0 | 采样/记录/Modbus | 中 | 高 | ID 长度与字符串 API |
| 46 | 门禁最新记录索引错误 | 中 | P2 | Modbus/门禁缓存 | 低 | 低 | 缓存未满/满 |
| 47 | TSDB 缓存写失败仍出队 | 中 | P2 | ScreenCache/TSDB | 低 | 低 | TSDB 未就绪 |
| 48 | TSDB 最小时间阈值过严 | 中 | P2 | TSDB/时间 | 中 | 中 | 未校时启动 |
| 49 | TSDB 回调非对齐访问 | 中 | P1 | RecordCache/TSDB | 低 | 低 | 非对齐访问禁用 |
| 50 | Switch/Time 触发采样次数上限 24 | 中 | P2 | Switch/Time 调度 | 中 | 中 | 高频配置 |
| 51 | SampleId 未刷新 RTC | 中 | P1 | SampleId/记录 | 低 | 中 | 跨秒生成 ID |
| 52 | SampleId 初始化非幂等 | 低 | P3 | SampleId | 低 | 低 | 多次初始化 |
| 53 | Commtrigger 桶切换仅翻一次 | 中 | P2 | 通信触发/调度 | 中 | 中 | 长时间无请求 |
| 54 | AT 响应解析未按长度 | 中 | P1 | OTA/AT 解析 | 低 | 低 | 长响应/无终止 |
| 55 | SPI Flash 无边界校验 | 低 | P3 | SPI 驱动 | 低 | 低 | 越界参数 |
| 56 | 瓶位传感器失效检测失真 | 中 | P2 | 留样/传感器 | 中 | 低 | 传感器断开 |
| 57 | 瞬时留样递归 | 低 | P3 | 留样流程 | 低 | 低 | 多瓶参数 |
| 58 | 调试缓冲并发无锁 | 中 | P2 | OTA 调试/日志 | 中 | 低 | 高频日志与发送 |
| 59 | 送样校准数组越界 | 高 | P0 | 送样时间计算 | 低 | 中 | 三校准点有效 |
| 60 | 门禁事件时长未填 | 低 | P3 | 门禁/TSDB 统计 | 低 | 低 | 时长统计一致性 |
| 61 | 采样进度字段未维护 | 低 | P3 | 调度/UI | 低 | 低 | 进度显示 |

## 分阶段修复计划（P0/P1/P2/P3）

### P0（立即修复：崩溃/越界/不可用）
- 覆盖条目：#1、#4、#5、#6、#37、#45、#59。
- 目标：消除越界/内存破坏与存储越界写入，确保系统稳定与数据安全。
- 关键依赖/注意点：
  - #1/#45 影响 `sample_id` 长度与历史记录格式，需要评估存量数据兼容与上位机解析。
  - #5 涉及 Flash 分区/容量识别，需确认实际芯片容量与分区表一致性。
  - #37 采样槽与位掩码溢出，需与采样调度逻辑同步检查。
  - #59 栈越界问题需优先处理，影响送样时间计算正确性。
- 建议验证：
  - 串口长帧/异常帧回归（#4/#6）。
  - 采样/送样完整流程与 ID 唯一性（#1/#45/#59）。
  - TSDB 格式化与大数据写入边界（#5/#37）。

### P1（高优先：核心流程正确性/数据完整）
- 覆盖条目：#2、#3、#7、#9、#14、#15、#16、#17、#21、#24、#26、#27、#28、#29、#40、#42、#49、#51、#54。
- 目标：保证 OTA/调度/串口解析/存储一致性，避免核心功能失效或数据丢失。
- 关键依赖/注意点：
  - #3/#14/#15/#16/#54 需要 OTA 端到端一致（MCU/服务器/APP/蓝牙工具）。
  - #17/#21/#24 调度相关改动要与屏幕/Modbus 配置入口对齐。
  - #28/#40/#49 涉及并发与存储一致性，需统一锁策略。
- 建议验证：
  - OTA 全链路升级（含 CRC 校验与最后 ACK）。
  - 采样调度边界配置（interval=0/1、跨天、短周期）。
  - 串口/屏幕短帧与噪声帧鲁棒性。
  - TSDB 读写一致性与断电恢复。

### P2（中优先：功能偏差/低概率异常）
- 覆盖条目：#8、#10、#11、#12、#13、#18、#20、#22、#23、#25、#30、#31、#38、#39、#41、#46、#47、#48、#50、#53、#56、#58。
- 目标：降低低概率异常与并发/统计偏差，提升系统一致性与可维护性。
- 建议验证：
  - 并发触发与事件标志一致性（#12/#25/#38/#41/#58）。
  - 调度容量与采样覆盖率（#20/#23/#50/#53）。
  - 门禁/电源/TSDB 缓存行为与回读（#46/#47/#48）。

### P3（低优先：统计展示/规范性）
- 覆盖条目：#32、#33、#34、#35、#36、#43、#44、#52、#55、#57、#60、#61。
- 目标：完善健壮性与可观测性，降低潜在隐患。
- 建议验证：
  - 故障路径与边界场景（Flash 锁、广播应答、递归深度等）。
  - 门禁统计与界面显示一致性（#60/#61）。

## 按模块拆分修复包（独立交付）

### 修复包 A：Modbus
- 覆盖条目：#18、#19、#22、#26、#35、#36、#44、#46。
- 优先级分布：P1=#19/#26；P2=#18/#22/#46；P3=#35/#36/#44。
- 关键依赖/接口影响：
  - #46 依赖 RecordCache/TSDB 最新索引维护（FlashDB 包需配合）。
  - 动态密码字段修正需与 RTC 刷新时机一致（可能受采样调度时钟策略影响）。
- 建议验证：
  - Modbus RTU 短帧/噪声帧 CRC 处理。
  - 大湖/大岳/西安寄存器写入与持久化一致性。
  - 广播地址不应答行为（多机总线场景）。

### 修复包 B：采样调度（含采样 ID/触发/留样流程）
- 覆盖条目：#1、#7、#17、#20、#21、#23、#24、#25、#37、#38、#39、#45、#50、#51、#52、#53、#56、#57、#59、#61。
- 优先级分布：P0=#1/#37/#45/#59；P1=#7/#17/#21/#24/#51；P2=#20/#23/#25/#38/#39/#50/#53/#56；P3=#52/#57/#61。
- 关键依赖/接口影响：
  - #1/#45 影响样本 ID 长度，需同步 Modbus 西安、TSDB 记录与上位机解析。
  - #37/#50/#20 与采样槽容量相关，可能影响屏幕与配置上限策略。
  - #25/#53 涉及通信触发策略，需与 Modbus/上位机调用节奏确认。
- 建议验证：
  - 全部触发模式（Time/Switch/Flow/Comm）在高频与跨天配置下的稳定性。
  - 采样/送样/留样完整流程与 ID 唯一性。
  - 配置边界（interval=0/1、sample_count=1、超高采样频率）。

### 修复包 C：FlashDB（含 Flash 驱动边界）
- 覆盖条目：#5、#28、#40、#41、#46、#47、#48、#49、#55、#43。
- 优先级分布：P0=#5；P1=#28/#40/#49；P2=#41/#46/#47/#48；P3=#43/#55。
- 关键依赖/接口影响：
  - #5 需确认实际 SPI Flash 容量并与分区表一致；可能影响 OTA/TSDB 格式化。
  - #46 与 Modbus 包联动（最新门禁时间戳读取）。
- 建议验证：
  - TSDB 初始化/格式化在不同容量芯片上行为一致。
  - KVDB 并发写与锁超时路径。
  - 断电/复位后 TSDB 读写一致性与时间阈值策略。

### 修复包 D：OTA（含客户端与调试链路）
- 覆盖条目：#3、#8、#9、#10、#11、#14、#15、#16、#34、#54、#58。
- 优先级分布：P1=#3/#9/#14/#15/#16/#54；P2=#8/#10/#11/#58；P3=#34。
- 关键依赖/接口影响：
  - OTA 与 UART6 接收链路紧耦合，建议同时参考 #4（串口 DMA 解析安全）。
  - 校验算法统一后需同步服务器/Android/蓝牙工具与 MCU。
- 建议验证：
  - OTA 全链路升级（含最后 CRC ACK）。
  - 大包/异常包/重连与 ACK 丢失场景。
  - AT 响应长报文与无 NUL 终止解析。

## 修复包任务清单（可派工）

### A. Modbus 修复包任务清单
- MB-01【P2】动态门禁密码字段修正 + RTC 刷新时机统一。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_dayue.c`、`middlewares/bsp/freemodbus/mb_reg_dahu.c`、`middlewares/bsp/freemodbus/mb_reg_sichuan.c`、`project/inc/rtc.h`
  - 产出：使用 `calendar.date`；读取前确保 `rtc_time_get()` 更新。
  - 验证：读取动态密码寄存器，跨分钟/跨日验证正确性。
- MB-02【P1】大湖协议写寄存器范围校验与持久化。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_dahu.c`、`middlewares/bsp/flashDB/app_flashdb.c`（cfg_save_*）
  - 产出：对 CycleTime/SampleInterval/SampleVolume 等做范围校验；写入 KVDB。
  - 验证：越界值拒绝/裁剪；重启后配置保持。
- MB-03【P2】大湖满水位计算溢出修复。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_dahu.c`
  - 产出：将乘积改为 32 位并做上限裁剪。
  - 验证：大样本量配置下满水位判断正确。
- MB-04【P1】Modbus CRC 短帧长度保护。
  - 涉及：`middlewares/bsp/work.c`
  - 产出：CRC 校验前强制 `length >= 4`，短帧直接丢弃。
  - 验证：短帧噪声不再导致越界读或异常。
- MB-05【P3】西安单寄存器写处理策略明确。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_xian.c`
  - 产出：返回非法地址/功能或补齐写入逻辑。
  - 验证：上位机写入单寄存器反馈符合协议预期。
- MB-06【P3】广播地址应答策略调整。
  - 涉及：`middlewares/bsp/freemodbus/mb.c`
  - 产出：广播地址 0 不应答；若单机部署需明确限制。
  - 验证：多机总线广播无冲突应答。
- MB-07【P3】Dayue 浮点读取严格别名修复。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_dayue.c`
  - 产出：用 `memcpy`/union 安全转换浮点。
  - 验证：因子值读取在高优化编译下仍稳定。
- MB-08【P2】最新门禁时间戳读取修正（与 FlashDB 包协同）。
  - 涉及：`middlewares/bsp/freemodbus/mb_reg_dayue.c`、`middlewares/bsp/record_cache.c`
  - 产出：改用 `count-1` 或维护 `window_end_idx`，并加锁读取。
  - 验证：缓存未满/已满场景下“最近门禁时间”正确。

### B. 采样调度修复包任务清单
- SCH-01【P0】Sample ID 长度扩展与字符串使用修正。
  - 涉及：`middlewares/bsp/sample_id.h`、`middlewares/bsp/sampling.h`、`middlewares/bsp/sampling.c`、`middlewares/bsp/retain_judge.c`、`middlewares/bsp/freemodbus/mb_reg_sichuan.c`
  - 产出：统一缓冲区为 19 字节并确保 `\0`；替换 `strcpy/strlen/%s` 的越界风险。
  - 验证：ID 唯一性、日志/Modbus 输出正确；与历史记录兼容策略确认。
- SCH-02【P1】Sample ID 生成前 RTC 同步 + 初始化幂等化。
  - 涉及：`middlewares/bsp/sample_id.c`、`middlewares/bsp/sampling.c`、`middlewares/bsp/retain_judge.c`
  - 产出：`generate_sample_id` 内部刷新 RTC；避免重复创建互斥锁。
  - 验证：跨秒生成、重复初始化场景。
- SCH-03【P1】采样间隔与样本数边界（除零/非法值）统一防护。
  - 涉及：`middlewares/bsp/sampling_time.c`、`middlewares/bsp/sampling.c`、`middlewares/bsp/screen.c`、相关 Modbus 写入入口
  - 产出：入口与使用处统一校验 `SampleInterval >= 1`、`sample_count >= 1`。
  - 验证：配置为 0/1、sample_count=1 的调度稳定性。
- SCH-04【P0】Flowtrigger 采样槽/位掩码溢出修复。
  - 涉及：`middlewares/bsp/Flowtrigger.h`、`middlewares/bsp/Flowtrigger.c`
  - 产出：限制 `sample_count` 上限并修正位移范围或扩容数组。
  - 验证：高采样频率、长周期配置下不越界。
- SCH-05【P2】Timetrigger 时间点计算溢出修复。
  - 涉及：`middlewares/bsp/Timetrigger.c`
  - 产出：用 `uint16_t` 计算分钟并正确处理多小时进位。
  - 验证：大间隔/跨小时配置采样时间正确。
- SCH-06【P2】Switchtrigger 时间基准统一 + 窗口下溢保护 + 通知标志原子化。
  - 涉及：`middlewares/bsp/Switchtrigger.c`
  - 产出：统一使用相同时间基准；修复 `sample_time-60` 下溢；标志位用临界区/原子操作。
  - 验证：跨天与高频触发场景。
- SCH-07【P2】时间等比调度容量与采样次数上限策略调整。
  - 涉及：`middlewares/bsp/sampling_time.h`、`middlewares/bsp/sampling_time.c`、`middlewares/bsp/Switchtrigger.c`、`middlewares/bsp/Timetrigger.c`
  - 产出：扩容采样槽或限制配置；超限给出告警。
  - 验证：高频配置下采样次数一致。
- SCH-08【P2】通信触发请求队列化与 AB 桶切换修正。
  - 涉及：`middlewares/bsp/Commtrigger.c`
  - 产出：单槽 pending 改为队列或 busy 反馈；桶切换按周期差值计算。
  - 验证：密集下发请求与跨周期场景。
- SCH-09【P2】瓶位传感器健康检测修正。
  - 涉及：`middlewares/bsp/work.c`、`middlewares/bsp/bsp_button.c`
  - 产出：用真实传感器触发更新时间戳，避免初始化标志误判。
  - 验证：传感器断开/无脉冲场景。
- SCH-10【P3】瞬时留样递归改迭代。
  - 涉及：`middlewares/bsp/work.c`
  - 产出：循环处理多瓶，避免递归堆栈风险。
  - 验证：多瓶留样场景。
- SCH-11【P0】送样校准数组越界修复。
  - 涉及：`middlewares/bsp/work.c`
  - 产出：限制 `usable_count` 上限或扩容数组并定义选点策略。
  - 验证：三校准点均有效时稳定。
- SCH-12【P3】采样进度统计字段维护。
  - 涉及：`middlewares/bsp/sampling_time.c`
  - 产出：在采样完成点更新 `sample_done_mask/sample_count` 或移除进度接口。
  - 验证：进度显示与实际采样次数一致。

### C. FlashDB 修复包任务清单
- FDB-01【P0】SPI Flash 容量识别与分区长度一致性修复。
  - 涉及：`middlewares/bsp/fal/fal_cfg.h`、`middlewares/bsp/fal/fal_flash_AT32_port.c`、`middlewares/bsp/fal/fal_partition.c`
  - 产出：默认容量与提示一致；校验 `offset+len`；分区长度与设备容量同步。
  - 验证：不同容量芯片初始化/格式化边界。
- FDB-02【P3】SPI Flash 驱动读写边界校验。
  - 涉及：`middlewares/bsp/spi_flash.c`、`middlewares/bsp/spi_flash.h`
  - 产出：统一检查 `addr + length <= SPIF_CHIP_SIZE`。
  - 验证：越界参数返回错误且不写入。
- FDB-03【P1】KVDB 锁超时路径处理。
  - 涉及：`middlewares/bsp/flashDB/app_flashdb.c`
  - 产出：锁超时直接返回错误；调用处尊重返回值。
  - 验证：并发写入时不破坏 KVDB。
- FDB-04【P1】TSDB 缓存 flush 并发保护。
  - 涉及：`middlewares/bsp/screen_cache.c`
  - 产出：flush 与 append 共用互斥锁。
  - 验证：并发 flush/append 无丢失/乱序。
- FDB-05【P2】TSDB flush 写失败处理。
  - 涉及：`middlewares/bsp/screen_cache.c`
  - 产出：写失败不出队，支持重试或告警。
  - 验证：TSDB 未就绪时事件不丢失。
- FDB-06【P2】TSDB 最小时间阈值策略优化。
  - 涉及：`middlewares/bsp/flashDB/app_flashdb.c`
  - 产出：允许未校时记录或缓存等待校时。
  - 验证：首次上电未校时日志保留。
- FDB-07【P1】TSDB 回调结构体非对齐访问修复。
  - 涉及：`middlewares/bsp/record_cache.c`
  - 产出：`memcpy` 到对齐结构体再解析。
  - 验证：开启非对齐访问陷阱仍稳定。
- FDB-08【P2】门禁最新记录索引维护（与 Modbus 协同）。
  - 涉及：`middlewares/bsp/record_cache.c`
  - 产出：维护 `window_end_idx` 或提供可靠“最新记录”接口。
  - 验证：缓存未满与已满时最新记录正确。
- FDB-09【P3】Flash 写失败路径加锁恢复。
  - 涉及：`middlewares/bsp/flash.c`
  - 产出：所有错误返回路径 `flash_lock()`。
  - 验证：写失败后 Flash 保持锁定。

### D. OTA 修复包任务清单
- OTA-01【P1】OTA 校验算法统一（MCU/Server/Android/蓝牙工具）。
  - 涉及：`middlewares/bsp/ota.c`、`OTA/ota_server.py`、`androidApp/.../OtaHelper.kt`、`bluetooth/.../MainActivity.kt`
  - 产出：明确 CRC/校验算法与包格式，文档化。
  - 验证：全链路升级通过且 CRC 一致。
- OTA-02【P1】OTA 启动命令校验与失败处理。
  - 涉及：`middlewares/bsp/ota.c`
  - 产出：解析失败立即返回错误；清零旧值。
  - 验证：错误 CRC/长度不触发写入。
- OTA-03【P1】Base64 解码超长处理。
  - 涉及：`middlewares/bsp/ota.c`
  - 产出：超过缓冲直接报错或分片解码。
  - 验证：超长 payload 不会静默截断。
- OTA-04【P1】OTA 包解析非对齐访问修复。
  - 涉及：`middlewares/bsp/ota.c`
  - 产出：用 `memcpy` 到对齐结构体再解析。
  - 验证：未对齐平台稳定。
- OTA-05【P1】AT 响应解析按长度截断。
  - 涉及：`middlewares/bsp/ota.c`
  - 产出：`Buf[len]='\0'` 或使用长度安全解析。
  - 验证：长响应/无终止不越界。
- OTA-06【P2】Android BLE OTA ACK 可靠性与最终 CRC ACK 等待。
  - 涉及：`androidApp/.../BluetoothLeManager.kt`、`androidApp/.../BluetoothViewModel.kt`
  - 产出：ACK 缓冲/replay；等待 `ACK_65535_OK` 再成功。
  - 验证：ACK 先到/晚到均成功。
- OTA-07【P1】OTAServer 握手 size 与实际发送一致。
  - 涉及：`OTA/ota_server.py`
  - 产出：统一使用原始或填充后大小并与 MCU 对齐。
  - 验证：`received_size == total_size`。
- OTA-08【P3】Android 固定 5s 延时改为就绪 ACK。
  - 涉及：`androidApp/.../BluetoothViewModel.kt`
  - 产出：等待设备就绪 ACK 或重试机制。
  - 验证：Flash 擦除超时场景稳定。
- OTA-09【P2】调试日志缓冲并发同步。
  - 涉及：`project/src/wk_system.c`、`middlewares/bsp/ota.c`
  - 产出：统一锁/无锁环形缓冲，避免并发写与 `memmove` 冲突。
  - 验证：高频日志与发送并发不丢失/错乱。

## 修复包详细技术方案（实施指南）

### 修复包 A：Modbus
实施说明：优先修复短帧保护与配置写入校验，确保协议与设备状态一致。

| 任务ID | 修改文件 | 修改方法 | 修改完成后测试 |
| --- | --- | --- | --- |
| MB-01 | `middlewares/bsp/freemodbus/mb_reg_dayue.c`<br>`middlewares/bsp/freemodbus/mb_reg_dahu.c`<br>`middlewares/bsp/freemodbus/mb_reg_sichuan.c` | 将动态密码计算中 `calendar.day` 替换为 `calendar.date`；计算前调用 `rtc_time_get()` 刷新 `calendar`。 | 连续读取动态密码寄存器，跨分钟/跨日变化正确。 |
| MB-02 | `middlewares/bsp/freemodbus/mb_reg_dahu.c` | 对写寄存器（如 40384/40385/40386/40408 等）增加范围校验（对齐屏幕侧规则）；非法值返回异常码；合法值写入后调用 `cfg_save_*` 持久化。 | 写入越界值被拒；合法值重启后保持。 |
| MB-03 | `middlewares/bsp/freemodbus/mb_reg_dahu.c` | `full_volume = sample_count * SampleVolume` 改为 32 位计算；比较与显示时做上限裁剪。 | 大样本量配置下满水位判断不误报。 |
| MB-04 | `middlewares/bsp/work.c` | CRC 校验前增加 `length >= 4` 判断；短帧直接丢弃（建议在 `Modbus()` 入口做）。 | 注入短帧噪声，不再越界读或异常。 |
| MB-05 | `middlewares/bsp/freemodbus/mb_reg_xian.c` | `nregs==1` 分支返回异常（`MB_EX_ILLEGAL_FUNCTION/ADDRESS`）或补齐写入逻辑，避免“写成功但不生效”。 | 单寄存器写入与上位机反馈一致。 |
| MB-06 | `middlewares/bsp/freemodbus/mb.c` | 广播地址 0 不应答；若单机部署需在协议文档说明。 | 多机总线广播无冲突应答。 |
| MB-07 | `middlewares/bsp/freemodbus/mb_reg_dayue.c` | 替换 `*(uint32_t*)&float` 为 `memcpy` 或 union，避免严格别名风险。 | 高优化编译下因子值读取正常。 |
| MB-08 | `middlewares/bsp/record_cache.c`<br>`middlewares/bsp/freemodbus/mb_reg_dayue.c` | 方案 A：在 `cache_add_door` 更新 `window_end_idx`；方案 B：新增 `cache_get_latest_door_timestamp()` 并在 Modbus 读时加锁调用。推荐 B。 | 缓存未满/已满时“最近门禁时间”正确。 |

### 修复包 B：采样调度（含 Sample ID/触发/留样）
实施说明：先处理 P0 越界与 ID 兼容，再处理调度精度与触发可靠性。

| 任务ID | 修改文件 | 修改方法 | 修改完成后测试 |
| --- | --- | --- | --- |
| SCH-01 | `middlewares/bsp/sample_id.h`<br>`middlewares/bsp/sampling.h`<br>`middlewares/bsp/sampling.c`<br>`middlewares/bsp/retain_judge.c`<br>`middlewares/bsp/freemodbus/mb_reg_sichuan.c` | 统一 `sample_id` 缓冲为 19 字节（18+`\0`）；引入 `SAMPLE_ID_LEN=18` 与 `SAMPLE_ID_BUF_LEN=19`；所有打印改为 `%.18s`；避免 `strcpy/strlen/%s` 直接使用未终止缓冲。 | Sample ID 不截断不越界；日志/Modbus 输出正确；与历史记录兼容策略明确。 |
| SCH-02 | `middlewares/bsp/sample_id.c`<br>`middlewares/bsp/sampling.c`<br>`middlewares/bsp/retain_judge.c` | `generate_sample_id()` 内部 `rtc_time_get()`；`sample_id_generator_init()` 判断互斥锁是否已创建，避免重复创建和序号重置。 | 跨秒生成不重复；多次初始化无副作用。 |
| SCH-03 | `middlewares/bsp/screen.c`<br>`middlewares/bsp/sampling_time.c`<br>`middlewares/bsp/sampling.c`<br>相关 Modbus 写入入口 | 入口统一校验 `SampleInterval >= 1`；`sample_count <= 1` 时单次采样路径直接返回；避免除零。 | interval=0/1、sample_count=1 时调度稳定。 |
| SCH-04 | `middlewares/bsp/Flowtrigger.c`<br>`middlewares/bsp/Flowtrigger.h` | `sample_count` 上限裁剪（<=24 且 <=31）；移位前保证范围；超限告警。 | 高频配置不越界/不溢出。 |
| SCH-05 | `middlewares/bsp/Timetrigger.c` | 使用 `uint16_t` 处理 `sample_offsets`；分钟进位采用 `hour += sample_min / 60`；`hour %= 24`。 | 大间隔/跨小时采样时间正确。 |
| SCH-06 | `middlewares/bsp/Switchtrigger.c` | 统一时间基准（建议 `rtc_seconds_since_2000()`）；`sample_time-60` 下溢保护；`g_st_notify_flags` 用临界区/原子位操作。 | 跨天窗口判断正确；高频触发不丢标志。 |
| SCH-07 | `middlewares/bsp/sampling_time.h`<br>`middlewares/bsp/sampling_time.c`<br>`middlewares/bsp/Switchtrigger.c`<br>`middlewares/bsp/Timetrigger.c` | 采样槽容量与采样次数上限对齐（扩容或限制配置）；超限记录告警。 | 高频配置采样不静默丢失。 |
| SCH-08 | `middlewares/bsp/Commtrigger.c` | `g_comm_trigger_request` 改为队列（FreeRTOS Queue）；满队列返回忙；AB 桶切换按周期差值计算（避免只翻一次）。 | 连续下发请求不丢；跨周期桶选择正确。 |
| SCH-09 | `middlewares/bsp/work.c`<br>`middlewares/bsp/bsp_button.c` | 在传感器 ISR 回调更新 `last_sensor_time`；`bottle_sensor_is_working` 仅依据真实触发判断。 | 传感器断开/无脉冲可被识别。 |
| SCH-10 | `middlewares/bsp/work.c` | 将 `instant_retention_execute` 递归改为循环；确保每瓶记录与状态更新不丢。 | 多瓶留样无栈溢出。 |
| SCH-11 | `middlewares/bsp/work.c` | `calc_delivery_time_by_volume` 中 `usable_t/usable_v` 扩容为 3 或限制 `usable_count < 2`；定义明确的选点策略。 | 三个校准点都有效时不越界。 |
| SCH-12 | `middlewares/bsp/sampling_time.c` | 在调度建立时设置 `g_tp_scheduler.sample_count`；采样完成时更新 `sample_done_mask`；或移除进度接口。 | 进度显示与实际采样次数一致。 |

### 修复包 C：FlashDB（含 Flash 驱动边界）
实施说明：P0 先确保分区与容量一致，避免越界擦写。

| 任务ID | 修改文件 | 修改方法 | 修改完成后测试 |
| --- | --- | --- | --- |
| FDB-01 | `middlewares/bsp/fal/fal_cfg.h`<br>`middlewares/bsp/fal/fal_flash_AT32_port.c`<br>`middlewares/bsp/fal/fal_partition.c` | 未识别芯片时容量返回值与提示一致；分区注册时校验 `offset+len <= flash->len`；必要时修正分区长度。 | 2/4/8MB 芯片初始化/格式化不越界。 |
| FDB-02 | `middlewares/bsp/spi_flash.c`<br>`middlewares/bsp/spi_flash.h` | 读/写/擦除入口统一检查 `addr + length <= SPIF_CHIP_SIZE`；超界直接返回错误。 | 越界参数不写入、不擦除。 |
| FDB-03 | `middlewares/bsp/flashDB/app_flashdb.c` | `kvdb_lock` 超时后返回失败；调用处检测并跳过读写或重试。 | 并发访问时 KVDB 不损坏。 |
| FDB-04 | `middlewares/bsp/screen_cache.c` | `tsdb_cache_flush_all` 与 `tsdb_cache_append` 使用同一互斥锁。 | flush/append 并发不丢事件。 |
| FDB-05 | `middlewares/bsp/screen_cache.c` | `tsdb_event_append` 失败时不推进 `s_tsdb_head`，保留事件重试。 | TSDB 未就绪时事件不丢失。 |
| FDB-06 | `middlewares/bsp/flashDB/app_flashdb.c` | 放宽 `TSDB_MIN_VALID_TIMESTAMP` 或允许“未校时缓存”。 | 未校时开机期间记录可保留。 |
| FDB-07 | `middlewares/bsp/record_cache.c` | `_load_*_cb` 改为 `memcpy` 到对齐结构体后再解析。 | 关闭非对齐访问仍稳定。 |
| FDB-08 | `middlewares/bsp/record_cache.c` | 维护 `window_end_idx` 或新增“获取最新记录”接口；上层统一加锁读取。 | “最新记录”读取正确。 |
| FDB-09 | `middlewares/bsp/flash.c` | 所有错误返回路径补 `flash_lock()`。 | 写失败后 Flash 仍锁定。 |

### 修复包 D：OTA（含客户端与调试链路）
实施说明：先统一校验与握手尺寸，再修复解析安全与客户端可靠性。

| 任务ID | 修改文件 | 修改方法 | 修改完成后测试 |
| --- | --- | --- | --- |
| OTA-01 | `middlewares/bsp/ota.c`<br>`OTA/ota_server.py`<br>`androidApp/.../OtaHelper.kt`<br>`bluetooth/.../MainActivity.kt` | 统一 OTA 校验算法（建议 CRC16-MODBUS）；协议字段与文档一致。 | MCU/Server/Android/蓝牙工具全链路升级成功。 |
| OTA-02 | `middlewares/bsp/ota.c` | 启动命令解析失败直接返回错误并清零旧值。 | 错误 CRC/长度不触发写入。 |
| OTA-03 | `middlewares/bsp/ota.c` | Base64 解码超出缓冲直接报错或分片处理。 | 超长 payload 不静默截断。 |
| OTA-04 | `middlewares/bsp/ota.c` | OTA 包解析使用 `memcpy` 到对齐结构体后再解析。 | 未对齐平台稳定。 |
| OTA-05 | `middlewares/bsp/ota.c` | AT 响应解析按长度截断（`Buf[min(len, buf_size-1)] = '\0'` 或长度安全匹配）。 | 长响应不越界读取。 |
| OTA-06 | `androidApp/.../BluetoothLeManager.kt`<br>`androidApp/.../BluetoothViewModel.kt` | ACK 使用带 replay 的 Flow/Channel；等待 `ACK_65535_OK` 才完成。 | ACK 抢跑/晚到均成功。 |
| OTA-07 | `OTA/ota_server.py` | `total_size` 与实际发送字节一致（原始/填充二选一并与 MCU 对齐）。 | `received_size == total_size`。 |
| OTA-08 | `androidApp/.../BluetoothViewModel.kt` | 固定 5 秒延时改为等待就绪 ACK 或重试机制。 | 擦除耗时长也稳定。 |
| OTA-09 | `project/src/wk_system.c`<br>`middlewares/bsp/ota.c` | 调试缓冲写入/读取统一同步策略（锁或无锁环形缓冲）；避免 `memmove` 并发冲突。 | 高频日志发送不丢失/错乱。 |

## 通用验收建议
- 每个修复包至少覆盖“正常流程 + 边界条件 + 异常注入”三类验证。
- 涉及存储格式或协议字段变更的修复必须提供兼容策略说明。
- 涉及并发与锁策略调整的修复，需记录死锁风险与超时处理策略。

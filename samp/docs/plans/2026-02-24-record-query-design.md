# 串口屏记录查询功能设计

## 目标

为 samp 工程实现串口屏记录查询功能，支持5种记录类型（采样/送样/留样/电源/门禁）的分页查询和翻页浏览。

## 架构

```
串口屏命令 → app_screen.c (查询/翻页分发)
                ↓
          app_record_query.c (缓存管理 + TSDB分页查询 + 帧构造)
                ↓
          app_flashdb.c (fdb_tsl_iter_reverse 反向遍历)
                ↓
          QSPI Flash (W25Q64, quad mode)
```

核心思路：利用 FlashDB 原生的 `fdb_tsl_iter_reverse` 反向遍历能力，从最新记录开始查询。用户查看最近记录时几乎零延迟，越往后翻越慢但在可接受范围内。对高频查询的采样记录使用20页缓存窗口，送样/留样使用5页缓存窗口，电源/门禁直接查询不缓存。

## 差异化缓存策略

| 记录类型 | 缓存页数 | 缓存条数 | RAM占用 | 说明 |
|---------|---------|---------|--------|------|
| 采样记录 | 20页 | 140条 | ~700B | 高频采样，每小时4次 |
| 送样记录 | 5页 | 35条 | ~175B | 频率较低 |
| 留样记录 | 5页 | 35条 | ~210B | 频率较低 |
| 电源记录 | 0 | 0 | 0 | 直查TSDB |
| 门禁记录 | 0 | 0 | 0 | 直查TSDB |

总RAM约 ~1.1KB + session管理开销。

## 分页查询算法

使用 `fdb_tsl_iter_reverse` 反向遍历 + skip/take 模式：

1. 反向遍历（最新→最旧），只匹配目标 event_type
2. 跳过前 `page * per_page` 条匹配记录（skip）
3. 收集接下来的 `per_page` 条（take）
4. 收集够了回调返回 `true` 终止遍历

性能估算（35000条采样记录，一年数据量）：
- 第0页（最新）：读7条即停止，<1ms
- 第10页：跳70条+读7条，<2ms
- 第100页：跳700条+读7条，~5ms
- 第5000页（最旧）：跳35000条，~220ms

缓存命中时直接返回，无需TSDB查询。

## 缓存窗口工作流程

1. 首次进入查询：反向遍历加载缓存窗口（采样20页/送样留样5页），同时计数总记录数
2. 翻页命中缓存：直接从缓存返回数据
3. 翻页超出缓存：重新加载以目标页为中心的缓存窗口
4. 电源/门禁：每次翻页直接 fdb_tsl_iter_reverse + skip/take

## Event Type 定义

| 事件 | event_type | body结构 | body大小 |
|------|-----------|---------|---------|
| 采样完成 | 0x0040 | SampleLogData | 5B |
| 送样完成 | 0x0042 | DeliveryLogData | 5B |
| 留样完成 | 0x0044 | RetainSampleLogData | 6B |
| 断电检测 | 0x00F2 | 无 | 0B |
| 恢复上电 | 0x00F3 | 无 | 0B |
| 开门 | 0x0070 | 无 | 0B |
| 关门 | 0x0071 | 无 | 0B |

## 记录写入时机

| 写入位置 | 事件 | 调用 |
|---------|------|------|
| app_scheduler.c | 采样完成 | tsdb_event_append(0x0040, &log, 5) |
| freertos_app.c Task04 | 送样完成 | tsdb_event_append(0x0042, &log, 5) |
| app_retain_judge.c | 留样完成 | tsdb_event_append(0x0044, &log, 6) |
| 断电检测回调 | 断电 | tsdb_event_append(0x00F2, NULL, 0) |
| 上电初始化 | 恢复 | tsdb_event_append(0x00F3, NULL, 0) |
| 门禁IO中断 | 开/关门 | tsdb_event_append(0x0070/0x0071, NULL, 0) |

## 串口屏协议（沿用 samplingB）

### 帧格式

写入地址：0x5182，帧头 5A A5

每条记录字节数：
- 采样/送样：12字节 = mode(2) + bucket(2) + volume(2) + datetime(6)
- 留样：14字节 = mode(2) + bottle(2) + volume(2) + datetime(6) + result(2)
- 电源/门禁：8字节 = type(2) + datetime(6)

每页7条记录。

### 命令协议

查询初始化（进入查询页面）：
- addr 高字节=0x00（confirm类），value = (0x81 << 8) | query_type
- query_type: 0x71=采样, 0x72=送样, 0x73=留样, 0x74=电源, 0x75=门禁

翻页：
- value = (query_type << 8) | direction
- direction: 0x01=上一页, 0x02=下一页

### 在 app_screen.c 中的分发

在 screen_handle_confirm() 的 switch(action) 中新增：
- case 0x81: 根据 param 调用 record_query_init(type)
- case 0x71~0x75: 调用 record_query_page_nav(type, param)

## 文件变更清单

### 新增文件
- `middlewares/bsp/app_record_query.h` — 记录查询模块头文件
- `middlewares/bsp/app_record_query.c` — 记录查询模块实现

### 修改文件
- `middlewares/bsp/flashDB/app_flashdb.h` — 新增 event_type 常量
- `middlewares/bsp/app_screen.h` — 新增查询相关 action 定义
- `middlewares/bsp/app_screen.c` — confirm 处理中新增查询/翻页分发
- `middlewares/bsp/app_scheduler.c` — 采样完成时写入记录
- `middlewares/bsp/app_retain_judge.c` — 留样完成时写入记录
- `project/src/freertos_app.c` — 送样完成时写入记录
- `project/MDK_V5/samp.uvprojx` — 添加 app_record_query.c

## 约束

- AT32F435 MCU, FreeRTOS, Keil V5 (ARMCC V5, C89/C90)
- 编译必须 0 Error 0 Warning
- 变量声明必须在块开头（C89）
- 中文注释，UTF-8编码

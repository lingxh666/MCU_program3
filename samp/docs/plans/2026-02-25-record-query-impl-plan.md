# 串口屏记录查询功能 — 实施计划

> 对应设计文档: `samp/docs/plans/2026-02-24-record-query-design.md`
> bd任务: MCU_program3-x42

## 约束提醒

- ARMCC V5 / C89: 变量声明必须在块开头
- 编译 0 Error 0 Warning
- samp 屏幕发送接口: `screen_write_var(addr, data, len)` (非 samplingB 的 screen_send_notify)
- samp 无 `rtc_seconds_to_datetime`，需自行实现 `encode_datetime`
- `fdb_tsl_iter_reverse(db, cb, cb_arg)` 已存在于 flashdb.h:63

---

## Batch 1: 基础设施 — 类型定义 + TSDB扩展 + 模块骨架

### Task 1.1: app_flashdb.h 添加 event_type 常量 + tsdb_iter_reverse 声明

**文件**: `samp/middlewares/bsp/flashDB/app_flashdb.h`

**修改内容**:
1. 在 `RetainBottleState` 之后添加 event_type 常量定义:
```c
/* ---- Event Type 常量 ---- */
#define EVT_SAMPLE_DONE     0x0040
#define EVT_DELIVERY_DONE   0x0042
#define EVT_RETAIN_DONE     0x0044
#define EVT_POWER_OFF       0x00F2
#define EVT_POWER_ON        0x00F3
#define EVT_DOOR_OPEN       0x0070
#define EVT_DOOR_CLOSE      0x0071
```

2. 在 `tsdb_iter_range` 声明之后添加反向遍历包装:
```c
void tsdb_iter_reverse_all(tsdb_event_iter_cb cb, void *user);
```

**验收**: 编译通过

### Task 1.2: app_flashdb.c 实现 tsdb_iter_reverse_all

**文件**: `samp/middlewares/bsp/flashDB/app_flashdb.c`

**修改内容**: 在 `tsdb_iter_range` 函数之后添加:
```c
/* 反向遍历回调适配 - fdb_tsl_iter_reverse 使用 fdb_tsl_cb 签名 */
void tsdb_iter_reverse_all(tsdb_event_iter_cb cb, void *user)
{
    if (!g_tsdb_ready || !cb) return;

    iter_ctx_t ctx = { .user_cb = cb, .user_arg = user };

    if (g_tsdb_mutex)
        xSemaphoreTake(g_tsdb_mutex, pdMS_TO_TICKS(10000));

    fdb_tsl_iter_reverse(g_tsdb, tsdb_iter_cb_wrapper, &ctx);

    if (g_tsdb_mutex)
        xSemaphoreGive(g_tsdb_mutex);
}
```

注意: 复用已有的 `iter_ctx_t` 和 `tsdb_iter_cb_wrapper`。

**验收**: 编译通过

### Task 1.3: 创建 app_record_query.h

**文件**: `samp/middlewares/bsp/app_record_query.h` (新建)

**内容**:
```c
#ifndef APP_RECORD_QUERY_H
#define APP_RECORD_QUERY_H

#include <stdint.h>

/* 查询类型 */
typedef enum {
    RQ_SAMPLING = 0,
    RQ_DELIVERY = 1,
    RQ_RETAIN   = 2,
    RQ_POWER    = 3,
    RQ_DOOR     = 4,
    RQ_TYPE_COUNT
} rq_type_t;

/* 每页记录数 */
#define RQ_PAGE_SIZE  7

/* 缓存页数 */
#define RQ_CACHE_SAMPLING  20
#define RQ_CACHE_DELIVERY   5
#define RQ_CACHE_RETAIN     5
#define RQ_CACHE_POWER      0   /* 直查 */
#define RQ_CACHE_DOOR       0   /* 直查 */

/* 查询会话 */
typedef struct {
    uint8_t  current_page;
    uint16_t total_count;     /* 进入查询时的记录总数快照 */
    uint8_t  valid;
} rq_session_t;

/* 缓存条目 — 采样/送样 (12字节帧数据) */
typedef struct {
    uint16_t mode;
    uint16_t bucket_or_source;
    uint16_t volume;
    uint32_t timestamp;
} rq_sample_entry_t;

/* 缓存条目 — 留样 (14字节帧数据) */
typedef struct {
    uint16_t mode;
    uint16_t bottle;
    uint16_t volume;
    uint32_t timestamp;
    uint8_t  success;
    uint8_t  acid;
} rq_retain_entry_t;

/* 缓存条目 — 电源/门禁 (8字节帧数据) */
typedef struct {
    uint16_t event_type;
    uint32_t timestamp;
} rq_event_entry_t;

/* API */
void record_query_init(rq_type_t type);
void record_query_page_nav(rq_type_t type, uint8_t direction);

#endif /* APP_RECORD_QUERY_H */
```

**验收**: 编译通过

### Task 1.4: 创建 app_record_query.c 骨架 + 添加到 Keil 工程

**文件**: `samp/middlewares/bsp/app_record_query.c` (新建)

**内容**: 包含头文件、静态变量声明、两个空函数体:
```c
#include "app_record_query.h"
#include "app_flashdb.h"
#include "bsp_screen.h"
#include <string.h>
#include <stdio.h>

/* 查询会话 */
static rq_session_t s_session[RQ_TYPE_COUNT];

void record_query_init(rq_type_t type)
{
    (void)type;
}

void record_query_page_nav(rq_type_t type, uint8_t direction)
{
    (void)type;
    (void)direction;
}
```

**Keil工程修改**: 在 `samp.uvprojx` 中 `app_retain_judge.c` 条目之后添加 `app_record_query.c`。

**验收**: 编译通过，0 Error 0 Warning

---

## Batch 2: TSDB 分页查询核心

### Task 2.1: 实现 count + skip/take 反向遍历查询

**文件**: `samp/middlewares/bsp/app_record_query.c`

**添加内容**:

1. 迭代上下文结构体:
```c
/* 计数回调上下文 */
typedef struct {
    uint16_t event_type;
    uint16_t count;
} count_ctx_t;

/* 分页查询回调上下文 */
typedef struct {
    uint16_t event_type;
    uint16_t skip;
    uint16_t take;
    uint16_t skipped;
    uint16_t taken;
    void    *out_buf;       /* 输出缓冲区指针 */
    uint8_t  entry_size;    /* 单条目大小 */
} page_ctx_t;
```

2. 计数回调函数 `count_cb` — 遍历所有记录，匹配 event_type 计数
3. 分页回调函数 `page_cb` — skip前N条，take接下来的M条，填充到 out_buf
4. 辅助函数 `rq_count_records(event_type)` — 调用 tsdb_iter_reverse_all + count_cb
5. 辅助函数 `rq_load_page(event_type, page, buf, entry_size, out_count)` — 调用 tsdb_iter_reverse_all + page_cb

注意: 回调中需要根据 event_type 范围判断记录类型:
- 采样: EVT_SAMPLE_DONE (0x0040)
- 送样: EVT_DELIVERY_DONE (0x0042)
- 留样: EVT_RETAIN_DONE (0x0044)
- 电源: EVT_POWER_OFF (0x00F2) 或 EVT_POWER_ON (0x00F3)
- 门禁: EVT_DOOR_OPEN (0x0070) 或 EVT_DOOR_CLOSE (0x0071)

需要一个辅助函数 `event_type_matches(evt, rq_type)` 来判断事件是否属于查询类型。

**验收**: 编译通过

### Task 2.2: 实现差异化缓存窗口

**文件**: `samp/middlewares/bsp/app_record_query.c`

**添加内容**:

1. 缓存存储 — 静态数组:
```c
/* 采样缓存: 20页 × 7条 = 140条 */
static rq_sample_entry_t s_cache_sampling[RQ_CACHE_SAMPLING * RQ_PAGE_SIZE];
static uint16_t s_cache_sampling_start;  /* 缓存起始页 */
static uint16_t s_cache_sampling_count;  /* 缓存中有效条目数 */

/* 送样缓存: 5页 × 7条 = 35条 */
static rq_sample_entry_t s_cache_delivery[RQ_CACHE_DELIVERY * RQ_PAGE_SIZE];
static uint16_t s_cache_delivery_start;
static uint16_t s_cache_delivery_count;

/* 留样缓存: 5页 × 7条 = 35条 */
static rq_retain_entry_t s_cache_retain[RQ_CACHE_RETAIN * RQ_PAGE_SIZE];
static uint16_t s_cache_retain_start;
static uint16_t s_cache_retain_count;
```

2. 缓存加载函数 `cache_load_window(type, center_page)`:
   - 计算窗口范围 [center_page - half, center_page + half]
   - 调用 rq_load_page 批量加载
   - 更新 cache_start 和 cache_count

3. 缓存查询函数 `cache_get_page(type, page, out_buf, out_count)`:
   - 检查 page 是否在缓存窗口内
   - 命中: 直接从缓存复制
   - 未命中: 调用 cache_load_window 重新加载
   - 电源/门禁: 直接调用 rq_load_page (不缓存)

**验收**: 编译通过

---

## Batch 3: 屏幕显示帧构造

### Task 3.1: app_screen.h 添加查询相关 action 定义

**文件**: `samp/middlewares/bsp/app_screen.h`

**修改内容**: 在现有 `SCR_ACT_BOTTLE_RESET` 之后添加:
```c
/* 记录查询 */
#define SCR_ACT_LOG_QUERY   0x81  /* 查询初始化 */
#define SCR_ACT_LOG_SAMP    0x71  /* 采样记录翻页 */
#define SCR_ACT_LOG_DELIV   0x72  /* 送样记录翻页 */
#define SCR_ACT_LOG_RETAIN  0x73  /* 留样记录翻页 */
#define SCR_ACT_LOG_POWER   0x74  /* 电源记录翻页 */
#define SCR_ACT_LOG_DOOR    0x75  /* 门禁记录翻页 */
```

**验收**: 编译通过

### Task 3.2: 实现 encode_datetime + 5个 send_xxx_page 函数

**文件**: `samp/middlewares/bsp/app_record_query.c`

**添加内容**:

1. `encode_datetime(dst, ts)` — 将 Unix 时间戳转为 BCD 6字节 (YY MM DD HH MM SS)
   - 参考 samplingB 实现，但 samp 无 `rtc_seconds_to_datetime`
   - 需自行实现简单的秒数→日期转换（或调用已有的 RTC 接口）
   - 检查 samp 中是否有可用的 RTC 时间转换函数

2. 帧常量定义:
```c
#define SCR_LOG_ADDR        0x5182
#define SAMPLING_REC_BYTES  12   /* mode(2)+bucket(2)+volume(2)+datetime(6) */
#define RETAIN_REC_BYTES    14   /* mode(2)+bottle(2)+volume(2)+datetime(6)+result(2) */
#define EVENT_REC_BYTES      8   /* type(2)+datetime(6) */
```

3. 5个发送函数，每个构造帧数据并调用 `screen_write_var(SCR_LOG_ADDR, data, len)`:
   - `send_sampling_page(page)` — 7条 × 12字节
   - `send_delivery_page(page)` — 7条 × 12字节
   - `send_retain_page(page)` — 7条 × 14字节
   - `send_power_page(page)` — 7条 × 8字节
   - `send_door_page(page)` — 7条 × 8字节

每个函数流程:
1. 从缓存/直查获取当前页数据
2. 构造帧数据数组
3. 调用 screen_write_var 发送

**验收**: 编译通过

### Task 3.3: 实现 record_query_init 和 record_query_page_nav

**文件**: `samp/middlewares/bsp/app_record_query.c`

**修改内容**: 填充之前的空函数体:

`record_query_init(type)`:
1. 计数总记录数 → s_session[type].total_count
2. s_session[type].current_page = 0
3. s_session[type].valid = 1
4. 加载缓存窗口 (采样/送样/留样)
5. 调用对应的 send_xxx_page(0) 发送第一页

`record_query_page_nav(type, direction)`:
1. 检查 session.valid
2. direction=0x01 上一页, direction=0x02 下一页
3. 边界检查
4. 更新 current_page
5. 调用对应的 send_xxx_page(current_page)

**验收**: 编译通过

---

## Batch 4: 屏幕命令分发集成

### Task 4.1: app_screen.c screen_handle_confirm 添加查询/翻页分发

**文件**: `samp/middlewares/bsp/app_screen.c`

**修改内容**: 在 `screen_handle_confirm` 的 switch(action) 中，`default` 之前添加:

```c
case SCR_ACT_LOG_QUERY: {
    /* param: 0x71=采样 0x72=送样 0x73=留样 0x74=电源 0x75=门禁 */
    rq_type_t qt = (rq_type_t)(param - 0x71);
    if (qt < RQ_TYPE_COUNT)
        record_query_init(qt);
    break;
}
case SCR_ACT_LOG_SAMP:
    record_query_page_nav(RQ_SAMPLING, param);
    break;
case SCR_ACT_LOG_DELIV:
    record_query_page_nav(RQ_DELIVERY, param);
    break;
case SCR_ACT_LOG_RETAIN:
    record_query_page_nav(RQ_RETAIN, param);
    break;
case SCR_ACT_LOG_POWER:
    record_query_page_nav(RQ_POWER, param);
    break;
case SCR_ACT_LOG_DOOR:
    record_query_page_nav(RQ_DOOR, param);
    break;
```

需要在文件顶部添加 `#include "app_record_query.h"`。

**验收**: 编译通过，0 Error 0 Warning

---

## Batch 5: 记录写入集成

### Task 5.1: 采样完成时写入 SampleLogData

**文件**: `samp/middlewares/bsp/app_sampling.c`

**修改位置**: `sampling_step()` 中 SAMP_POST_BLOW case，`s_samp.stage = SAMP_DONE` 之前（约第164行）。

**添加代码**:
```c
/* 写入采样记录 */
{
    SampleLogData log;
    log.trigger_source = s_samp.is_manual ? 0 : 1;
    log.bucket_id = (uint8_t)(s_samp.bucket_id + 1);
    log.sample_volume = 0;  /* 暂无流量计，后续补充 */
    log.result = s_samp.result;
    tsdb_event_append(EVT_SAMPLE_DONE, &log, sizeof(log));
}
```

需要在文件顶部添加 `#include "app_flashdb.h"`。

**验收**: 编译通过

### Task 5.2: 送样完成时写入 DeliveryLogData

**文件**: `samp/project/src/freertos_app.c`

**修改位置**: `handle_retain_and_drain` 函数调用前（即 Task04 收到通知并确定 bucket 后），写入送样记录。

```c
/* 送样记录写入 */
{
    DeliveryLogData log;
    log.trigger_source = 0; /* TODO: 获取实际触发源 */
    log.water_source = (uint8_t)(bucket + 1);
    log.delivery_volume = 0; /* TODO: 获取实际送样量 */
    log.result = delivery_get_result();
    tsdb_event_append(EVT_DELIVERY_DONE, &log, sizeof(log));
}
```

需要在文件顶部添加 `#include "app_flashdb.h"`（如果尚未包含）。

**验收**: 编译通过

### Task 5.3: 留样完成时写入 RetainSampleLogData

**文件**: `samp/middlewares/bsp/app_retain_judge.c`

**修改位置**: `retention_execute` 函数中，`retain_advance_bottle()` 之前:

```c
/* 留样记录写入 */
{
    RetainSampleLogData log;
    log.trigger_source = 0;
    log.bottle_id = bottle;
    log.retain_volume = 0; /* TODO: 获取实际留样量 */
    log.success = 1;
    log.acid_added = 0;
    tsdb_event_append(EVT_RETAIN_DONE, &log, sizeof(log));
}
```

**验收**: 编译通过

### Task 5.4: 电源/门禁事件写入

**分析**: 需要确认 samp 中断电检测和门禁IO的位置。

**电源事件**:
- 断电检测: 查找 PVD 中断或类似机制
- 上电恢复: 在系统初始化时写入 EVT_POWER_ON

**门禁事件**:
- 查找门磁IO中断处理

如果这些机制尚未实现，此任务可标记为"待硬件接口就绪后集成"，先预留调用点。

**验收**: 编译通过

### Task 5.5: 全量 rebuild 验证

**操作**: 清理并重新编译整个工程，确保 0 Error 0 Warning。

---

## 文件变更汇总

| 文件 | 操作 | Batch |
|------|------|-------|
| `middlewares/bsp/flashDB/app_flashdb.h` | 修改: 添加EVT常量 + tsdb_iter_reverse_all声明 | 1 |
| `middlewares/bsp/flashDB/app_flashdb.c` | 修改: 添加tsdb_iter_reverse_all实现 | 1 |
| `middlewares/bsp/app_record_query.h` | 新建 | 1 |
| `middlewares/bsp/app_record_query.c` | 新建 | 1-3 |
| `middlewares/bsp/app_screen.h` | 修改: 添加SCR_ACT_LOG_xxx | 3 |
| `middlewares/bsp/app_screen.c` | 修改: confirm分发 + include | 4 |
| `middlewares/bsp/app_scheduler.c` | 修改: 采样记录写入 | 5 |
| `middlewares/bsp/app_retain_judge.c` | 修改: 留样记录写入 | 5 |
| `project/src/freertos_app.c` | 修改: 送样记录写入 | 5 |
| `project/MDK_V5/samp.uvprojx` | 修改: 添加app_record_query.c | 1 |

## 已确认事项

1. **encode_datetime 实现**: TSDB 时间戳使用自定义公式 `year*365 + (month-1)*30 + day → days*86400 + h*3600 + m*60 + s`。encode_datetime 需反向解析此公式。同时 `ertc_calendar_get(&time)` 可直接获取当前 RTC 时间。
2. **采样完成写入点**: `sampling_step()` 中 `SAMP_POST_BLOW` → `SAMP_DONE` 转换处（app_sampling.c:164），此处 `s_samp.result=1` 已设置。
3. **送样/留样实际数据**: 采样和送样状态机均无 volume 字段，volume 暂填 0，后续硬件流量计集成后补充。trigger_source 可从 `s_samp.is_manual` 推断。
4. **电源/门禁硬件接口**: samp 中尚无 PVD 断电检测和门磁 IO 代码。Task 5.4 标记为"预留接口，待硬件就绪后集成"，仅在 app_flashdb.h 中定义好 EVT 常量。
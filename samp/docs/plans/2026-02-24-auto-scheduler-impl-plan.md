# samp 自动调度器实施计划

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 samp 工程实现全自动运行能力，支持5种采样触发模式 + 7种留样模式。

**Architecture:** 单文件调度器 app_scheduler.c 内含5种模式状态机，通过 scheduler_run() 在 Task02 中 100ms 非阻塞调用。AD模块数据通过 UART8 接收，留样判定在 Task04 中阻塞执行。屏幕分发器复用 samplingB 地址体系。

**Tech Stack:** AT32F435 MCU, FreeRTOS (raw API), Keil MDK V5, C99

**编译验证命令:** `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`

**要求:** 每个任务完成后必须编译通过（0错误0警告）

---

## 批次1：基础层（app_config扩展 + app_adc_module）

### Task 1.1: 扩展 app_config.h 配置结构体

**Files:**
- Modify: `samp/middlewares/bsp/app_config.h`

**Step 1: 扩展 SamplingConfig**

在 `SamplingConfig` 结构体末尾添加字段：

```c
typedef struct {
    uint8_t  mode;            /* 0=时间等比 1=定时 2=流量 3=开关量 4=通信 */
    uint16_t interval_min;    /* 采样间隔(分钟) */
    uint16_t volume_ml;       /* 单次采样量(ml) */
    uint16_t blowback_sec;    /* 反吹时长(s) */
    uint16_t improve_sec;     /* 提升时长(s) */
    uint16_t tube_hold_sec;   /* 管存时长(s) */
    uint16_t motor_rpm;       /* 采样电机转速 */
    uint16_t cycle_time_min;  /* 周期时间(分钟) — 新增 */
    uint16_t analysis_time_min; /* 仪器分析时间(分钟) — 新增 */
    uint16_t flow_start;      /* 流量触发值(m³/h) — 新增 */
    uint16_t flow_stop;       /* 流量停止值(m³/h) — 新增 */
} SamplingConfig;
```

**Step 2: 扩展 DeliveryConfig**

```c
typedef struct {
    uint16_t volume_ml;       /* 送样量(ml) */
    uint16_t motor_rpm;       /* 送样电机转速 */
    uint16_t backdraw_sec;    /* 回抽时长(s) */
    uint8_t  enable;          /* 是否启用定时送样 — 新增 */
    uint8_t  start_hour;      /* 送样小时 — 新增 */
    uint8_t  start_min;       /* 送样分钟 — 新增 */
    uint16_t duration_sec;    /* 送样时长(s) — 新增 */
    uint8_t  fixedhour[24];   /* 定时触发小时数组 — 新增 */
    uint8_t  fixedmin;        /* 定时触发分钟 — 新增 */
} DeliveryConfig;
```

**Step 3: 扩展 RetainConfig**

```c
typedef struct {
    uint8_t  mode;            /* 留样模式(0-6) */
    uint16_t volume_ml;       /* 留样量(ml) */
    uint8_t  bottle_count;    /* 留样瓶数(1-24) */
    uint16_t motor_rpm;       /* 留样电机转速 */
    uint8_t  enable;          /* 是否留样 — 新增 */
    uint8_t  parallel_count;  /* 平行样数量 — 新增 */
    uint8_t  mix_count;       /* 混样次数 — 新增 */
    uint8_t  enable_acid;     /* 是否加酸 — 新增 */
    uint16_t tube_hold_sec;   /* 留样管存时间(s) — 新增 */
    uint16_t blowback_sec;    /* 留样反吹时间(s) — 新增 */
    uint16_t backdraw_sec;    /* 留样回抽时间(s) — 新增 */
} RetainConfig;
```

**Step 4: 新增 CommConfig 和 ChannelLimitConfig**

```c
/* 通讯配置 */
typedef struct {
    uint8_t  protocol;        /* 通讯协议(0=大岳 1=大湖...) */
    uint8_t  device_addr;     /* 设备地址 */
    uint16_t flow_ad_lower;   /* 流量AD下限(0=0-20mA, 非0=4-20mA) */
    float    flow_meter_base; /* 流量计量程(m³/h) */
} CommConfig;

/* 通道限值配置 */
typedef struct {
    uint8_t  enable;          /* 是否启用 */
    uint8_t  factor_type;     /* 因子类型 */
    float    lower_limit;     /* 超标下限 */
    float    upper_limit;     /* 超标上限 */
} ChannelLimitConfig;

extern CommConfig         g_comm_cfg;
extern ChannelLimitConfig g_ch_limits[6];
```

**Step 5: 扩展 SystemState**

在 `SystemState` 结构体中添加设备状态和调度器状态字段：

```c
typedef struct {
    uint8_t  running;
    uint8_t  bucket_a_state;
    uint8_t  bucket_b_state;
    uint8_t  current_bucket;
    /* 新增：设备状态 */
    uint8_t  sampling_motor;
    uint8_t  delivery_motor;
    uint8_t  inlet_valve;
    uint8_t  outlet_valve;
    uint8_t  sample_valve;
    uint8_t  instant_valve;
    uint8_t  drain_a;
    uint8_t  drain_b;
    uint16_t water_a;         /* A桶存水量(ml) */
    uint16_t water_b;         /* B桶存水量(ml) */
    /* 新增：调度器状态 */
    uint8_t  current_mode;
    uint8_t  current_phase;
    uint32_t cycle_count;
    uint32_t sample_count;
    uint32_t delivery_count;
    /* 新增：留样瓶 */
    uint8_t  bottle_current;
    uint8_t  bottle_next;
    uint8_t  bottle_empty;
    /* 新增：时间 */
    uint8_t  time[6];
} SystemState;
```

**Step 6: 编译验证**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

注意：扩展结构体后，现有代码中引用旧字段的地方不会报错（只是新增字段），但 KVDB 的 cfg_save/cfg_load 使用固定大小（32字节），结构体扩展后可能超出。需要同步更新 `app_flashdb.c` 中的大小参数。

**Step 7: 提交**

```bash
git add samp/middlewares/bsp/app_config.h
git commit -m "feat: 扩展配置结构体（采样/送样/留样/通讯/通道限值）"
```

---

### Task 1.2: 更新 app_config.c 默认值和KVDB存取

**Files:**
- Modify: `samp/middlewares/bsp/app_config.c`
- Modify: `samp/middlewares/bsp/flashDB/app_flashdb.c` (KVDB大小参数)

**Step 1: 添加新全局实例和默认值**

在 `app_config.c` 中添加：

```c
CommConfig         g_comm_cfg;
ChannelLimitConfig g_ch_limits[6];

static const CommConfig s_comm_default = {
    .protocol       = PROTO_DAYUE,
    .device_addr    = 1,
    .flow_ad_lower  = 0,
    .flow_meter_base = 100.0f,
};

static const ChannelLimitConfig s_ch_limit_default = {
    .enable      = 0,
    .factor_type = 0,
    .lower_limit = 0.0f,
    .upper_limit = 0.0f,
};
```

**Step 2: 更新 s_samp_default 和 s_deliv_default 和 s_retain_default**

为新增字段添加默认值：

```c
static const SamplingConfig s_samp_default = {
    .mode            = 0,
    .interval_min    = 15,
    .volume_ml       = 100,
    .blowback_sec    = 5,
    .improve_sec     = 30,
    .tube_hold_sec   = 10,
    .motor_rpm       = 1000,
    .cycle_time_min  = 60,
    .analysis_time_min = 30,
    .flow_start      = 10,
    .flow_stop       = 5,
};

static const DeliveryConfig s_deliv_default = {
    .volume_ml     = 500,
    .motor_rpm     = 1000,
    .backdraw_sec  = 3,
    .enable        = 1,
    .start_hour    = 0,
    .start_min     = 58,
    .duration_sec  = 120,
    .fixedhour     = {0},
    .fixedmin      = 58,
};

static const RetainConfig s_retain_default = {
    .mode           = 0,
    .volume_ml      = 200,
    .bottle_count   = 24,
    .motor_rpm      = 800,
    .enable         = 1,
    .parallel_count = 1,
    .mix_count      = 1,
    .enable_acid    = 0,
    .tube_hold_sec  = 10,
    .blowback_sec   = 5,
    .backdraw_sec   = 3,
};
```

**Step 3: 在 cfg_init_load() 中添加 CommConfig 和 ChannelLimitConfig 加载**

```c
/* 加载通讯配置 */
if (!cfg_load_comm(&g_comm_cfg)) {
    g_comm_cfg = s_comm_default;
    cfg_save_comm(&g_comm_cfg);
    printf("[CFG] 通讯配置: 使用默认值\r\n");
} else {
    printf("[CFG] 通讯配置: 从KVDB加载\r\n");
}

/* 通道限值使用默认值（暂不持久化，由屏幕设置） */
for (int i = 0; i < 6; i++) {
    g_ch_limits[i] = s_ch_limit_default;
}
```

**Step 4: 更新 cfg_save_all()**

```c
void cfg_save_all(void)
{
    cfg_save_sample(&g_sampling_cfg);
    cfg_save_delivery(&g_delivery_cfg);
    cfg_save_retain(&g_retain_cfg);
    cfg_save_comm(&g_comm_cfg);
}
```

**Step 5: 更新 app_flashdb.c 中的大小参数**

检查 `cfg_save_sample` 等函数中的大小参数（当前为32），如果结构体超过32字节需要增大。

- SamplingConfig: 原7字段约14字节 → 新增4字段约8字节 = ~22字节，32够用
- DeliveryConfig: 原3字段约6字节 → 新增fixedhour[24]+其他约32字节 = ~38字节，需要增大到64
- RetainConfig: 原4字段约6字节 → 新增7字段约12字节 = ~18字节，32够用
- CommConfig: ~8字节，32够用

修改 `app_flashdb.c`:
```c
uint8_t cfg_save_delivery(const void *p)     { return cfg_save(KV_DELIVERY, p, 64); }
uint8_t cfg_load_delivery(void *p)           { return cfg_load(KV_DELIVERY, p, 64); }
```

**Step 6: 编译验证**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

**Step 7: 提交**

```bash
git add samp/middlewares/bsp/app_config.c samp/middlewares/bsp/flashDB/app_flashdb.c
git commit -m "feat: 更新配置默认值和KVDB存取大小"
```

---

### Task 1.3: 创建 app_adc_module.h

**Files:**
- Create: `samp/middlewares/bsp/app_adc_module.h`

**Step 1: 创建头文件**

```c
/**
 * @file    app_adc_module.h
 * @brief   外部AD模块数据接收（UART8 16字节帧解析 + 流量检测）
 */
#ifndef APP_ADC_MODULE_H
#define APP_ADC_MODULE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_MOD_CH_COUNT    6
#define ADC_MOD_FRAME_LEN   16
#define ADC_MOD_HEAD1       0x6B
#define ADC_MOD_HEAD2       0xB6
#define ADC_MOD_TAIL1       0x8C
#define ADC_MOD_TAIL2       0xC8

typedef struct {
    uint16_t raw[ADC_MOD_CH_COUNT];   /* 原始值 (mA×1000) */
    float    ma[ADC_MOD_CH_COUNT];    /* 转换后mA值 */
    float    flow_value;               /* 换算后实际流量 (m³/h) */
    uint8_t  flow_active;              /* 流量信号状态 */
    uint8_t  valid;                    /* 数据有效标志 */
    uint32_t last_update_tick;         /* 最后更新时间(ms) */
    uint32_t rx_count;                 /* 接收帧计数 */
    uint32_t err_count;                /* 错误帧计数 */
} adc_module_data_t;

extern adc_module_data_t g_adc_module;

void     adc_module_init(void);
void     adc_module_poll(void);           /* Task05调用 */
uint16_t adc_module_get_raw(uint8_t ch);
float    adc_module_get_ma(uint8_t ch);
float    adc_module_get_flow(void);       /* 换算后流量(m³/h) */
uint8_t  adc_module_is_flow_active(void);
uint8_t  adc_module_is_valid(void);       /* 2秒超时 */

#ifdef __cplusplus
}
#endif

#endif /* APP_ADC_MODULE_H */
```

**Step 2: 编译验证**（仅头文件，不影响编译）

---

### Task 1.4: 创建 app_adc_module.c

**Files:**
- Create: `samp/middlewares/bsp/app_adc_module.c`

**Step 1: 实现AD模块数据接收**

```c
/**
 * @file    app_adc_module.c
 * @brief   外部AD模块数据接收 — UART8帧解析 + 流量换算 + 边沿检测
 */
#include "app_adc_module.h"
#include "app_config.h"
#include "bsp_uart.h"
#include <stdio.h>

/* 外部定时器变量 */
extern volatile uint32_t g_tmr4_milliseconds;

/* 全局数据实例 */
adc_module_data_t g_adc_module;

/* 前向声明 */
static void adc_module_update_flow(void);

void adc_module_init(void)
{
    uint8_t i;
    for (i = 0; i < ADC_MOD_CH_COUNT; i++) {
        g_adc_module.raw[i] = 0;
        g_adc_module.ma[i]  = 0.0f;
    }
    g_adc_module.flow_value       = 0.0f;
    g_adc_module.flow_active      = 0;
    g_adc_module.valid            = 0;
    g_adc_module.last_update_tick = 0;
    g_adc_module.rx_count         = 0;
    g_adc_module.err_count        = 0;
    printf("[ADC_MOD] 初始化完成\r\n");
}

void adc_module_poll(void)
{
    uint8_t buf[48];
    uint16_t len;
    uint16_t i;
    uint8_t ch;

    if (!bsp_uart_rx_available(UART_PORT_ADMODULE))
        return;

    len = bsp_uart_get_rxdata(UART_PORT_ADMODULE, buf, sizeof(buf));
    if (len < ADC_MOD_FRAME_LEN)
        return;

    /* 在接收数据中搜索帧头 */
    for (i = 0; i + ADC_MOD_FRAME_LEN <= len; i++) {
        if (buf[i]      == ADC_MOD_HEAD1 &&
            buf[i + 1]  == ADC_MOD_HEAD2 &&
            buf[i + 14] == ADC_MOD_TAIL1 &&
            buf[i + 15] == ADC_MOD_TAIL2)
        {
            /* 解析6通道 */
            for (ch = 0; ch < ADC_MOD_CH_COUNT; ch++) {
                uint16_t offset = i + 2 + ch * 2;
                g_adc_module.raw[ch] = ((uint16_t)buf[offset] << 8) | buf[offset + 1];
                g_adc_module.ma[ch]  = (float)g_adc_module.raw[ch] / 1000.0f;
            }
            g_adc_module.valid            = 1;
            g_adc_module.last_update_tick = g_tmr4_milliseconds;
            g_adc_module.rx_count++;

            /* 流量换算 + 边沿检测 */
            adc_module_update_flow();
            return;
        }
    }
    g_adc_module.err_count++;
}

/* 流量换算 + 迟滞边沿检测 */
static void adc_module_update_flow(void)
{
    float current_ma = g_adc_module.ma[5]; /* CH6 = 流量 */
    float i_lower = (g_comm_cfg.flow_ad_lower == 0) ? 0.0f : 4.0f;
    float i_upper = 20.0f;
    float ratio;

    if (i_upper <= i_lower) {
        g_adc_module.flow_value = 0.0f;
        return;
    }

    ratio = (current_ma - i_lower) / (i_upper - i_lower);
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    g_adc_module.flow_value = ratio * g_comm_cfg.flow_meter_base;

    /* 迟滞边沿检测 */
    if (!g_adc_module.flow_active &&
        g_adc_module.flow_value >= (float)g_sampling_cfg.flow_start)
    {
        g_adc_module.flow_active = 1;
        printf("[ADC_MOD] 流量开始: %.2f >= %u\r\n",
               g_adc_module.flow_value, g_sampling_cfg.flow_start);
        /* scheduler_notify_flow(1) 将在批次2集成 */
    }
    else if (g_adc_module.flow_active &&
             g_adc_module.flow_value <= (float)g_sampling_cfg.flow_stop)
    {
        g_adc_module.flow_active = 0;
        printf("[ADC_MOD] 流量停止: %.2f <= %u\r\n",
               g_adc_module.flow_value, g_sampling_cfg.flow_stop);
        /* scheduler_notify_flow(0) 将在批次2集成 */
    }
}

uint16_t adc_module_get_raw(uint8_t ch)
{
    if (ch >= ADC_MOD_CH_COUNT) return 0;
    return g_adc_module.raw[ch];
}

float adc_module_get_ma(uint8_t ch)
{
    if (ch >= ADC_MOD_CH_COUNT) return 0.0f;
    return g_adc_module.ma[ch];
}

float adc_module_get_flow(void)
{
    return g_adc_module.flow_value;
}

uint8_t adc_module_is_flow_active(void)
{
    return g_adc_module.flow_active;
}

uint8_t adc_module_is_valid(void)
{
    return g_adc_module.valid &&
           (g_tmr4_milliseconds - g_adc_module.last_update_tick < 2000);
}
```

**Step 2: 添加到Keil工程**

在 `samp.uvprojx` 的 `middlewares/bsp` Group 中，在 `app_modbus.c` 条目之后添加：

```xml
<File>
  <FileName>app_adc_module.c</FileName>
  <FileType>1</FileType>
  <FilePath>../../middlewares/bsp/app_adc_module.c</FilePath>
</File>
```

**Step 3: 编译验证**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

**Step 4: 提交**

```bash
git add samp/middlewares/bsp/app_adc_module.h samp/middlewares/bsp/app_adc_module.c samp/project/MDK_V5/samp.uvprojx
git commit -m "feat: 创建AD模块数据接收(UART8帧解析+流量检测)"
```

---

### Task 1.5: 集成 adc_module_poll 到 Task05

**Files:**
- Modify: `samp/project/src/freertos_app.c` (Task05函数)

**Step 1: 在 freertos_app.c 顶部添加 include**

```c
#include "app_adc_module.h"
```

**Step 2: 在 Task05 初始化部分调用 adc_module_init()**

在 Task05 的 `for(;;)` 之前添加：
```c
adc_module_init();
```

**Step 3: 在 Task05 循环中调用 adc_module_poll()**

在 Task05 的 4G UART 处理之后添加：
```c
/* AD模块数据接收(UART8) */
adc_module_poll();
```

**Step 4: 编译验证**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

**Step 5: 提交**

```bash
git add samp/project/src/freertos_app.c
git commit -m "feat: 集成AD模块数据接收到Task05"
```

---

## 批次2：核心调度器框架 + 时间等比模式

### Task 2.1: 创建 app_scheduler.h

**Files:**
- Create: `samp/middlewares/bsp/app_scheduler.h`

**Step 1: 创建头文件**

```c
/**
 * @file    app_scheduler.h
 * @brief   自动调度器接口（5种采样触发模式）
 */
#ifndef APP_SCHEDULER_H
#define APP_SCHEDULER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 采样触发模式 */
typedef enum {
    SCHED_MODE_TIME_PROP  = 0,  /* 时间等比 */
    SCHED_MODE_FIXED_TIME = 1,  /* 定时触发 */
    SCHED_MODE_FLOW       = 2,  /* 流量触发 */
    SCHED_MODE_SWITCH     = 3,  /* 开关量触发 */
    SCHED_MODE_COMM       = 4,  /* 通信触发 */
} sched_mode_t;

/* 调度阶段 */
typedef enum {
    PHASE_IDLE    = 0,  /* 空闲 */
    PHASE_STARTUP = 1,  /* 启动阶段 */
    PHASE_CYCLING = 2,  /* 周期循环 */
    PHASE_STOPPED = 3,  /* 已停止(等待恢复) */
} sched_phase_t;

/* 通信触发请求类型 */
typedef enum {
    COMM_REQ_NONE     = 0,
    COMM_REQ_SAMPLING = 1,
    COMM_REQ_DRAIN    = 2,
    COMM_REQ_DELIVERY = 3,
} comm_req_type_t;

/* 通信触发请求 */
typedef struct {
    comm_req_type_t type;
    uint8_t  bucket;    /* 0=A, 1=B, 2=自动 */
    uint16_t volume;
    uint8_t  pending;
} comm_trigger_req_t;

extern comm_trigger_req_t g_comm_trigger_req;

/* 调度器接口 */
void    scheduler_init(sched_mode_t mode);
void    scheduler_start(void);
void    scheduler_stop(void);
void    scheduler_run(void);          /* Task02调用, 非阻塞 */
uint8_t scheduler_is_running(void);
sched_phase_t scheduler_get_phase(void);

/* 外部事件通知 */
void scheduler_notify_flow(uint8_t active);
void scheduler_notify_switch(void);
void scheduler_notify_comm(comm_req_type_t req,
                           uint8_t bucket, uint16_t vol);

/* 送样完成通知Task04（由调度器内部调用） */
void scheduler_notify_task4_delivery(uint8_t bucket_id);

#ifdef __cplusplus
}
#endif

#endif /* APP_SCHEDULER_H */
```

---

### Task 2.2: 创建 app_scheduler.c 框架 + 时间等比模式

**Files:**
- Create: `samp/middlewares/bsp/app_scheduler.c`

**Step 1: 创建调度器框架（状态结构体 + 公共接口 + 模式分发）**

```c
/**
 * @file    app_scheduler.c
 * @brief   自动调度器 — 5种模式状态机 + 链式编排
 */
#include "app_scheduler.h"
#include "app_config.h"
#include "app_sampling.h"
#include "app_adc_module.h"
#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

/* 外部时间变量 */
extern volatile uint32_t g_tmr2_seconds;

/* FreeRTOS通知（Task04句柄，在freertos_app.c中定义） */
extern void *my_task04_handle;  /* TaskHandle_t */

/* 最大采样点数 */
#define MAX_SAMPLE_POINTS  24

/* 调度器内部状态 */
typedef struct {
    /* 公共字段 */
    sched_mode_t   mode;
    sched_phase_t  phase;
    uint8_t        running;
    uint8_t        active_bucket;
    uint32_t       cycle_idx;
    uint8_t        cycle_start_hour;
    uint8_t        sample_count;
    uint16_t       sample_offsets[MAX_SAMPLE_POINTS];
    uint32_t       sample_done_mask;
    uint8_t        delivery_done;
    uint32_t       total_cycles;
    uint32_t       total_samples;
    uint32_t       total_deliveries;

    /* 时间等比专用 */
    struct {
        uint8_t  startup_mode;   /* 0=FULL 1=SKIP 2=INSTANT */
        uint8_t  delay_active;
        uint32_t delay_end_sec;
        uint32_t anchor_sec;     /* 送样锚点(秒) */
    } tp;

    /* 定时触发专用 */
    struct {
        uint8_t  next_delivery_idx;
        uint8_t  last_check_hour;
    } ft;

    /* 流量触发专用 */
    struct {
        uint8_t  flow_active;
        uint8_t  startup_phase;  /* 0=瞬时送样 1=满量采样 2=等整点 */
        uint32_t flow_start_time;
        uint32_t flow_stop_time;
    } fl;

    /* 开关量专用 */
    struct {
        uint8_t  first_trigger_done;
        uint8_t  waiting_resume;
        uint8_t  window_triggered[MAX_SAMPLE_POINTS];
    } sw;

    /* 通信触发专用 */
    struct {
        uint32_t last_delivery_time;
    } cm;
} scheduler_state_t;

static scheduler_state_t s_sched;

/* 通信触发请求（全局，供Modbus写入） */
comm_trigger_req_t g_comm_trigger_req;

/* 前向声明：各模式处理函数 */
static void sched_time_prop(void);
static void sched_fixed_time(void);
static void sched_flow(void);
static void sched_switch(void);
static void sched_comm(void);

/* RTC时间获取辅助（需要外部提供） */
extern void rtc_time_get(void);
extern struct { uint8_t hour; uint8_t min; uint8_t sec; } calendar;
```

注意：`calendar` 结构体和 `rtc_time_get()` 需要与现有RTC驱动对接。如果samp工程中RTC接口不同，需要适配。

**Step 2: 实现公共接口**

```c
void scheduler_init(sched_mode_t mode)
{
    memset(&s_sched, 0, sizeof(s_sched));
    s_sched.mode = mode;
    s_sched.phase = PHASE_IDLE;

    /* 计算采样点 */
    if (g_sampling_cfg.interval_min > 0 && g_sampling_cfg.cycle_time_min > 0) {
        s_sched.sample_count = g_sampling_cfg.cycle_time_min / g_sampling_cfg.interval_min;
        if (s_sched.sample_count > MAX_SAMPLE_POINTS)
            s_sched.sample_count = MAX_SAMPLE_POINTS;
    } else {
        s_sched.sample_count = 1;
    }

    uint8_t i;
    for (i = 0; i < s_sched.sample_count; i++) {
        s_sched.sample_offsets[i] = i * g_sampling_cfg.interval_min;
    }

    printf("[调度器] 初始化: 模式=%d, 周期=%d分, 间隔=%d分, 采样次数=%d\r\n",
           mode, g_sampling_cfg.cycle_time_min,
           g_sampling_cfg.interval_min, s_sched.sample_count);
}

void scheduler_start(void)
{
    if (!s_sched.running) {
        s_sched.running = 1;
        s_sched.phase = PHASE_STARTUP;
        g_state.current_mode = (uint8_t)s_sched.mode;
        g_state.current_phase = (uint8_t)s_sched.phase;
        printf("[调度器] 启动: 模式=%d\r\n", s_sched.mode);
    }
}

void scheduler_stop(void)
{
    s_sched.running = 0;
    s_sched.phase = PHASE_IDLE;
    g_state.current_phase = PHASE_IDLE;
    printf("[调度器] 停止\r\n");
}

void scheduler_run(void)
{
    if (!s_sched.running) return;

    switch (s_sched.mode) {
    case SCHED_MODE_TIME_PROP:  sched_time_prop();  break;
    case SCHED_MODE_FIXED_TIME: sched_fixed_time(); break;
    case SCHED_MODE_FLOW:       sched_flow();       break;
    case SCHED_MODE_SWITCH:     sched_switch();     break;
    case SCHED_MODE_COMM:       sched_comm();       break;
    default: break;
    }

    /* 同步状态到g_state */
    g_state.current_bucket = s_sched.active_bucket;
    g_state.current_phase  = (uint8_t)s_sched.phase;
    g_state.cycle_count    = s_sched.total_cycles;
    g_state.sample_count   = s_sched.total_samples;
    g_state.delivery_count = s_sched.total_deliveries;
}

uint8_t scheduler_is_running(void) { return s_sched.running; }
sched_phase_t scheduler_get_phase(void) { return s_sched.phase; }

/* 外部事件通知 */
void scheduler_notify_flow(uint8_t active)
{
    if (s_sched.mode != SCHED_MODE_FLOW) return;
    s_sched.fl.flow_active = active;
    if (active) {
        s_sched.fl.flow_start_time = g_tmr2_seconds;
    } else {
        s_sched.fl.flow_stop_time = g_tmr2_seconds;
    }
}

void scheduler_notify_switch(void)
{
    if (s_sched.mode != SCHED_MODE_SWITCH) return;
    s_sched.sw.first_trigger_done = 0; /* 将在sched_switch中处理 */
}

void scheduler_notify_comm(comm_req_type_t req, uint8_t bucket, uint16_t vol)
{
    g_comm_trigger_req.type    = req;
    g_comm_trigger_req.bucket  = bucket;
    g_comm_trigger_req.volume  = vol;
    g_comm_trigger_req.pending = 1;
}

/* Task04通知辅助 */
void scheduler_notify_task4_delivery(uint8_t bucket_id)
{
    /* xTaskNotify在freertos_app.c中实现，这里用extern */
    extern void notify_task4_delivery_complete(uint8_t bucket_id);
    notify_task4_delivery_complete(bucket_id);
}
```

**Step 3: 实现时间等比模式**

```c
/* 分钟取整辅助（复用samplingB的tp_minutes_round_up逻辑） */
static uint8_t tp_round_hour(uint8_t hour, uint8_t min)
{
    if (min >= 30) return (hour + 1) % 24;
    return hour;
}

static void sched_time_prop(void)
{
    rtc_time_get();
    uint32_t now_sec = (uint32_t)calendar.hour * 3600 +
                       (uint32_t)calendar.min * 60 + calendar.sec;
    uint16_t cycle_min = g_sampling_cfg.cycle_time_min;
    uint16_t cycle_hours = cycle_min / 60;

    if (cycle_hours == 0) cycle_hours = 1;

    /* === STARTUP阶段 === */
    if (s_sched.phase == PHASE_STARTUP) {
        /* 计算送样锚点 */
        uint8_t anchor_hour = tp_round_hour(calendar.hour, calendar.min);
        uint32_t anchor_sec = (uint32_t)anchor_hour * 3600 +
                              (uint32_t)g_delivery_cfg.start_min * 60;
        if (anchor_sec <= now_sec) anchor_sec += 3600; /* 下一个整点 */

        uint32_t remaining = anchor_sec - now_sec;
        s_sched.tp.anchor_sec = anchor_sec;

        if (remaining >= cycle_min * 60) {
            s_sched.tp.startup_mode = 0; /* FULL_SAMPLING */
            printf("[时间等比] 启动模式: FULL_SAMPLING\r\n");
        } else if (remaining < g_sampling_cfg.interval_min * 60) {
            s_sched.tp.startup_mode = 2; /* INSTANT_SAMPLING */
            printf("[时间等比] 启动模式: INSTANT_SAMPLING\r\n");
        } else {
            s_sched.tp.startup_mode = 1; /* SKIP_TO_CYCLE */
            printf("[时间等比] 启动模式: SKIP_TO_CYCLE\r\n");
        }

        /* 计算周期起点 */
        s_sched.cycle_start_hour = anchor_hour;
        s_sched.cycle_idx = 0;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.active_bucket = 0; /* A桶 */

        s_sched.phase = PHASE_CYCLING;
        printf("[时间等比] 进入周期循环: anchor=%02d:%02d, cycle_start=%02d\r\n",
               anchor_hour, g_delivery_cfg.start_min, s_sched.cycle_start_hour);
        return;
    }

    /* === CYCLING阶段 === */
    if (s_sched.phase != PHASE_CYCLING) return;

    /* 周期索引计算 */
    uint32_t cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
    uint32_t elapsed;
    if (now_sec >= cycle_start_sec) {
        elapsed = now_sec - cycle_start_sec;
    } else {
        elapsed = now_sec + 86400 - cycle_start_sec; /* 跨天 */
    }
    uint32_t new_cycle_idx = elapsed / (cycle_min * 60);

    /* 周期切换 */
    if (new_cycle_idx != s_sched.cycle_idx) {
        s_sched.cycle_idx = new_cycle_idx;
        s_sched.active_bucket ^= 1;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.total_cycles++;
        printf("[时间等比] 新周期: idx=%lu, 桶=%c\r\n",
               (unsigned long)new_cycle_idx,
               s_sched.active_bucket ? 'B' : 'A');
    }

    /* delay机制（CycleTime==60时） */
    if (s_sched.tp.delay_active) {
        if (g_tmr2_seconds < s_sched.tp.delay_end_sec) return;
        s_sched.tp.delay_active = 0;
    }

    /* 采样触发 */
    {
        uint8_t i;
        uint32_t cycle_base = cycle_start_sec + s_sched.cycle_idx * cycle_min * 60;
        for (i = 0; i < s_sched.sample_count; i++) {
            uint32_t mask = 1u << i;
            if (s_sched.sample_done_mask & mask) continue;

            uint32_t sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
            int32_t diff = (int32_t)(now_sec - sample_sec);
            if (diff < 0) diff = -diff;

            if (diff <= 30 && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                    printf("[时间等比] 采样#%d: 桶=%c\r\n",
                           i + 1, s_sched.active_bucket ? 'B' : 'A');
                }
            }
        }
    }

    /* 送样触发 */
    if (!s_sched.delivery_done && calendar.sec == 0) {
        uint8_t delivery_hour = (s_sched.cycle_start_hour +
                                 (uint8_t)((s_sched.cycle_idx + 1) * cycle_hours) +
                                 24 - 1) % 24;
        if (calendar.hour == delivery_hour &&
            calendar.min == g_delivery_cfg.start_min)
        {
            uint16_t water = s_sched.active_bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(s_sched.active_bucket, 0)) {
                    s_sched.delivery_done = 1;
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(s_sched.active_bucket);
                    printf("[时间等比] 送样: 桶=%c, 水量=%u\r\n",
                           s_sched.active_bucket ? 'B' : 'A', water);
                }
            }
        }
    }
}
```

**Step 4: 添加占位模式函数（批次3实现）**

```c
static void sched_fixed_time(void)
{
    /* 批次3 Task 3.1 实现 */
}

static void sched_flow(void)
{
    /* 批次3 Task 3.2 实现 */
}

static void sched_switch(void)
{
    /* 批次3 Task 3.3 实现 */
}

static void sched_comm(void)
{
    /* 批次3 Task 3.4 实现 */
}
```

---

### Task 2.3: 添加 app_scheduler.c 到Keil工程 + Task02集成

**Files:**
- Modify: `samp/project/MDK_V5/samp.uvprojx`
- Modify: `samp/project/src/freertos_app.c`

**Step 1: 添加到Keil工程**

在 `samp.uvprojx` 的 `middlewares/bsp` Group 中添加：

```xml
<File>
  <FileName>app_scheduler.c</FileName>
  <FileType>1</FileType>
  <FilePath>../../middlewares/bsp/app_scheduler.c</FilePath>
</File>
```

**Step 2: 在 freertos_app.c 中 include**

```c
#include "app_scheduler.h"
```

**Step 3: 在 Task02 的 for(;;) 循环中，在注释 `/* 5. 周期调度逻辑 */` 处添加**

```c
    /* 5. 周期调度逻辑 */
    if (g_state.running) {
        scheduler_run();
    }
```

**Step 4: 在 Task02 初始化部分（cfg_init_load 之后）添加**

```c
    scheduler_init((sched_mode_t)g_sampling_cfg.mode);
```

**Step 5: 添加 notify_task4_delivery_complete 函数**

在 freertos_app.c 中添加（Task04句柄附近）：

```c
#include "task.h"

void notify_task4_delivery_complete(uint8_t bucket_id)
{
    uint32_t value = (bucket_id == 0xFF) ? 0xFF : (uint32_t)(bucket_id + 1);
    xTaskNotify(my_task04_handle, value, eSetValueWithOverwrite);
    printf("[调度器] 通知Task04: value=%lu\r\n", (unsigned long)value);
}
```

注意：需要确认 `my_task04_handle` 是 `TaskHandle_t` 类型且已正确声明为全局变量。

**Step 6: 在屏幕命令处理中，SYS_START/SYS_STOP 时调用 scheduler**

修改 `app_screen.c` 中的 `SCR_CMD_SYS_START` 和 `SCR_CMD_SYS_STOP` 处理：

```c
case SCR_CMD_SYS_START:
    g_state.running = 1;
    scheduler_init((sched_mode_t)g_sampling_cfg.mode);
    scheduler_start();
    printf("[屏幕] 系统启动\r\n");
    break;
case SCR_CMD_SYS_STOP:
    g_state.running = 0;
    scheduler_stop();
    printf("[屏幕] 系统停止\r\n");
    break;
```

**Step 7: 编译验证**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

**Step 8: 提交**

```bash
git add samp/middlewares/bsp/app_scheduler.h samp/middlewares/bsp/app_scheduler.c samp/project/MDK_V5/samp.uvprojx samp/project/src/freertos_app.c samp/middlewares/bsp/app_screen.c
git commit -m "feat: 创建调度器框架+时间等比模式+Task02集成"
```

---

## 批次3：补全4种触发模式

### Task 3.1: 实现定时触发模式 (sched_fixed_time)

**Files:**
- Modify: `samp/middlewares/bsp/app_scheduler.c` — 替换 `sched_fixed_time` 占位函数

**核心逻辑:**

```c
static void sched_fixed_time(void)
{
    rtc_time_get();

    if (s_sched.phase == PHASE_STARTUP) {
        s_sched.phase = PHASE_CYCLING; /* 无启动阶段 */
        s_sched.active_bucket = 0;
        s_sched.ft.last_check_hour = 0xFF;
        printf("[定时触发] 直接进入周期循环\r\n");
        return;
    }
    if (s_sched.phase != PHASE_CYCLING) return;

    /* 遍历fixedhour数组，检查当前小时是否有送样 */
    uint8_t h;
    for (h = 0; h < 24; h++) {
        if (!g_delivery_cfg.fixedhour[h]) continue;

        uint8_t fmin = g_delivery_cfg.fixedmin;

        /* 计算采样周期起点 */
        uint8_t cycle_start_h = (fmin >= 50) ? h : ((h + 23) % 24);

        /* 采样触发 */
        uint32_t cycle_base = (uint32_t)cycle_start_h * 3600;
        uint32_t now_sec = (uint32_t)calendar.hour * 3600 +
                           (uint32_t)calendar.min * 60 + calendar.sec;
        uint8_t i;
        for (i = 0; i < s_sched.sample_count; i++) {
            uint32_t mask = 1u << i;
            if (s_sched.sample_done_mask & mask) continue;

            uint32_t sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
            int32_t diff = (int32_t)(now_sec - sample_sec);
            if (diff < 0) diff = -diff;

            if (diff <= 30 && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                }
            }
        }

        /* 送样触发：精确到秒 */
        if (calendar.hour == h && calendar.min == fmin && calendar.sec == 0 &&
            !s_sched.delivery_done)
        {
            uint16_t water = s_sched.active_bucket ?
                             g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(s_sched.active_bucket, 0)) {
                    s_sched.delivery_done = 1;
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(s_sched.active_bucket);
                    s_sched.active_bucket ^= 1;
                    s_sched.sample_done_mask = 0;
                    s_sched.delivery_done = 0; /* 为下一小时重置 */
                    s_sched.total_cycles++;
                }
            }
        }
    }
}
```

**编译验证 + 提交**

---

### Task 3.2: 实现流量触发模式 (sched_flow)

**Files:**
- Modify: `samp/middlewares/bsp/app_scheduler.c` — 替换 `sched_flow` 占位函数
- Modify: `samp/middlewares/bsp/app_adc_module.c` — 连接 scheduler_notify_flow

**核心逻辑:**

```c
static void sched_flow(void)
{
    rtc_time_get();
    uint32_t now_sec = (uint32_t)calendar.hour * 3600 +
                       (uint32_t)calendar.min * 60 + calendar.sec;

    /* STOPPED: 等待流量信号 */
    if (s_sched.phase == PHASE_STOPPED || s_sched.phase == PHASE_STARTUP) {
        if (s_sched.fl.flow_active) {
            s_sched.phase = PHASE_STARTUP;
            s_sched.fl.startup_phase = 0;
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            printf("[流量触发] 流量开始，进入启动阶段\r\n");
        } else {
            if (s_sched.phase == PHASE_STARTUP) {
                s_sched.phase = PHASE_STOPPED;
            }
            return;
        }
    }

    /* 流量停止检测 */
    if (!s_sched.fl.flow_active && s_sched.phase == PHASE_CYCLING) {
        printf("[流量触发] 流量停止\r\n");
        sampling_abort();
        /* 送样当前桶 */
        uint16_t water = s_sched.active_bucket ?
                         g_state.water_b : g_state.water_a;
        if (water > 0 && !delivery_is_active()) {
            delivery_start(s_sched.active_bucket, 0);
            scheduler_notify_task4_delivery(0xFF); /* 0xFF=流量停止 */
        }
        s_sched.phase = PHASE_STOPPED;
        return;
    }

    /* STARTUP: 3阶段 */
    if (s_sched.phase == PHASE_STARTUP) {
        switch (s_sched.fl.startup_phase) {
        case 0: /* 瞬时送样 */
            if (!delivery_is_active()) {
                uint16_t water = g_state.water_a;
                if (water > 0) {
                    delivery_start(0, 0);
                    scheduler_notify_task4_delivery(0);
                }
                s_sched.fl.startup_phase = 1;
            }
            break;
        case 1: /* 满量采样(A桶) */
        {
            uint32_t expected = (1u << s_sched.sample_count) - 1;
            if (s_sched.sample_done_mask == expected) {
                s_sched.fl.startup_phase = 2;
                break;
            }
            /* 按间隔采样 */
            if (!sampling_is_active()) {
                uint8_t i;
                for (i = 0; i < s_sched.sample_count; i++) {
                    if (!(s_sched.sample_done_mask & (1u << i))) {
                        sampling_start(0, 0);
                        s_sched.sample_done_mask |= (1u << i);
                        s_sched.total_samples++;
                        break;
                    }
                }
            }
            break;
        }
        case 2: /* 等待整点 */
            if (calendar.min == 0 && calendar.sec == 0) {
                s_sched.cycle_start_hour = calendar.hour;
                s_sched.cycle_idx = 0;
                s_sched.sample_done_mask = 0;
                s_sched.delivery_done = 0;
                s_sched.phase = PHASE_CYCLING;
                printf("[流量触发] 进入周期循环\r\n");
            }
            break;
        }
        return;
    }

    /* CYCLING: 复用时间等比的周期逻辑 */
    /* (与sched_time_prop的CYCLING部分相同，可提取公共函数) */
    /* 此处简化：直接复制周期循环逻辑 */
    uint16_t cycle_min = g_sampling_cfg.cycle_time_min;
    uint16_t cycle_hours = cycle_min / 60;
    if (cycle_hours == 0) cycle_hours = 1;

    uint32_t cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
    uint32_t elapsed = (now_sec >= cycle_start_sec) ?
                       (now_sec - cycle_start_sec) :
                       (now_sec + 86400 - cycle_start_sec);
    uint32_t new_idx = elapsed / (cycle_min * 60);

    if (new_idx != s_sched.cycle_idx) {
        s_sched.cycle_idx = new_idx;
        s_sched.active_bucket ^= 1;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.total_cycles++;
    }

    /* 采样+送样触发（同时间等比） */
    /* ... 复用相同逻辑 ... */
}
```

**Step 2: 在 app_adc_module.c 中连接 scheduler_notify_flow**

将 `adc_module_update_flow()` 中的注释替换为实际调用：

```c
#include "app_scheduler.h"

/* 在流量开始处 */
scheduler_notify_flow(1);

/* 在流量停止处 */
scheduler_notify_flow(0);
```

**编译验证 + 提交**

---

### Task 3.3: 实现开关量触发模式 (sched_switch)

**Files:**
- Modify: `samp/middlewares/bsp/app_scheduler.c` — 替换 `sched_switch` 占位函数

**核心逻辑:**

```c
static void sched_switch(void)
{
    rtc_time_get();
    uint32_t now_sec = (uint32_t)calendar.hour * 3600 +
                       (uint32_t)calendar.min * 60 + calendar.sec;

    /* 等待首次GPIO信号 */
    if (s_sched.phase == PHASE_STARTUP && !s_sched.sw.first_trigger_done) {
        /* 读取GPIO（需要适配实际引脚） */
        extern uint8_t read_trigger_sampling_signal(void);
        if (read_trigger_sampling_signal() == 0) { /* 低电平触发 */
            s_sched.sw.first_trigger_done = 1;
            s_sched.cycle_start_hour = (calendar.hour + 2) % 24;
            s_sched.active_bucket = 0;
            s_sched.sample_done_mask = 0;
            s_sched.phase = PHASE_CYCLING;
            printf("[开关量] 首次触发，等待整点进入周期\r\n");
        }
        return;
    }

    if (s_sched.phase != PHASE_CYCLING) return;

    /* 恢复等待 */
    if (s_sched.sw.waiting_resume) {
        extern uint8_t read_trigger_sampling_signal(void);
        if (read_trigger_sampling_signal() == 0) {
            s_sched.sw.waiting_resume = 0;
            s_sched.sw.first_trigger_done = 0;
            s_sched.phase = PHASE_STARTUP;
            printf("[开关量] 信号恢复，重新启动\r\n");
        }
        return;
    }

    /* 周期循环 + 窗口检测 */
    uint16_t cycle_min = g_sampling_cfg.cycle_time_min;
    uint32_t cycle_start_sec = (uint32_t)s_sched.cycle_start_hour * 3600;
    uint32_t elapsed = (now_sec >= cycle_start_sec) ?
                       (now_sec - cycle_start_sec) :
                       (now_sec + 86400 - cycle_start_sec);
    uint32_t new_idx = elapsed / (cycle_min * 60);

    if (new_idx != s_sched.cycle_idx) {
        s_sched.cycle_idx = new_idx;
        s_sched.active_bucket ^= 1;
        s_sched.sample_done_mask = 0;
        s_sched.delivery_done = 0;
        s_sched.total_cycles++;
        memset(s_sched.sw.window_triggered, 0, sizeof(s_sched.sw.window_triggered));
    }

    /* 窗口检测采样 */
    uint8_t i;
    uint32_t cycle_base = cycle_start_sec + s_sched.cycle_idx * cycle_min * 60;
    for (i = 0; i < s_sched.sample_count; i++) {
        uint32_t mask = 1u << i;
        if (s_sched.sample_done_mask & mask) continue;

        uint32_t sample_sec = cycle_base + (uint32_t)s_sched.sample_offsets[i] * 60;
        /* ±1分钟窗口 */
        if (now_sec >= sample_sec - 60 && now_sec <= sample_sec + 60) {
            if (!s_sched.sw.window_triggered[i]) {
                extern uint8_t read_trigger_sampling_signal(void);
                if (read_trigger_sampling_signal() == 0) {
                    s_sched.sw.window_triggered[i] = 1;
                }
            }
            if (s_sched.sw.window_triggered[i] && !sampling_is_active()) {
                if (sampling_start(s_sched.active_bucket, 0)) {
                    s_sched.sample_done_mask |= mask;
                    s_sched.total_samples++;
                }
            }
        }
    }

    /* 送样触发（同时间等比） */
    /* ... */
}
```

**编译验证 + 提交**

---

### Task 3.4: 实现通信触发模式 (sched_comm)

**Files:**
- Modify: `samp/middlewares/bsp/app_scheduler.c` — 替换 `sched_comm` 占位函数

**核心逻辑:**

```c
static void sched_comm(void)
{
    if (s_sched.phase == PHASE_STARTUP) {
        s_sched.phase = PHASE_CYCLING;
    }
    if (!g_comm_trigger_req.pending) return;

    uint8_t bucket;
    uint16_t samples_per_cycle;

    switch (g_comm_trigger_req.type) {
    case COMM_REQ_SAMPLING:
        bucket = (g_comm_trigger_req.bucket == 2) ?
                 s_sched.active_bucket : g_comm_trigger_req.bucket;
        if (!sampling_is_active()) {
            if (sampling_start(bucket, 0)) {
                s_sched.total_samples++;
                /* 自动桶切换 */
                if (g_comm_trigger_req.bucket == 2 &&
                    g_sampling_cfg.cycle_time_min > 0 &&
                    g_sampling_cfg.interval_min > 0)
                {
                    samples_per_cycle = g_sampling_cfg.cycle_time_min /
                                        g_sampling_cfg.interval_min;
                    if (samples_per_cycle > 0 &&
                        (s_sched.total_samples % samples_per_cycle) == 0)
                    {
                        s_sched.active_bucket ^= 1;
                        s_sched.total_cycles++;
                    }
                }
            }
        }
        break;

    case COMM_REQ_DELIVERY:
        bucket = (g_comm_trigger_req.bucket == 2) ?
                 (1 - s_sched.active_bucket) : g_comm_trigger_req.bucket;
        {
            uint16_t water = bucket ? g_state.water_b : g_state.water_a;
            if (water > 0 && !delivery_is_active()) {
                if (delivery_start(bucket, 0)) {
                    s_sched.total_deliveries++;
                    scheduler_notify_task4_delivery(bucket);
                }
            }
        }
        break;

    case COMM_REQ_DRAIN:
        bucket = g_comm_trigger_req.bucket;
        if (bucket == 2) {
            drain_start(0);
            /* B桶排水需等A桶完成，简化处理 */
        } else {
            drain_start(bucket);
        }
        break;

    default:
        break;
    }

    g_comm_trigger_req.pending = 0;
    g_comm_trigger_req.type = COMM_REQ_NONE;
}
```

**编译验证 + 提交**

```bash
git commit -m "feat: 实现定时/流量/开关量/通信触发4种模式"
```

---

## 批次4：留样判定模块

### Task 4.1: 创建 app_retain_judge.h

**Files:**
- Create: `samp/middlewares/bsp/app_retain_judge.h`

**内容:** 留样模式定义(0-6) + 判定接口 + 执行接口 + 瓶位管理接口

参考设计文档第5节，定义：
- `RETAIN_MODE_ALARM` 到 `RETAIN_MODE_SWITCH` (0-6)
- `retain_judge_init()`, `retain_judge_commit()`, `retain_judge_reset_state()`
- `retain_judge_notify_switch()`, `retain_judge_notify_modbus()`
- `retention_execute()`, `drain_execute_blocking()`
- `retain_get_bottle_status()`, `retain_clear_all_bottles()`

---

### Task 4.2: 创建 app_retain_judge.c

**Files:**
- Create: `samp/middlewares/bsp/app_retain_judge.c`

**核心实现:**

1. 内部状态结构体（over_state[6], switch_triggered, modbus_triggered等）
2. `retain_judge_commit()` — 按Mode分发判定逻辑
3. `retain_judge_check_analog()` — 6通道边沿检测（读取 `adc_module_get_ma()`）
4. `retention_execute()` — 阻塞留样流程（阀位切换→平行留样→混样→排水）
5. `drain_execute_blocking()` — 阻塞排水

注意：`retention_execute` 和 `drain_execute_blocking` 是阻塞函数，在Task04上下文中执行，需要周期喂狗（`xEventGroupSetBits`）。

---

### Task 4.3: Task04集成留样判定

**Files:**
- Modify: `samp/project/src/freertos_app.c` (Task04函数)
- Modify: `samp/project/MDK_V5/samp.uvprojx`

**Step 1: 添加 app_retain_judge.c 到Keil工程**

**Step 2: 在Task04中添加 xTaskNotifyWait 处理**

在Task04的for循环中，Modbus处理之后添加：

```c
uint32_t notify_value = 0;
if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_value, 0) == pdTRUE) {
    if (notify_value == 0xFF) {
        drain_execute_blocking(0);
        drain_execute_blocking(1);
    } else {
        uint8_t bucket = (uint8_t)(notify_value - 1);
        handle_retain_and_drain(bucket);
    }
}
```

**Step 3: 实现 handle_retain_and_drain**

```c
static void handle_retain_and_drain(uint8_t bucket)
{
    uint32_t window_start = g_tmr2_seconds;
    uint32_t delay_sec = 20 * 60;
    uint32_t window_sec = g_sampling_cfg.analysis_time_min * 60;
    uint8_t should_retain = 0;

    while (g_tmr2_seconds - window_start < window_sec) {
        if (g_tmr2_seconds - window_start >= delay_sec) {
            if (retain_judge_commit(bucket, g_tmr2_seconds)) {
                should_retain = 1;
                break;
            }
        }
        xEventGroupSetBits(my_event01_handle, TASK04_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (should_retain) {
        retention_execute(bucket, g_tmr2_seconds);
    } else {
        drain_execute_blocking(bucket);
    }
    retain_judge_reset_state();
}
```

**编译验证 + 提交**

```bash
git commit -m "feat: 创建留样判定模块+Task04集成"
```

---

## 批次5：屏幕分发器重写 + Modbus扩展

### Task 5.1: 重写 app_screen.h（复用samplingB地址）

**Files:**
- Modify: `samp/middlewares/bsp/app_screen.h`

替换现有地址定义为samplingB地址体系（cmd_type/sub_cmd格式），添加帧缓冲区结构体。

---

### Task 5.2: 重写 app_screen.c（分发器 + 状态页回写）

**Files:**
- Modify: `samp/middlewares/bsp/app_screen.c`

**核心改造:**

1. ISR回调改为缓存完整帧（scr_frame_t环形缓冲区）
2. `screen_poll_commands()` 改为调用 `screen_dispatch()` 分发器
3. 分发器按 cmd_type/sub_cmd 路由到各 handle 函数
4. `screen_write_status_page()` 构建128字节帧回写
5. 各设置处理函数写入对应 g_xxx_cfg 字段

---

### Task 5.3: 扩展 app_modbus.c（通信触发寄存器）

**Files:**
- Modify: `samp/middlewares/bsp/app_modbus.c`

**Step 1: 在写寄存器处理中添加通信触发映射**

当写入特定holding寄存器时，设置 `g_comm_trigger_req`：

```c
/* 例：寄存器30040=采样触发, 30041=送样触发, 30042=排水触发 */
case 40: scheduler_notify_comm(COMM_REQ_SAMPLING, value >> 8, value & 0xFF); break;
case 41: scheduler_notify_comm(COMM_REQ_DELIVERY, value >> 8, 0); break;
case 42: scheduler_notify_comm(COMM_REQ_DRAIN, value >> 8, 0); break;
```

**Step 2: 在 modbus_sync_status() 中同步 ADC 模块数据**

```c
#include "app_adc_module.h"

/* 输入寄存器映射ADC通道 */
s_input_regs[10] = adc_module_get_raw(0); /* COD */
s_input_regs[11] = adc_module_get_raw(1); /* 氨氮 */
s_input_regs[12] = adc_module_get_raw(2); /* 总磷 */
s_input_regs[13] = adc_module_get_raw(3); /* 总氮 */
s_input_regs[14] = adc_module_get_raw(4); /* 流速 */
s_input_regs[15] = adc_module_get_raw(5); /* 流量 */
```

**编译验证 + 提交**

```bash
git commit -m "feat: 重写屏幕分发器+扩展Modbus通信触发"
```

---

## 批次6：集成验证

### Task 6.1: Keil工程最终检查

**Files:**
- Modify: `samp/project/MDK_V5/samp.uvprojx`

确认所有新文件已添加到工程：
- app_adc_module.c ✓ (批次1)
- app_scheduler.c ✓ (批次2)
- app_retain_judge.c ✓ (批次4)

确认include路径包含 `middlewares/bsp`。

---

### Task 6.2: 全模式编译验证

**Step 1: 编译**

Run: `"C:\Keil_v5\UV4\UV4.exe" -b D:\MCU_program3\samp\project\MDK_V5\samp.uvprojx -o build_log.txt -j0`
Expected: 0 Error(s), 0 Warning(s)

**Step 2: 检查build_log.txt**

确认无未解析符号、无类型不匹配警告。

**Step 3: 检查RAM/ROM使用量**

确认不超出AT32F435的限制（256KB RAM, 4MB Flash）。

---

### Task 6.3: 最终提交 + 推送

```bash
git add -A
git commit -m "feat: samp自动调度器全模式实现完成"
git push origin main
```

---

## 任务总览

| 批次 | 任务 | 文件 | 说明 |
|------|------|------|------|
| 1 | 1.1 | app_config.h | 扩展配置结构体 |
| 1 | 1.2 | app_config.c, app_flashdb.c | 默认值+KVDB |
| 1 | 1.3 | app_adc_module.h | AD模块头文件 |
| 1 | 1.4 | app_adc_module.c, samp.uvprojx | AD模块实现 |
| 1 | 1.5 | freertos_app.c | Task05集成 |
| 2 | 2.1 | app_scheduler.h | 调度器头文件 |
| 2 | 2.2 | app_scheduler.c | 框架+时间等比 |
| 2 | 2.3 | samp.uvprojx, freertos_app.c, app_screen.c | Task02集成 |
| 3 | 3.1 | app_scheduler.c | 定时触发模式 |
| 3 | 3.2 | app_scheduler.c, app_adc_module.c | 流量触发模式 |
| 3 | 3.3 | app_scheduler.c | 开关量触发模式 |
| 3 | 3.4 | app_scheduler.c | 通信触发模式 |
| 4 | 4.1 | app_retain_judge.h | 留样判定头文件 |
| 4 | 4.2 | app_retain_judge.c | 留样判定实现 |
| 4 | 4.3 | freertos_app.c, samp.uvprojx | Task04集成 |
| 5 | 5.1 | app_screen.h | 屏幕地址重定义 |
| 5 | 5.2 | app_screen.c | 分发器重写 |
| 5 | 5.3 | app_modbus.c | 通信触发寄存器 |
| 6 | 6.1 | samp.uvprojx | 工程最终检查 |
| 6 | 6.2 | — | 全模式编译验证 |
| 6 | 6.3 | — | 最终提交推送 |

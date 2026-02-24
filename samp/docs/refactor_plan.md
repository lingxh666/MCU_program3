# samp 工程重构实施计划

> **目标:** 将 samp 从驱动测试阶段重构为正式业务架构
> **策略:** 参考 samplingB 架构，全部重写
> **平台:** AT32F435, FreeRTOS, Keil V5

---

## 架构概览

### 任务分配（8任务）

| 任务 | 职责 | 优先级 | 栈大小 |
|------|------|--------|--------|
| Task01 | USB (CDC Device + MSC Host OTA) | osPriorityNormal | 512w |
| Task02 | 采样/送样/留样主控状态机 | osPriorityAboveNormal | 1024w |
| Task03 | 串口屏通信与命令处理 | osPriorityAboveNormal | 512w |
| Task04 | 数采仪/Modbus通信 | osPriorityNormal | 512w |
| Task05 | 4G模块通信 | osPriorityNormal | 512w |
| Task06 | CAN电机控制 + ADC监控 | osPriorityAboveNormal | 256w |
| Task07 | 系统管理 (WDT/KVDB刷写/日志) | osPriorityBelowNormal | 256w |
| Task08 | 心跳/刷卡/备用通信 | osPriorityBelowNormal | 256w |

### 拆分模块

| 文件 | 职责 | 行数估计 |
|------|------|----------|
| app_config.h/c | 系统配置结构体 + KVDB存取 | ~300 |
| app_sampling.h/c | 采样/送样/排水状态机 | ~600 |
| app_screen.h/c | 串口屏页面逻辑 | ~400 |
| app_modbus.h/c | Modbus多协议变体 | ~500 |

### IPC 机制

| 资源 | 用途 |
|------|------|
| Queue01 | 串口屏接收数据 → Task03 |
| Queue02 | 数采仪接收数据 → Task04 |
| Queue03 | 4G模块接收数据 → Task05 |
| Queue04 | CAN接收数据 → Task06 |
| Queue05 | 屏幕发送队列（Task02/04→Task03） |
| Queue06-09 | 备用 |
| Sem01 | 采样流程完成通知 |
| Sem02 | 送样流程完成通知 |
| Sem03-06 | 备用 |
| Mutex01 | KVDB访问互斥 |
| Mutex02 | 屏幕发送互斥 |
| Mutex03-08 | 备用 |
| Event01 | 任务心跳位 + 系统状态标志 |

---

## 实施批次

### Batch 1: 框架搭建 + 系统配置（基础骨架）

**目标:** 清除测试代码，搭建正式任务框架，建立配置管理模块

#### Task 1.1 — 创建 app_config.h/c（系统配置模块）

**文件:** `middlewares/bsp/app_config.h`, `middlewares/bsp/app_config.c`

定义全局配置结构体（参考 samplingB 的 g_SampleConfig / g_DeliveryConfig / g_RetainSampleConfig）：

```c
/* app_config.h */

/* 系统运行状态 */
typedef struct {
    uint8_t running;          /* 0=停止 1=运行 */
    uint8_t bucket_a_state;   /* BucketStateCode */
    uint8_t bucket_b_state;   /* BucketStateCode */
    uint8_t current_bucket;   /* 当前活跃桶 0=A 1=B */
} SystemState;

/* 采样配置（从KVDB加载） */
typedef struct {
    uint8_t  mode;            /* 1=时间等比 2=流量 3=开关量 4=直接 */
    uint16_t interval_min;    /* 采样间隔(分钟) */
    uint16_t volume_ml;       /* 单次采样量(ml) */
    uint16_t blowback_sec;    /* 反吹时长(s) */
    uint16_t improve_sec;     /* 提升时长(s) */
    uint16_t tube_hold_sec;   /* 管存时长(s) */
    uint16_t motor_rpm;       /* 采样电机转速 */
} SamplingConfig;

/* 送样配置 */
typedef struct {
    uint16_t volume_ml;       /* 送样量(ml) */
    uint16_t motor_rpm;       /* 送样电机转速 */
    uint16_t backdraw_sec;    /* 回抽时长(s) */
} DeliveryConfig;

/* 留样配置 */
typedef struct {
    uint8_t  mode;            /* 留样模式 */
    uint16_t volume_ml;       /* 留样量(ml) */
    uint8_t  bottle_count;    /* 留样瓶数(1-24) */
    uint16_t motor_rpm;       /* 留样电机转速 */
} RetainConfig;

/* KVDB 存取接口 */
void cfg_init_load(void);     /* 上电加载所有配置 */
void cfg_save_all(void);      /* 保存所有配置到KVDB */

extern SystemState        g_state;
extern SamplingConfig     g_sampling_cfg;
extern DeliveryConfig     g_delivery_cfg;
extern RetainConfig       g_retain_cfg;
```

**验证:** Keil编译 0错误0警告

#### Task 1.2 — 重构 freertos_app.c 任务框架

**修改:** `project/src/freertos_app.c`

- 删除 task02 中的 S11 测试代码
- 为每个任务填入骨架代码（初始化 + 主循环 + 心跳上报）
- 定义事件位宏（TASK_x_HEARTBEAT_BIT）
- Task01: USB + 看门狗监控（参考 samplingB task01）
- Task07: WDT喂狗 + KVDB定时刷写

```c
/* 事件位定义 */
#define TASK02_HB_BIT  (1 << 0)
#define TASK03_HB_BIT  (1 << 1)
#define TASK04_HB_BIT  (1 << 2)
#define TASK05_HB_BIT  (1 << 3)
#define TASK06_HB_BIT  (1 << 4)
#define TASK07_HB_BIT  (1 << 5)
#define TASK08_HB_BIT  (1 << 6)
#define ALL_HB_BITS    (0x7F)
```

**验证:** Keil编译 0错误0警告

#### Task 1.3 — 添加新文件到 Keil 工程

**修改:** `project/MDK_V5/samp.uvprojx`

- bsp 组添加 app_config.c
- include 路径已包含 middlewares/bsp/（无需修改）

**验证:** Keil编译 0错误0警告，全量编译通过

---

### Batch 2: 采样 + 排水流程（核心业务）

**目标:** 实现采样和排水状态机，Task02 主控调度，Task06 CAN电机控制

#### Task 2.1 — 创建 app_sampling.h（接口定义）

**文件:** `middlewares/bsp/app_sampling.h`

定义采样/送样/排水状态机接口（参考 samplingB 的 sampling.h 状态机设计）：

```c
/* 桶状态码 */
typedef enum {
    BUCKET_IDLE = 0,
    BUCKET_SAMPLING,
    BUCKET_DELIVERY,
    BUCKET_RETENTION,
    BUCKET_DRAINING,
    BUCKET_MIXING
} bucket_state_t;

/* 采样阶段 */
typedef enum {
    SAMP_IDLE = 0,
    SAMP_PRE_BLOW,        /* 前反吹 */
    SAMP_IMPROVE,         /* 外接泵提升 */
    SAMP_TUBE_HOLD,       /* 管存 */
    SAMP_MEASURE,         /* 计量采样 */
    SAMP_POST_BLOW,       /* 后反吹 */
    SAMP_DONE,
    SAMP_ABORT
} samp_stage_t;

/* 排水阶段 */
typedef enum {
    DRAIN_IDLE = 0,
    DRAIN_OPEN_VALVE,     /* 开排水阀 */
    DRAIN_MIXING,         /* 搅拌(可选) */
    DRAIN_WAIT,           /* 等待排空 */
    DRAIN_DONE
} drain_stage_t;

/* 非阻塞状态机接口 */
uint8_t sampling_start(uint8_t bucket, uint8_t is_manual);
void    sampling_step(void);       /* Task02 周期调用 */
uint8_t sampling_is_active(void);
uint8_t sampling_get_result(void); /* 0=fail 1=ok 2=abort */

uint8_t drain_start(uint8_t bucket);
void    drain_step(void);
uint8_t drain_is_active(void);
```

**验证:** Keil编译 0错误0警告

#### Task 2.2 — 实现 app_sampling.c（采样 + 排水状态机）

**文件:** `middlewares/bsp/app_sampling.c`

参考 samplingB/middlewares/bsp/sampling.c 的非阻塞状态机模式，实现采样和排水两个状态机。

**采样状态机核心结构（参考 samplingB 的 SamplingContext + _sampling_step 分发）：**

```c
/* app_sampling.c */
#include "app_sampling.h"
#include "app_config.h"
#include "bsp_io.h"
#include "bsp_can_motor.h"
#include "bsp_rtc.h"

/* 全局状态机上下文 */
static struct {
    samp_stage_t stage;
    uint8_t  bucket_id;       /* 0=A 1=B */
    uint8_t  is_manual;
    uint16_t blowback_sec;    /* 参数快照 */
    uint16_t improve_sec;
    uint16_t tube_hold_sec;
    uint16_t measure_sec;
    uint16_t rpm;
    uint32_t stage_start;     /* 阶段开始时间(秒) */
    uint32_t delay_start_ms;  /* 延时开始(ms) */
    uint8_t  result;          /* 0=fail 1=ok 2=abort */
} s_samp;

static struct {
    drain_stage_t stage;
    uint8_t  bucket_id;
    uint16_t drain_sec;       /* 排水时长 */
    uint32_t stage_start;
} s_drain;
```

**采样状态机 _sampling_step() 分发逻辑（参考 samplingB 的 switch-case 模式）：**

```c
void sampling_step(void)
{
    if (s_samp.stage == SAMP_IDLE || s_samp.stage == SAMP_DONE
        || s_samp.stage == SAMP_ABORT)
        return;

    switch (s_samp.stage) {
    case SAMP_PRE_BLOW:
        /* 启动采样电机反转(反吹)，等待 blowback_sec */
        /* 时间到 → 停电机 → 延时500ms → SAMP_IMPROVE */
        break;
    case SAMP_IMPROVE:
        /* 开外接泵(PB10)，等待 improve_sec */
        /* 时间到 → 关外接泵 → SAMP_TUBE_HOLD */
        break;
    case SAMP_TUBE_HOLD:
        /* 等待 tube_hold_sec（管存静置） */
        /* 时间到 → SAMP_MEASURE */
        break;
    case SAMP_MEASURE:
        /* 启动采样电机正转，等待 measure_sec */
        /* 时间到 → 停电机 → 延时500ms → SAMP_POST_BLOW */
        break;
    case SAMP_POST_BLOW:
        /* 启动采样电机反转(后反吹)，等待 blowback_sec */
        /* 时间到 → 停电机 → 关进水阀 → SAMP_DONE */
        break;
    default: break;
    }
}
```

**排水状态机（简化版）：**

```c
void drain_step(void)
{
    if (s_drain.stage == DRAIN_IDLE || s_drain.stage == DRAIN_DONE)
        return;

    switch (s_drain.stage) {
    case DRAIN_OPEN_VALVE:
        /* 开排水阀(A或B) → DRAIN_MIXING */
        break;
    case DRAIN_MIXING:
        /* 开搅拌电机(可选) → DRAIN_WAIT */
        break;
    case DRAIN_WAIT:
        /* 等待 drain_sec → 关阀/停搅拌 → DRAIN_DONE */
        break;
    default: break;
    }
}
```

**关键实现要点：**
- 每个阶段函数内部用 `g_tmr2_seconds` 判断超时（秒级），用 `g_tmr3_milliseconds` 判断短延时（ms级）
- 电机控制通过 `can_motor_send_run()` / `can_motor_send_stop()`，motor_id=0 为采样电机
- 阀门控制通过 `bsp_io.h` 的宏（INLET_VALVE_ON/OFF 等）
- 阶段切换时打印 printf 日志便于调试

**验证:** Keil编译 0错误0警告

#### Task 2.3 — 重构 Task02 为采样主控任务

**修改:** `project/src/freertos_app.c`

删除 task02 中的 S11 测试代码，替换为采样/送样/排水主控调度循环：

```c
void task02(void *pvParameters)
{
    /* 上电初始化：加载配置、设置初始桶状态 */
    cfg_init_load();
    vTaskDelay(pdMS_TO_TICKS(1000));  /* 等待外设就绪 */

    for (;;)
    {
        /* 1. 推进采样状态机 */
        sampling_step();

        /* 2. 推进排水状态机 */
        drain_step();

        /* 3. 周期调度逻辑（定时采样触发判断） */
        /* TODO: Batch 5 实现完整调度器 */

        /* 4. 心跳上报 */
        xEventGroupSetBits(event01_id, TASK02_HB_BIT);

        vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms轮询周期 */
    }
}
```

**要点：**
- 50ms 轮询周期，保证状态机响应及时
- 采样/排水状态机通过 `sampling_step()` / `drain_step()` 非阻塞推进
- 周期调度逻辑（定时触发采样）留到 Batch 5 实现
- 手动采样通过串口屏命令调用 `sampling_start()` 触发

**验证:** Keil编译 0错误0警告，task02 正常启动运行

#### Task 2.4 — 重构 Task06 为 CAN电机控制 + ADC监控任务

**修改:** `project/src/freertos_app.c`

```c
void task06(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;)
    {
        /* 1. 周期查询电机状态（每500ms） */
        can_motor_query_all();

        /* 2. ADC电流监控：检查各通道是否超阈值 */
        /* 阀门电流异常检测（开阀后电流为0=断线，电流过大=堵转） */

        /* 3. 冰箱温度监控（NTC通道） */

        /* 4. 心跳上报 */
        xEventGroupSetBits(event01_id, TASK06_HB_BIT);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

**验证:** Keil编译 0错误0警告

#### Task 2.5 — 添加 app_sampling.c 到 Keil 工程

**修改:** `project/MDK_V5/samp.uvprojx`

- bsp 组添加 app_sampling.c

**验证:** Keil全量编译 0错误0警告

---

### Batch 3: 串口屏通信（人机交互）

**目标:** 实现 Task03 串口屏收发、页面逻辑，支持手动操作触发采样/排水

#### Task 3.1 — 创建 app_screen.h（屏幕页面接口）

**文件:** `middlewares/bsp/app_screen.h`

```c
/* app_screen.h */
#ifndef APP_SCREEN_H
#define APP_SCREEN_H

#include <stdint.h>

/* 迪文屏变量地址定义（根据实际屏幕工程配置） */
#define SCR_ADDR_PAGE_ID        0x0084  /* 当前页面ID */
#define SCR_ADDR_SYS_STATE      0x1000  /* 系统运行状态 */
#define SCR_ADDR_BUCKET_A       0x1010  /* A桶状态 */
#define SCR_ADDR_BUCKET_B       0x1020  /* B桶状态 */
#define SCR_ADDR_SAMP_MODE      0x2000  /* 采样模式设置 */
#define SCR_ADDR_SAMP_INTERVAL  0x2002  /* 采样间隔 */
#define SCR_ADDR_SAMP_VOLUME    0x2004  /* 采样量 */
#define SCR_ADDR_CMD_MANUAL     0x3000  /* 手动操作命令 */

/* 手动命令码 */
#define SCR_CMD_SAMPLING_A      0x0001  /* 手动采样A桶 */
#define SCR_CMD_SAMPLING_B      0x0002  /* 手动采样B桶 */
#define SCR_CMD_DRAIN_A         0x0010  /* 手动排水A桶 */
#define SCR_CMD_DRAIN_B         0x0011  /* 手动排水B桶 */
#define SCR_CMD_SYS_START       0x0020  /* 系统启动 */
#define SCR_CMD_SYS_STOP        0x0021  /* 系统停止 */

/* 屏幕任务接口 */
void screen_task_init(void);
void screen_process_rx(void);       /* 解析接收数据并分发命令 */
void screen_update_status(void);    /* 周期刷新状态显示 */
void screen_handle_command(uint16_t addr, uint16_t value);

#endif
```

**验证:** Keil编译 0错误0警告

#### Task 3.2 — 实现 app_screen.c（屏幕收发与命令处理）

**文件:** `middlewares/bsp/app_screen.c`

```c
/* app_screen.c */
#include "app_screen.h"
#include "bsp_screen.h"
#include "bsp_uart.h"
#include "app_config.h"
#include "app_sampling.h"

/* 接收解析：从 Queue01 取数据，解析 5A A5 帧 */
void screen_process_rx(void)
{
    uint8_t buf[64];
    uint16_t len = bsp_screen_try_recv(buf, sizeof(buf));
    if (len == 0) return;

    /* 解析迪文帧：5A A5 [len] [cmd] [addr_h] [addr_l] [data...] */
    /* cmd=0x83 读应答 / cmd=0x82 写通知 */
    uint16_t addr = (buf[3] << 8) | buf[4];
    uint16_t value = (buf[5] << 8) | buf[6];
    screen_handle_command(addr, value);
}

/* 命令分发 */
void screen_handle_command(uint16_t addr, uint16_t value)
{
    if (addr == SCR_ADDR_CMD_MANUAL) {
        switch (value) {
        case SCR_CMD_SAMPLING_A:
            sampling_start(0, 1);  /* A桶手动采样 */
            break;
        case SCR_CMD_SAMPLING_B:
            sampling_start(1, 1);  /* B桶手动采样 */
            break;
        case SCR_CMD_DRAIN_A:
            drain_start(0);
            break;
        case SCR_CMD_DRAIN_B:
            drain_start(1);
            break;
        default: break;
        }
    }
    /* 配置参数写入处理 */
    if (addr == SCR_ADDR_SAMP_MODE) {
        g_sampling_cfg.mode = (uint8_t)value;
    }
    /* ... 其他地址处理 */
}

/* 周期刷新状态到屏幕 */
void screen_update_status(void)
{
    screen_write_var(SCR_ADDR_SYS_STATE, g_state.running);
    screen_write_var(SCR_ADDR_BUCKET_A, g_state.bucket_a_state);
    screen_write_var(SCR_ADDR_BUCKET_B, g_state.bucket_b_state);
}
```

**验证:** Keil编译 0错误0警告

#### Task 3.3 — 重构 Task03 为串口屏通信任务

**修改:** `project/src/freertos_app.c`

```c
void task03(void *pvParameters)
{
    screen_task_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;)
    {
        /* 1. 处理屏幕接收数据（从Queue01取帧并解析） */
        screen_process_rx();

        /* 2. 周期刷新状态显示（每1秒） */
        static uint32_t last_update = 0;
        if ((xTaskGetTickCount() - last_update) >= pdMS_TO_TICKS(1000))
        {
            screen_update_status();
            last_update = xTaskGetTickCount();
        }

        /* 3. 心跳上报 */
        xEventGroupSetBits(event01_id, TASK03_HB_BIT);

        vTaskDelay(pdMS_TO_TICKS(20));  /* 20ms轮询 */
    }
}
```

**验证:** Keil编译 0错误0警告

#### Task 3.4 — 添加 app_screen.c 到 Keil 工程

**修改:** `project/MDK_V5/samp.uvprojx`

- bsp 组添加 app_screen.c

**验证:** Keil全量编译 0错误0警告，屏幕通信正常

---

### Batch 4: 送样 + 留样流程

**目标:** 在 app_sampling 中补充送样状态机，实现留样转盘控制和留样流程

#### Task 4.1 — 扩展 app_sampling.h（送样 + 留样接口）

**修改:** `middlewares/bsp/app_sampling.h`

新增送样和留样状态枚举及接口：

```c
/* 送样阶段 */
typedef enum {
    DELIV_IDLE = 0,
    DELIV_PRE_BLOW,       /* 反吹清线 */
    DELIV_STABILIZE,      /* 稳定等待(2s) */
    DELIV_MIX,            /* 启动搅拌 */
    DELIV_MEASURE,        /* 计量送样 */
    DELIV_BACKDRAW,       /* 回抽 */
    DELIV_DONE,
    DELIV_ABORT
} deliv_stage_t;

/* 留样阶段 */
typedef enum {
    RETAIN_IDLE = 0,
    RETAIN_MOVE_BOTTLE,   /* 转盘定位 */
    RETAIN_PUMP,          /* 泵送留样 */
    RETAIN_DONE,
    RETAIN_ABORT
} retain_stage_t;

/* 送样接口 */
uint8_t delivery_start(uint8_t bucket, uint8_t is_manual);
void    delivery_step(void);
uint8_t delivery_is_active(void);
uint8_t delivery_get_result(void);

/* 留样接口 */
uint8_t retain_start(uint8_t bottle_target, uint8_t is_manual);
void    retain_step(void);
uint8_t retain_is_active(void);
```

**验证:** Keil编译 0错误0警告

#### Task 4.2 — 实现送样状态机（app_sampling.c 扩展）

**修改:** `middlewares/bsp/app_sampling.c`

参考 samplingB 的 DeliveryContext + _delivery_step 模式：

```c
/* 送样上下文 */
static struct {
    deliv_stage_t stage;
    uint8_t  bucket_id;
    uint8_t  is_manual;
    uint16_t blowback_sec;
    uint16_t deliver_sec;
    uint16_t backdraw_sec;
    uint16_t rpm;
    uint32_t stage_start;
    uint32_t delay_start_ms;
    uint8_t  result;
} s_deliv;

void delivery_step(void)
{
    if (s_deliv.stage == DELIV_IDLE || s_deliv.stage == DELIV_DONE
        || s_deliv.stage == DELIV_ABORT)
        return;

    switch (s_deliv.stage) {
    case DELIV_PRE_BLOW:
        /* 送样电机反转清线 → DELIV_STABILIZE */
        break;
    case DELIV_STABILIZE:
        /* 等待2秒稳定 → DELIV_MIX */
        break;
    case DELIV_MIX:
        /* 开搅拌电机 → DELIV_MEASURE */
        break;
    case DELIV_MEASURE:
        /* 送样电机正转计量 → DELIV_BACKDRAW */
        break;
    case DELIV_BACKDRAW:
        /* 送样电机反转回抽 → 关阀 → DELIV_DONE */
        break;
    default: break;
    }
}
```

**关键要点：**
- 送样电机 motor_id=1（送留样电机）
- 送样前开送留样阀（送样方向），完成后关阀
- 回抽防止管路残留

**验证:** Keil编译 0错误0警告

#### Task 4.3 — 实现留样状态机（app_sampling.c 扩展）

**修改:** `middlewares/bsp/app_sampling.c`

留样流程需要转盘定位 + 泵送，参考 samplingB 的 bottle_move_to_start/check 非阻塞模式：

```c
/* 留样上下文 */
static struct {
    retain_stage_t stage;
    uint8_t  target_bottle;   /* 目标瓶号(1-24) */
    uint8_t  is_manual;
    uint16_t volume_ml;
    uint16_t pump_sec;        /* 泵送时长 */
    uint16_t rpm;
    uint32_t stage_start;
    uint8_t  result;
} s_retain;

void retain_step(void)
{
    if (s_retain.stage == RETAIN_IDLE || s_retain.stage == RETAIN_DONE
        || s_retain.stage == RETAIN_ABORT)
        return;

    switch (s_retain.stage) {
    case RETAIN_MOVE_BOTTLE:
        /* 非阻塞转盘定位：调用 bottle_move_to_check() */
        /* 返回1=到位 → RETAIN_PUMP */
        /* 返回2=超时 → RETAIN_ABORT */
        break;
    case RETAIN_PUMP:
        /* 开送留样阀(留样方向) + 留样电机正转 */
        /* 等待 pump_sec → 停电机 → 关阀 → RETAIN_DONE */
        break;
    default: break;
    }
}
```

**关键要点：**
- 留样电机 motor_id=1（与送样共用送留样电机）
- 转盘电机 motor_id=2（留样转盘）
- STEP2和STEP3共享TMR3，互斥约束：先停送样再转盘
- 瓶位管理使用 bsp_io.h 的瓶原点/瓶到位传感器

**验证:** Keil编译 0错误0警告

#### Task 4.4 — Task02 扩展送样/留样调度

**修改:** `project/src/freertos_app.c`

在 task02 主循环中增加送样和留样状态机推进：

```c
void task02(void *pvParameters)
{
    cfg_init_load();
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        sampling_step();
        delivery_step();    /* 新增 */
        retain_step();      /* 新增 */
        drain_step();

        xEventGroupSetBits(event01_id, TASK02_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

**验证:** Keil编译 0错误0警告

---

### Batch 5: 系统管理 + 辅助任务（完善骨架）

**目标:** 完善 Task01/04/05/07/08 的业务逻辑

#### Task 5.1 — 重构 Task07 为系统管理任务

**修改:** `project/src/freertos_app.c`

```c
void task07(void *pvParameters)
{
    bsp_wdt_enable();
    vTaskDelay(pdMS_TO_TICKS(2000));

    for (;;)
    {
        /* 1. 喂狗（检查所有任务心跳） */
        EventBits_t bits = xEventGroupWaitBits(
            event01_id, ALL_HB_BITS, pdTRUE, pdTRUE,
            pdMS_TO_TICKS(5000));
        if ((bits & ALL_HB_BITS) == ALL_HB_BITS)
            bsp_wdt_feed();

        /* 2. KVDB脏数据定时刷写（每30秒） */
        static uint32_t last_flush = 0;
        if ((xTaskGetTickCount() - last_flush) >= pdMS_TO_TICKS(30000))
        {
            cfg_save_all();
            last_flush = xTaskGetTickCount();
        }

        /* 3. 心跳上报 */
        xEventGroupSetBits(event01_id, TASK07_HB_BIT);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**验证:** Keil编译 0错误0警告

#### Task 5.2 — 重构 Task04 为数采仪/Modbus通信任务

**修改:** `project/src/freertos_app.c`

```c
void task04(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    for (;;)
    {
        /* 1. 从Queue02取数采仪接收数据 */
        /* 2. Modbus协议解析与应答（待Batch 6 app_modbus实现） */

        /* 3. 心跳上报 */
        xEventGroupSetBits(event01_id, TASK04_HB_BIT);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
```

**验证:** Keil编译 0错误0警告

#### Task 5.3 — 重构 Task05/Task08 为辅助通信任务

**修改:** `project/src/freertos_app.c`

```c
/* Task05: 4G模块通信 */
void task05(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(3000));  /* 等待4G模块上电 */

    for (;;)
    {
        /* 1. 从Queue03取4G接收数据 */
        /* 2. AT指令交互（待后续实现） */

        xEventGroupSetBits(event01_id, TASK05_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* Task08: 心跳/刷卡/备用 */
void task08(void *pvParameters)
{
    bsp_wiegand_init();
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;)
    {
        /* 1. Wiegand刷卡检测 */
        uint32_t card_id;
        if (bsp_wiegand_get_card(&card_id))
        {
            printf("[刷卡] 卡号: %08X\r\n", card_id);
            /* TODO: 授权验证 + 开锁 */
        }

        xEventGroupSetBits(event01_id, TASK08_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
```

**验证:** Keil编译 0错误0警告

---

### Batch 6: Modbus 协议栈（数采仪通信）

**目标:** 实现 app_modbus 模块，支持大岳/大湖/四川/西安多协议变体

#### Task 6.1 — 创建 app_modbus.h/c（Modbus协议栈）

**文件:** `middlewares/bsp/app_modbus.h`, `middlewares/bsp/app_modbus.c`

```c
/* app_modbus.h */
#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>

/* 协议变体 */
#define PROTO_DAYUE     0   /* 大岳 */
#define PROTO_DAHU      1   /* 大湖 */
#define PROTO_SICHUAN   2   /* 四川 */
#define PROTO_XIAN      3   /* 西安 */

/* Modbus功能码 */
#define MB_FUNC_READ_HOLDING    0x03
#define MB_FUNC_READ_INPUT      0x04
#define MB_FUNC_WRITE_SINGLE    0x06
#define MB_FUNC_WRITE_MULTIPLE  0x10

/* 初始化与轮询 */
void modbus_init(uint8_t protocol, uint8_t slave_addr);
void modbus_poll(const uint8_t *frame, uint16_t len);
uint16_t modbus_get_register(uint16_t addr);
void modbus_set_register(uint16_t addr, uint16_t value);

#endif
```

**验证:** Keil编译 0错误0警告

#### Task 6.2 — 实现 Modbus 协议栈核心

**修改:** `middlewares/bsp/app_modbus.c`

```c
/* app_modbus.c */
#include "app_modbus.h"
#include "bsp_crc.h"
#include "app_config.h"

static uint8_t  s_protocol;
static uint8_t  s_slave_addr;
static uint16_t s_holding_regs[256];  /* 保持寄存器 */
static uint16_t s_input_regs[256];    /* 输入寄存器 */

void modbus_init(uint8_t protocol, uint8_t slave_addr)
{
    s_protocol = protocol;
    s_slave_addr = slave_addr;
    /* 初始化寄存器映射（从g_state/g_sampling_cfg同步） */
}

void modbus_poll(const uint8_t *frame, uint16_t len)
{
    if (len < 4) return;
    if (frame[0] != s_slave_addr) return;

    /* CRC校验 */
    uint16_t crc = bsp_crc16_modbus(frame, len - 2);
    if (crc != ((frame[len-1] << 8) | frame[len-2])) return;

    uint8_t func = frame[1];
    switch (func) {
    case MB_FUNC_READ_HOLDING:
        /* 读保持寄存器应答 */
        break;
    case MB_FUNC_READ_INPUT:
        /* 读输入寄存器应答 */
        break;
    case MB_FUNC_WRITE_SINGLE:
        /* 写单个寄存器（校时/控制命令） */
        break;
    case MB_FUNC_WRITE_MULTIPLE:
        /* 写多个寄存器 */
        break;
    default: break;
    }
}
```

**要点：**
- 从站地址通过拨码开关读取（`input_get_dip_switch()`）
- 寄存器映射根据协议变体不同（大岳用40xxx，西安用30xxx/40xxx）
- CRC16使用硬件CRC模块（bsp_crc.h）

**验证:** Keil编译 0错误0警告

#### Task 6.3 — 添加 app_modbus.c 到 Keil 工程

**修改:** `project/MDK_V5/samp.uvprojx`

- bsp 组添加 app_modbus.c

**验证:** Keil全量编译 0错误0警告

---

## 执行顺序与依赖关系

```
Batch 1 (框架+配置)
  ├── 1.1 app_config.h/c
  ├── 1.2 freertos_app.c 任务骨架
  └── 1.3 Keil工程更新
         │
Batch 2 (采样+排水) ← 依赖 Batch 1
  ├── 2.1 app_sampling.h
  ├── 2.2 app_sampling.c (采样+排水状态机)
  ├── 2.3 Task02 主控任务
  ├── 2.4 Task06 CAN+ADC任务
  └── 2.5 Keil工程更新
         │
Batch 3 (串口屏) ← 依赖 Batch 1
  ├── 3.1 app_screen.h
  ├── 3.2 app_screen.c
  ├── 3.3 Task03 屏幕任务
  └── 3.4 Keil工程更新
         │
Batch 4 (送样+留样) ← 依赖 Batch 2
  ├── 4.1 app_sampling.h 扩展
  ├── 4.2 送样状态机
  ├── 4.3 留样状态机
  └── 4.4 Task02 扩展调度
         │
Batch 5 (系统管理) ← 依赖 Batch 1
  ├── 5.1 Task07 WDT+KVDB
  ├── 5.2 Task04 Modbus骨架
  └── 5.3 Task05/08 辅助任务
         │
Batch 6 (Modbus) ← 依赖 Batch 5
  ├── 6.1 app_modbus.h
  ├── 6.2 app_modbus.c
  └── 6.3 Keil工程更新
```

**并行可能性：** Batch 2 和 Batch 3 可并行开发（无直接依赖），Batch 5 可与 Batch 4 并行。

---

## 新增文件清单

| 文件 | 类型 | 批次 |
|------|------|------|
| middlewares/bsp/app_config.h | 新建 | Batch 1 |
| middlewares/bsp/app_config.c | 新建 | Batch 1 |
| middlewares/bsp/app_sampling.h | 新建 | Batch 2 |
| middlewares/bsp/app_sampling.c | 新建 | Batch 2 |
| middlewares/bsp/app_screen.h | 新建 | Batch 3 |
| middlewares/bsp/app_screen.c | 新建 | Batch 3 |
| middlewares/bsp/app_modbus.h | 新建 | Batch 6 |
| middlewares/bsp/app_modbus.c | 新建 | Batch 6 |

## 修改文件清单

| 文件 | 修改内容 | 批次 |
|------|----------|------|
| project/src/freertos_app.c | 全部8个任务重写 | Batch 1-5 |
| project/MDK_V5/samp.uvprojx | 添加新.c文件到bsp组 | Batch 1/2/3/6 |

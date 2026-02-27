# Samp vs SamplingB 对比与验证 Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 详细比较 `samp/` 与 `samplingB/` 程序差异，并基于源码/工程配置/硬件抽象对照与 Keil 编译结果，判断 `samp/` 是否能实现与 `samplingB/` 相同功能及其风险点。

**Architecture:** 以“工程入口 → 外设驱动/中间层 → 业务功能 → 工程配置(uvprojx) → 硬件(BSP/引脚/时钟/中断)”为主线做对比。对可静态判定的问题给出结论；对必须上板验证的问题给出最小验证清单与风险评估。

**Tech Stack:** Keil MDK-ARM（`UV4.exe -b` 命令行编译），PowerShell，Git，ripgrep（如可用）。

---

### Task 1: 盘点工程入口与功能清单

**Files:**
- Inspect: `samp/`
- Inspect: `samplingB/`

**Step 1: 找到入口与主循环/任务模型**
- Run: `Get-ChildItem -Recurse -Filter *.uvprojx samp, samplingB`
- Run: `rg -n \"\\bmain\\s*\\(\" -S samp samplingB`

**Step 2: 生成 samplingB 功能清单（以模块/宏开关/外设为维度）**
- Run: `rg -n \"#define\\s+\" samplingB`
- Run: `rg -n \"(ADC|DMA|TIM|UART|SPI|I2C|USB|CAN|GPIO|EXTI|NVIC|RTC|WDT)\" -S samplingB`

---

### Task 2: 目录级差异对比（源码/头文件/资源）

**Files:**
- Compare: `samp/` vs `samplingB/`

**Step 1: 文件清单差异（新增/删除/移动）**
- Run: `git diff --no-index --name-status -- samplingB samp`

**Step 2: 关键文件内容差异（重点：BSP/时钟/中断/采样链路）**
- Run: `git diff --no-index -- samplingB samp -- \"*.c\" \"*.h\" \"*.s\" \"*.uvprojx\"`

---

### Task 3: Keil 工程配置差异（.uvprojx / include path / groups）

**Files:**
- Inspect: `samp/**/*.uvprojx`
- Inspect: `samplingB/**/*.uvprojx`

**Step 1: 对比 include path / 宏定义 / 源文件分组**
- Run: `rg -n \"<IncludePath>|<Define>|<Group>|<File>\" samp samplingB -S`

**Step 2: 如果 samp 新增/移动了源文件，确认 uvprojx 已同步**
- Evidence: `uvprojx` 中 `<FilePath>` 与磁盘实际路径一致

---

### Task 4: 硬件差异核对（BSP/引脚/时钟/外设实例）

**Files:**
- Inspect: `samp/**/bsp*` `samp/**/board*` `samp/**/pin*`（以实际为准）
- Inspect: `samplingB/**/bsp*` `samplingB/**/board*` `samplingB/**/pin*`

**Step 1: 核对 ADC/DMA/TIM 资源映射是否一致**
- Run: `rg -n \"(ADC\\d|DMA\\d|TIM\\d|Channel|Stream)\" -S samp samplingB`

**Step 2: 核对时钟树/PLL/采样频率相关计算是否一致**
- Run: `rg -n \"(SystemClock|PLL|HCLK|PCLK|APB|prescaler|sample|采样|freq|rate)\" -S samp samplingB`

---

### Task 5: Keil 命令行编译验证（0 错误 / 0 警告）

**Files:**
- Build: `samplingB/**/*.uvprojx`
- Build: `samp/**/*.uvprojx`

**Step 1: 编译 samplingB（基线）**
- Run: `& 'C:\\Keil_v5\\UV4\\UV4.exe' -b <samplingB工程路径> -o build_samplingB.txt -j0`
- Expected: 0 Error(s), 0 Warning(s)

**Step 2: 编译 samp（目标）**
- Run: `& 'C:\\Keil_v5\\UV4\\UV4.exe' -b <samp工程路径> -o build_samp.txt -j0`
- Expected: 0 Error(s), 0 Warning(s)

**Step 3: 若有告警/错误，做最小修复并复编译**
- Notes: 优先修复配置/包含路径/宏开关不一致，其次修复与硬件差异相关的外设实例/引脚配置

---

### Task 6: 输出对比报告与结论（可运行性/功能对齐/风险与验证清单）

**Deliverables:**
- 模块差异表（外设/驱动/业务/宏）
- 工程配置差异（include/define/source groups）
- 硬件差异（引脚/实例/时钟/中断）
- `samp` 覆盖 `samplingB` 功能的结论：可静态确认项 + 需上板验证项


# TIM6硬件定时器改进计划

## 1. 改进目标

将当前基于FreeRTOS任务的100ms轮询机制改为基于TIM6硬件定时器的中断驱动机制，提高时间精度和系统可靠性。

## 2. 当前问题分析

### 2.1 现有机制
```c
void task03_func(void *pvParameters)
{
    while(1) {
        scheduler_dispatcher();  // 调度检查
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms延迟
    }
}
```

### 2.2 存在的问题
1. **时间不精确**：依赖FreeRTOS任务调度，实际延迟可能超过100ms
2. **阻塞影响**：调度器执行时间会影响整体周期
3. **CPU占用**：持续轮询消耗CPU资源
4. **错过风险**：长时间阻塞可能导致错过时间点

## 3. 改进方案

### 3.1 整体架构
- **TIM6基本定时器**：配置为10ms周期中断
- **中断服务程序**：处理时间计数和事件触发
- **Task3优化**：改为处理状态机推进和非关键操作
- **标志机制**：中断中设置标志，任务中处理具体操作

### 3.2 时钟配置
- **时钟源**：APB1时钟（通常84MHz）
- **预分频器**：8400-1 = 8399
- **计数周期**：100-1 = 99
- **中断频率**：84MHz/8400/100 = 1000Hz（1ms）
  - 实际使用10ms中断（每10次触发一次）

## 4. 详细实现计划

### 4.1 TIM6初始化配置

```c
void tim6_init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 使能TIM6时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    // 配置定时器基本参数
    TIM_TimeBaseStructure.TIM_Period = 99;        // 100个计数周期
    TIM_TimeBaseStructure.TIM_Prescaler = 8399;   // 84MHz/8400 = 10kHz
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);

    // 配置中断
    NVIC_InitStructure.NVIC_IRQChannel = TIM6_DAC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 使能更新中断
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

    // 启动定时器
    TIM_Cmd(TIM6, ENABLE);
}
```

### 4.2 中断服务程序设计

```c
volatile uint32_t g_hardware_ms = 0;      // 硬件毫秒计数
volatile uint32_t g_hardware_sec = 0;      // 硬件秒计数
volatile uint8_t  g_scheduler_flag = 0;    // 调度器执行标志
volatile uint8_t  g_10ms_counter = 0;      // 10ms计数器

void TIM6_DAC_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);

        // 更新毫秒计数
        g_hardware_ms++;
        g_10ms_counter++;

        // 每100ms（10个10ms）设置调度标志
        if (g_10ms_counter >= 10)
        {
            g_10ms_counter = 0;
            g_scheduler_flag = 1;
        }

        // 每1000ms更新秒计数
        if (g_hardware_ms >= 1000)
        {
            g_hardware_ms = 0;
            g_hardware_sec++;
        }
    }
}
```

### 4.3 Task3任务优化

```c
void task03_func(void *pvParameters)
{
    // 初始化系统（保留原有初始化逻辑）
    system_init();

    // 启动TIM6定时器
    tim6_init();

    while(1)
    {
        // 检查调度标志
        if (g_scheduler_flag)
        {
            g_scheduler_flag = 0;  // 清除标志

            // 执行调度逻辑
            scheduler_dispatcher();
        }

        // 推进状态机（更快的响应）
        sampling_step_if_active();
        delivery_step_if_active();
        system_reset_update();

        // 更短延迟，提高响应性
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 4.4 时间函数适配

```c
// 替换原有的时间获取函数
uint32_t get_current_timestamp(void)
{
    return g_hardware_sec;  // 使用硬件秒计数
}

uint32_t get_millisecond_counter(void)
{
    return g_hardware_ms;   // 使用硬件毫秒计数
}
```

## 5. 实施步骤

### 第一阶段：基础实现
1. 添加TIM6初始化代码
2. 实现基本的中断服务程序
3. 保留原有Task3逻辑，并行运行
4. 添加调试输出验证定时器工作

### 第二阶段：逐步迁移
1. 修改Task3使用调度标志机制
2. 将时间获取函数改为使用硬件计数
3. 调整中断优先级，确保实时性
4. 测试时间精度

### 第三阶段：优化完善
1. 优化中断处理时间
2. 添加看门狗保护（可选）
3. 完善错误处理机制
4. 性能测试和优化

## 6. 注意事项

### 6.1 中断处理原则
- **保持简短**：中断中只做必要的操作
- **避免阻塞**：不允许在中断中调用延时函数
- **保护共享数据**：使用volatile关键字和适当的同步机制

### 6.2 优先级配置
- TIM6中断优先级应高于普通任务
- 但不能超过关键系统中断（如UART、RTC）
- 建议设置抢占优先级为5-7

### 6.3 调试支持
- 保留调试输出能力（避免在中断中printf）
- 可在中断中设置断点变量用于调试
- 添加定时器运行状态监控

## 7. 预期效果

### 7.1 性能提升
- **时间精度**：从100ms提升到10ms级
- **CPU占用**：降低约50-70%
- **响应延迟**：中断响应时间在微秒级

### 7.2 可靠性提升
- 消除任务调度延迟影响
- 避免阻塞导致的时间错过
- 更稳定的时间基准

### 7.3 扩展性
- 可轻松调整为其他时间间隔
- 支持更高精度的应用需求
- 便于添加其他定时功能

## 8. 测试计划

### 8.1 单元测试
1. TIM6定时精度测试（使用示波器或逻辑分析仪）
2. 中断响应时间测试
3. 长时间稳定性测试

### 8.2 集成测试
1. 与现有调度器系统集成测试
2. 采样送样时间准确性测试
3. 异常情况处理测试

### 8.3 性能测试
1. CPU占用率对比测试
2. 系统响应性测试
3. 功耗对比测试

## 9. 风险与对策

### 9.1 风险
1. 中断优先级冲突
2. 定时器资源占用
3. 调试困难增加

### 9.2 对策
1. 仔细设计中断优先级层次
2. 保留软件定时器作为备选
3. 完善调试和监控机制

## 10. 总结

使用TIM6硬件定时器替代软件轮询是一个系统性的改进，能够显著提升时间精度和系统可靠性。通过分阶段实施，可以平稳过渡，降低风险。改进后的系统将更加专业和可靠，满足高精度时间控制的需求。
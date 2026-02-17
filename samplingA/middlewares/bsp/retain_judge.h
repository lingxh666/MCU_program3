#ifndef __RETAIN_JUDGE_H__
#define __RETAIN_JUDGE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// 留样模式定义
//==============================================================================
// 0=超标留样 1=直接留样 2=比对留样 3=通信触发 4=同步留样 5=只送不留 6=外部节点(开关量触发)
#define RETAIN_MODE_ALARM           0  /* 超标留样（模拟量超标触发） */
#define RETAIN_MODE_DIRECT          1  /* 直接留样（每次都留样） */
#define RETAIN_MODE_COMPARE         2  /* 比对留样 */
#define RETAIN_MODE_MODBUS          3  /* 通信触发 */
#define RETAIN_MODE_SYNC            4  /* 同步留样 */
#define RETAIN_MODE_NEVER           5  /* 只送不留（每次都不留样） */
#define RETAIN_MODE_SWITCH          6  /* 外部节点（开关量触发） */

//==============================================================================
// 留样判断模块数据结构
//==============================================================================

/* 留样判断统计结构体 */
typedef struct {
    uint32_t analog_count;      // 模拟量触发次数
    uint32_t flow_count;        // 流量触发次数
    uint32_t switch_count;      // 开关量触发次数
    uint32_t modbus_count;      // 通信触发次数
} RetainJudgeStats;

//==============================================================================
// 留样判断模块函数接口
//==============================================================================

/**
 * @brief 留样判断模块初始化
 * 
 * @param windowHalfSec 判定窗口半宽（秒，暂未使用，预留）
 * 
 * 调用时机：系统启动时，在task4初始化阶段
 */
void retain_judge_init(uint16_t windowHalfSec);

/**
 * @brief 更新留样瓶号显示变量
 *
 * 更新 g_State.SampleBottle1/2/3 用于串口屏显示
 * 调用时机：初始化、瓶位变化、留样完成后
 */
void update_bottle_display(void);

/**
 * @brief 检查模拟量通道是否超标（通道0-4）
 * 
 * @param tsSec 当前时间戳（秒，RTC时间）
 * @return 触发的通道号（1-5），0表示无触发
 * 
 * 职责：
 * - 读取 g_RetainSampleConfig.channelData[0..4]
 * - 逐通道检查是否超出上下限
 * - 边沿检测：只在上升沿触发
 * - 记录留样日志
 */
uint8_t retain_judge_check_analog(uint32_t tsSec);

/**
 * @brief 检查流量通道是否触发（通道7=PA3）
 * 
 * @param tsSec 当前时间戳（秒）
 * @return 1=触发留样, 0=未触发
 */
uint8_t retain_judge_check_flow(uint32_t tsSec);

/**
 * @brief 开关量触发通知（外部调用）
 * 
 * @param level 开关量电平（0=低，1=高）
 * @param tsSec 当前时间戳（秒）
 */
void retain_judge_notify_switch(uint8_t level, uint32_t tsSec);

/**
 * @brief 通信触发通知（Modbus指令调用）
 * 
 * @param action 触发动作码（1=启动留样，2=停止留样，3=查询，4=复位）
 * @param tsSec 当前时间戳（秒）
 */
void retain_judge_notify_modbus(uint8_t action, uint32_t tsSec);

/**
 * @brief 综合留样判定（task4在判定窗口内调用）
 * 
 * @param bucket_id 待判定的桶号（0=A, 1=B）
 * @param tsSec 当前时间戳（秒）
 * @return 1=需要留样, 0=不需要留样
 * 
 * 调用时机：task4在留样判定窗口内每秒调用一次
 */
uint8_t retain_judge_commit(uint8_t bucket_id, uint32_t tsSec);

/**
 * @brief 重置判定状态（新周期开始时调用）
 */
void retain_judge_reset_state(void);

/**
 * @brief 获取当前触发统计
 * 
 * @param out_stats 输出统计结构体指针
 */
void retain_judge_get_stats(RetainJudgeStats *out_stats);

//==============================================================================
// 留样流程执行函数接口
//==============================================================================

/**
 * @brief 留样流程执行函数
 * 
 * @param bucket_id 留样桶号（0=A, 1=B）
 * @param delivery_time 对应的送样完成时间戳(用于TSDB关联查询)
 * @return 1=成功（已排空桶）, 0=失败/不留样（未排空桶，由调度器处理）
 * 
 * 特点：
 * - 支持平行留样（ParallelCount: 1-24瓶）
 * - 支持混样次数（MixCount: 1-16次）
 * - 可选加酸功能（EnableAcid）
 * - 瓶号从 bottleNumber+1 开始查找
 * - 成功后更新 bottleNumber 到KVDB
 * - 完整TSDB记录（包含留样瓶号、留样时间、送样时间）
 * - 支持紧急中断
 * - 留样成功后自动排空桶 ?
 * - 不留样时返回0，不排空桶（由调度器处理）?
 * 
 * 调用时机：task4在留样判定窗口结束后调用
 */
uint8_t retention_execute(uint8_t bucket_id, uint32_t delivery_time);

/**
 * @brief 排空桶流程执行函数
 * 
 * @param bucket_id 排空桶号（0=A, 1=B）
 * @return 1=成功, 0=失败
 * 
 * 职责：
 * - 启动排空泵
 * - 延时等待
 * - 停止排空泵
 * - 清零桶内水量
 * - 记录TSDB事件
 * 
 * 调用时机：
 * - task4在留样判定窗口结束后调用（retention_execute返回0时）
 */
uint8_t drain_execute(uint8_t bucket_id);

/**
 * @brief 清空所有瓶位（维护操作）
 * 
 * 调用时机：
 * - 手动清空操作
 * - 系统复位
 */
void retain_clear_all_bottles(void);

/**
 * @brief 设置瓶位不确定标志（手动测试留样瓶后调用）
 * 
 * @param uncertain 1=不确定（需要归零），0=确定
 * 
 * 调用时机：
 * - screen.c中手动测试留样瓶功能开始时
 * - 瓶盘归零或手动转动时
 */
void retain_set_bottle_position_uncertain(uint8_t uncertain);

//==============================================================================
// ADC双级滤波与数据采集接口
//==============================================================================

/**
 * @brief ADC采集与双级滤波处理（10ms周期调用）
 * 
 * 职责：
 * - 第一级滤波：10点去极值均值 → channelCurrent[i]
 * - 第二级滤波：100点（1秒）去10大10小的80点均值 → channelData[i]
 * - 超标/流量边沿检测与TSDB记录
 * 
 * 调用时机：task6中每10ms调用一次
 */
void retain_judge_process(void);

/**
 * @brief 判断是否需要启用UART7 485采集
 * @return 1=需要启用, 0=无需启用
 */
uint8_t retain_judge_uart7_should_run(void);

/**
 * @brief UART7 485采集数据更新（6个寄存器）
 * @param regs 6个寄存器原始值（扩大1000倍的4-20mA电流）
 * @param tsSec 当前时间戳（秒）
 */
void retain_judge_uart7_update(const uint16_t regs[6], uint32_t tsSec);

/**
 * @brief UART7 485数据无效/超时标记
 */
void retain_judge_uart7_mark_invalid(void);

//==============================================================================
// 留样瓶状态查询接口
//==============================================================================

/**
 * @brief 查询指定留样瓶的状态
 * 
 * @param bottle_number 瓶号（1-24）
 * @return 0=空瓶, 2=满瓶, 0xFF=无效瓶号
 * 
 * 调用时机：屏幕渲染、状态查询
 */
uint8_t retain_get_bottle_status(uint8_t bottle_number);

//==============================================================================
// 屏幕电流值发送功能
//==============================================================================

/**
 * @brief 发送6路通道电流值到屏幕（仅超标留样模式）
 *
 * 功能：在超标留样模式下，每秒向屏幕发送6个通道的电流值
 * 协议：5A A5 05 82 [地址] 03 [数据]
 */
void retain_send_current_values_to_screen(void);

/**
 * @brief 发送流量相关数据到串口屏（流量触发模式下5秒周期调用）
 * 
 * 发送4项数据：509E流量电流、509F流量值、50A0校准电流、50A1校准流量
 * 条件：仅在流量触发模式(SamplingMode==3)
 */
void retain_send_flow_values_to_screen(void);

/**
 * @brief 屏幕电流值发送标志位
 *
 * 定时器2中断设置，用于指示是否需要发送电流值
 * 0=不需要发送，1=需要发送
 */
extern volatile uint8_t g_retain_send_current_flag;

//==============================================================================
// 四川协议超标留样窗口检查接口
//==============================================================================

/**
 * @brief 四川协议超标留样窗口检查（周期性调用）
 * @note 在task4中周期性调用，检查窗口是否超时
 *       窗口超时时自动执行排水
 */
void sichuan_exceed_retain_check(void);

/**
 * @brief 四川协议超标留样窗口初始化（送样完成时调用）
 * @param bucket_id 桶号（0=A桶，1=B桶）
 * @note 在送样完成回调中调用，初始化超标留样窗口上下文
 */
void sichuan_init_exceed_retain_window(uint8_t bucket_id);


#ifdef __cplusplus
}
#endif

#endif /* __RETAIN_JUDGE_H__ */

#include "freertos_app.h"
#include "retain_judge.h"
#include "sampling.h"
#include "sample_id.h"
#include "Flowtrigger.h"
#include "app_flashdb.h"
#include "work.h"
#include "screen.h"
#include "at32f403a_407_rtc.h"
#include <string.h>

/* 大岳协议留样信息记录（外部定义于freertos_app.c） */
extern RetainSampleInfo_t g_LastRetainInfo;
extern DiscardSampleInfo_t g_LastDiscardInfo;

/* 屏幕电流值发送标志位 */
volatile uint8_t g_retain_send_current_flag = 0;

/* 十进制转BCD (如 30 → 0x30) */
static uint8_t dec_to_bcd_local(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}

//==============================================================================
// ADC校准配置
//==============================================================================

/**
 * @brief 2.5V基准源实际电压值（可根据实际测量微调）
 * @note 用于ADC通道校准，提高测量精度
 *       理论值：2.500V
 *       可根据万用表实测值调整，例如：2.512V
 */
#define ADC_REF_VOLTAGE (2.538f) // 单位：V，可微调（校准：16mA输入显示15.76mA，调整后更准确）

/**
 * @brief ADC供电电压（用于计算）
 */
#define ADC_SUPPLY_VOLTAGE (3.3f) // 单位：V

/**
 * @brief 2.5V基准源在ADC数组中的索引
 */
#define ADC_REF_CHANNEL_IDX (8) // adc1_ordinary_valuetab[j][8]

//==============================================================================
// 4-20mA电流检测电路参数
//==============================================================================

/**
 * @brief 采样电阻阻值
 * @note 硬件电路：JR3 = 3Ω
 *       4-20mA电流流经3Ω产生12-60mV电压
 */
#define ADC_SAMPLE_RESISTOR (3.0f) // 单位：Ω

/**
 * @brief INA180A2差分放大器增益
 * @note 硬件型号：INA180A2，增益固定为50 V/V
 *       输入：12-60mV → 输出：0.6-3.0V
 */
#define INA180_GAIN (50.0f) // 无单位

/**
 * @brief 等效电阻（用于ADC到电流的快速计算）
 * @note 等效电阻 = 采样电阻 × 放大器增益
 *       R_eq = 3Ω × 50 = 150Ω = 0.15kΩ
 *       电流计算：I(mA) = V_adc(V) / R_eq(kΩ)
 */
#define ADC_EQUIVALENT_RESISTOR_KOHM (0.15f) // 单位：kΩ (即150Ω)

#ifndef USE_UART7_AI
#define USE_UART7_AI 0
#endif

#define UART7_STABLE_COUNT 12u
#define CAL_AD_LEGACY_MAX 30u

//==============================================================================
// 内部数据结构与状态变量
//==============================================================================

/* 留样判断内部状态 */
typedef struct
{
    // 边沿检测状态（通道0-5）
    uint8_t over_state[6]; // 0=正常，1=超标
    uint8_t flow_active;   // 流量触发状态（0=低流量，1=高流量）

    // 异步触发标志（由外部设置）
    uint8_t switch_triggered; // 开关量触发标志
    uint8_t modbus_triggered; // 通信触发标志

    // 统计信息
    uint32_t analog_trigger_count; // 模拟量触发次数
    uint32_t flow_trigger_count;   // 流量触发次数
    uint32_t switch_trigger_count; // 开关量触发次数
    uint32_t modbus_trigger_count; // 通信触发次数

    // 最近一次触发信息
    uint8_t last_trigger_channel; // 最近触发的通道号（1-5，7=开关，8=通信）
    float last_trigger_value;     // 最近触发的数值
    uint32_t last_trigger_time;   // 最近触发的时间戳

    // 运行状态
    uint8_t is_initialized; // 是否已初始化

} RetainJudgeState;

static RetainJudgeState s_judge = {0};

/* 瓶位不确定标志（手动测试留样瓶后需归零） */
static uint8_t s_bottle_position_uncertain = 0;

//==============================================================================
// ADC三级滤波相关静态变量
//==============================================================================

/* 第二级滤波缓冲：9通道 x 200点 = 400ms数据（task6每2ms调用，累计200点） */
static float s_L2_buf[9][200] = {0};
static uint8_t s_L2_idx = 0; // 第二级滤波索引

/* 第三级滤波缓冲：9通道 x 30点 = 12秒数据（每400ms产生1点，累计30点） */
static float s_L3_buf[9][30] = {0};
static uint8_t s_L3_idx = 0; // 第三级滤波索引

/* 流量通道L3(60s)滤波：12个L2流量点（5s/点） */
static float s_flow_L3_60s_buf[12] = {0};
static uint8_t s_flow_L3_60s_idx = 0;
static uint8_t s_flow_L3_60s_filled = 0;
static float s_flow_L3_60s_value = 0.0f;

/* 流量触发活跃状态（0=低流量，1=高流量） */
static uint8_t g_flow_active = 0;

typedef struct
{
    uint8_t data_valid;
    uint8_t valid_mask;
    uint8_t over_pending[6];
    uint8_t over_count[6];
    uint8_t flow_pending;
    uint8_t flow_count;
    uint8_t pending_trigger;
} Uart7FilterState;

static Uart7FilterState s_uart7 = {0};

/* 外部变量声明 */
extern __IO uint16_t adc1_ordinary_valuetab[200][9]; // ADC DMA缓冲区

/* 外部函数声明 */
extern int scheduler_is_emergency_active(void);
extern uint32_t rtc_counter_get(void);
extern uint16_t calc_retain_time_by_volume(uint16_t target_ml);
extern uint8_t emptybottle(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);

/* 内部函数前向声明 */
static void retain_judge_commit_adc_1s_internal(const float chCurrent[9], uint32_t tsSec);
static uint8_t flow_l3_60s_update(float flow_l2, float *flow_l3_out);
static void retain_judge_flow_edge_update(float flow, uint32_t tsSec);
static float retain_judge_cal_current_ma(uint16_t cal_ad);
static uint16_t retain_judge_cal_current_to_screen(uint16_t cal_ad);
static int allow_analog_trigger(void);
static uint8_t retain_judge_uart7_check_analog(uint32_t tsSec);
static void retain_judge_uart7_flow_update(float flow, uint8_t flow_valid, uint32_t tsSec);
static void retain_judge_uart7_reset_pending(void);

//==============================================================================
// 内部辅助函数
//==============================================================================

static uint8_t flow_l3_60s_update(float flow_l2, float *flow_l3_out)
{
    /* 采样：写入环形缓冲 */
    s_flow_L3_60s_buf[s_flow_L3_60s_idx] = flow_l2;
    s_flow_L3_60s_idx++;
    if (s_flow_L3_60s_idx >= 12)
    {
        s_flow_L3_60s_idx = 0;
    }

    if (s_flow_L3_60s_filled < 12)
    {
        s_flow_L3_60s_filled++;
        if (s_flow_L3_60s_filled < 12)
        {
            return 0;
        }
    }

    /* 计算：12点去2大2小后取8点均值 */
    float tmp[12];
    for (int k = 0; k < 12; k++)
    {
        tmp[k] = s_flow_L3_60s_buf[k];
    }

    for (int k = 1; k < 12; k++)
    {
        float key = tmp[k];
        int t = k - 1;
        while (t >= 0 && tmp[t] > key)
        {
            tmp[t + 1] = tmp[t];
            t--;
        }
        tmp[t + 1] = key;
    }

    float sum = 0.0f;
    for (int k = 2; k < 10; k++)
    {
        sum += tmp[k];
    }

    float flow_l3 = sum / 8.0f;
    if (flow_l3 < 0.0f)
    {
        flow_l3 = 0.0f;
    }

    s_flow_L3_60s_value = flow_l3;
    if (flow_l3_out)
    {
        *flow_l3_out = flow_l3;
    }
    return 1;
}

static void retain_judge_flow_edge_update(float flow, uint32_t tsSec)
{
    uint16_t fStart = g_SampleConfig.FlowStart;
    uint16_t fStop = g_SampleConfig.FlowStop;
    // 串口屏发送的阈值扩大了10倍，使用时需除以10
    float fStartReal = (float)fStart / 10.0f;
    float fStopReal = (float)fStop / 10.0f;

    // 上升沿检测：流量从低到高
    if (!g_flow_active && flow >= fStartReal)
    {
        g_flow_active = 1u;
        struct
        {
            uint8_t code;
            float flow;
            uint16_t start;
            uint16_t stop;
        } ev = {0xB1u, flow, fStart, fStop};
        (void)tsdb_event_append(6u, &ev, sizeof(ev)); /* flow start */
        printf("[ADC滤波] 流量开始(L3-60s): %.2f >= %.1f\r\n", flow, fStartReal);

        // 通知流量触发调度器
        if (g_SampleConfig.SamplingMode == 3)
        { // 流量触发模式
            flow_trigger_notify_start(tsSec);
        }
    }
    // 下降沿检测：流量从高到低
    else if (g_flow_active && flow <= fStopReal)
    {
        g_flow_active = 0u;
        struct
        {
            uint8_t code;
            float flow;
            uint16_t start;
            uint16_t stop;
        } ev = {0xB0u, flow, fStart, fStop};
        (void)tsdb_event_append(6u, &ev, sizeof(ev)); /* flow stop */
        printf("[ADC滤波] 流量停止(L3-60s): %.2f <= %.1f\r\n", flow, fStopReal);

        // 通知流量触发调度器
        if (g_SampleConfig.SamplingMode == 3)
        { // 流量触发模式
            flow_trigger_notify_stop(tsSec);
        }
    }
}

static float retain_judge_cal_current_ma(uint16_t cal_ad)
{
    if (cal_ad == 0)
    {
        return 0.0f;
    }
    if (cal_ad <= CAL_AD_LEGACY_MAX)
    {
        return (float)cal_ad;
    }
    return (float)cal_ad / 10.0f;
}

static uint16_t retain_judge_cal_current_to_screen(uint16_t cal_ad)
{
    if (cal_ad == 0)
    {
        return 0;
    }
    if (cal_ad <= CAL_AD_LEGACY_MAX)
    {
        return (uint16_t)(cal_ad * 10u);
    }
    return cal_ad;
}

static void retain_judge_uart7_reset_pending(void)
{
    memset(s_uart7.over_pending, 0, sizeof(s_uart7.over_pending));
    memset(s_uart7.over_count, 0, sizeof(s_uart7.over_count));
    s_uart7.flow_pending = 0;
    s_uart7.flow_count = 0;
    s_uart7.pending_trigger = 0;
}

static void retain_judge_uart7_flow_update(float flow, uint8_t flow_valid, uint32_t tsSec)
{
    if (!flow_valid)
    {
        s_uart7.flow_count = 0;
        s_uart7.flow_pending = g_flow_active ? 1u : 0u;
        return;
    }

    uint16_t fStart = g_SampleConfig.FlowStart;
    uint16_t fStop = g_SampleConfig.FlowStop;
    float fStartReal = (float)fStart / 10.0f;
    float fStopReal = (float)fStop / 10.0f;

    uint8_t committed = g_flow_active ? 1u : 0u;
    uint8_t candidate = committed;
    if (!committed && flow >= fStartReal)
    {
        candidate = 1u;
    }
    else if (committed && flow <= fStopReal)
    {
        candidate = 0u;
    }

    if (candidate == committed)
    {
        s_uart7.flow_count = 0;
        s_uart7.flow_pending = committed;
        return;
    }

    if (s_uart7.flow_pending != candidate)
    {
        s_uart7.flow_pending = candidate;
        s_uart7.flow_count = 1;
    }
    else if (s_uart7.flow_count < UART7_STABLE_COUNT)
    {
        s_uart7.flow_count++;
    }

    if (s_uart7.flow_count >= UART7_STABLE_COUNT)
    {
        g_flow_active = candidate;
        s_judge.flow_active = candidate;

        if (candidate)
        {
            struct
            {
                uint8_t code;
                float flow;
                uint16_t start;
                uint16_t stop;
            } ev = {0xB1u, flow, fStart, fStop};
            (void)tsdb_event_append(6u, &ev, sizeof(ev));
            s_judge.flow_trigger_count++;
            printf("[UART7] 流量开始(60s确认): %.2f >= %.1f\r\n", flow, fStartReal);
            if (g_SampleConfig.SamplingMode == 3)
            {
                flow_trigger_notify_start(tsSec);
            }
        }
        else
        {
            struct
            {
                uint8_t code;
                float flow;
                uint16_t start;
                uint16_t stop;
            } ev = {0xB0u, flow, fStart, fStop};
            (void)tsdb_event_append(6u, &ev, sizeof(ev));
            printf("[UART7] 流量停止(60s确认): %.2f <= %.1f\r\n", flow, fStopReal);
            if (g_SampleConfig.SamplingMode == 3)
            {
                flow_trigger_notify_stop(tsSec);
            }
        }

        s_uart7.flow_count = 0;
    }
}

static uint8_t retain_judge_uart7_check_analog(uint32_t tsSec)
{
    if (!allow_analog_trigger())
    {
        retain_judge_uart7_reset_pending();
        return 0;
    }

    if (!s_uart7.data_valid)
    {
        return 0;
    }

    uint8_t triggered = 0;
    for (int i = 0; i < 6; i++)
    {
        if (!g_RetainSampleConfig.channelLimits[i].Enable)
        {
            s_uart7.over_count[i] = 0;
            if (s_uart7.pending_trigger == (uint8_t)(i + 1))
            {
                s_uart7.pending_trigger = 0;
            }
            continue;
        }
        if ((s_uart7.valid_mask & (1u << i)) == 0)
        {
            s_uart7.over_count[i] = 0;
            if (s_uart7.pending_trigger == (uint8_t)(i + 1))
            {
                s_uart7.pending_trigger = 0;
            }
            continue;
        }

        float v = g_RetainSampleConfig.channelData[i];
        float lo = g_RetainSampleConfig.channelLimits[i].LowerLimit;
        float hi = g_RetainSampleConfig.channelLimits[i].UpperLimit;
        uint8_t now_over = (uint8_t)((v < lo) || (v > hi));

        if (now_over == s_judge.over_state[i])
        {
            s_uart7.over_count[i] = 0;
            s_uart7.over_pending[i] = now_over;
            continue;
        }

        if (s_uart7.over_pending[i] != now_over)
        {
            s_uart7.over_pending[i] = now_over;
            s_uart7.over_count[i] = 1;
        }
        else if (s_uart7.over_count[i] < UART7_STABLE_COUNT)
        {
            s_uart7.over_count[i]++;
        }

        if (s_uart7.over_count[i] >= UART7_STABLE_COUNT)
        {
            if (now_over)
            {
                struct
                {
                    uint8_t code;
                    uint8_t ch;
                    float val;
                    float lo;
                    float hi;
                } ev = {0xA1u, (uint8_t)(i + 1), v, lo, hi};
                (void)tsdb_event_append(6u, &ev, sizeof(ev));
                s_judge.over_state[i] = 1u;
                s_judge.analog_trigger_count++;
                s_judge.last_trigger_channel = i + 1;
                s_judge.last_trigger_value = v;
                s_judge.last_trigger_time = tsSec;
                printf("[UART7] 通道 %d 超标(60s确认): %.2f (%.2f-%.2f)\r\n", i + 1, v, lo, hi);

                if (s_uart7.pending_trigger == 0)
                {
                    s_uart7.pending_trigger = (uint8_t)(i + 1);
                    triggered = (uint8_t)(i + 1);
                }
            }
            else
            {
                struct
                {
                    uint8_t code;
                    uint8_t ch;
                    float val;
                    float lo;
                    float hi;
                } ev = {0xA0u, (uint8_t)(i + 1), v, lo, hi};
                (void)tsdb_event_append(6u, &ev, sizeof(ev));
                s_judge.over_state[i] = 0u;
                printf("[UART7] 通道 %d 恢复正常(60s确认): %.2f\r\n", i + 1, v);
                if (s_uart7.pending_trigger == (uint8_t)(i + 1))
                {
                    s_uart7.pending_trigger = 0;
                }
            }

            s_uart7.over_count[i] = 0;
        }
    }

    return triggered;
}

/* 检查是否允许模拟量触发 */
static int allow_analog_trigger(void)
{
    switch (g_RetainSampleConfig.Mode)
    {
    case RETAIN_MODE_ALARM: // 0: 超标留样
        return 1;
    default:
        return 0;
    }
}

/* 检查是否允许开关量触发 */
static int allow_switch_trigger(void)
{
    switch (g_RetainSampleConfig.Mode)
    {
    case RETAIN_MODE_SWITCH: // 6: 开关量触发
        return 1;
    default:
        return 0;
    }
}

/* 检查是否允许通信触发 */
/* 注意：Mode=3时，work.c中直接执行留样，不需要通过此函数
 *       Mode!=3时，通过analysis_report_modbus通知，窗口内判定
 */
static int allow_modbus_trigger(void)
{
    // Mode=3时，work.c直接执行，不通过窗口判定，所以这里返回0
    // Mode!=3时，允许通过Modbus通知触发窗口判定
    if (g_RetainSampleConfig.Mode == RETAIN_MODE_MODBUS)
    {
        return 0; // Mode=3时，work.c直接执行，不通过窗口判定
    }
    return 1; // Mode!=3时，允许通过Modbus通知触发窗口判定
}

/* 参数夹持 */
static uint16_t _clamp_u16(uint16_t val, uint16_t min_val, uint16_t max_val)
{
    if (val < min_val)
        return min_val;
    if (val > max_val)
        return max_val;
    return val;
}

/* TSDB事件记录辅助函数 */
static void _retain_tsdb(uint8_t code, uint8_t bucket12, uint16_t param, uint8_t bottle)
{
    struct
    {
        uint8_t c;
        uint8_t b;
        uint16_t p;
        uint8_t n;
    } ev = {code, bucket12, param, bottle};
    (void)tsdb_event_append(3u, &ev, sizeof(ev));
}

/* 记录水量变化到TSDB */
static void _record_water_volume_change(uint8_t bucket_id, uint16_t new_volume, const char *reason)
{
    struct
    {
        uint8_t code;
        uint8_t bucket_id;
        uint16_t volume;
    } ev = {0x50u, bucket_id, new_volume};
    (void)tsdb_event_append(3u, &ev, sizeof(ev));
    printf("[水量变化] 桶_%c: %u ml (%s)\r\n",
           bucket_id ? 'B' : 'A', new_volume, reason);
}

/* 触发条件映射函数（用于大岳协议留样信息） */
static uint16_t _map_trigger_channel(uint8_t channel)
{
    switch (channel)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
        return 0x0002; // 指定时间段超标留样（通道超标）
    case 7:
        return 0x0004; // 指定时间段同步留样（开关量）
    case 8:
        return 0x0003; // 立即瞬时留样（通信触发）
    default:
        return 0x0000; // 无
    }
}

/* 样品编号生成函数（BCD格式，用于大岳协议） */
static void _fill_sample_id(uint16_t sampleId[4])
{
    // 格式: 起始年月日时分 + 结束年月日时分 (BCD编码)
    // 例: 2020120723000100 = 0x2020 0x1207 0x2300 0x0100
    uint8_t y_hi = dec_to_bcd_local(g_SystemSettingConfig.Year / 100);
    uint8_t y_lo = dec_to_bcd_local(g_SystemSettingConfig.Year % 100);
    uint8_t mon = dec_to_bcd_local(g_SystemSettingConfig.Month);
    uint8_t day = dec_to_bcd_local(g_SystemSettingConfig.Day);
    uint8_t hour = dec_to_bcd_local(g_SystemSettingConfig.Hour);
    uint8_t min = dec_to_bcd_local(g_SystemSettingConfig.Minute);

    sampleId[0] = (y_hi << 8) | y_lo;    // 年: 0x2020
    sampleId[1] = (mon << 8) | day;      // 月日: 0x1207
    sampleId[2] = (hour << 8) | min;     // 起始时分: 0x2300
    sampleId[3] = (hour << 8) | min;     // 结束时分（简化为同一时间）
}

/**
 * @brief 记录弃样信息（用于大岳协议）
 * @param bottle_number 被排空的瓶号
 * @param bottle_count 排空的瓶数量
 * @param success 是否成功 (1=成功, 0=失败)
 * @param fail_reason 失败原因码 (成功时为0)
 */
static void _record_discard_info(uint8_t bottle_number, uint8_t bottle_count,
                                  uint8_t success, uint8_t fail_reason)
{
    // 1. 填充Modbus协议数据结构
    g_LastDiscardInfo.year = g_SystemSettingConfig.Year;
    g_LastDiscardInfo.month = g_SystemSettingConfig.Month;
    g_LastDiscardInfo.day = g_SystemSettingConfig.Day;
    g_LastDiscardInfo.hour = g_SystemSettingConfig.Hour;
    g_LastDiscardInfo.minute = g_SystemSettingConfig.Minute;
    g_LastDiscardInfo.second = g_SystemSettingConfig.Second;
    g_LastDiscardInfo.result = success ? 1 : 0;
    g_LastDiscardInfo.failReason = fail_reason;
    g_LastDiscardInfo.startBottle = bottle_number;
    g_LastDiscardInfo.bottleCount = bottle_count;
    _fill_sample_id(g_LastDiscardInfo.sampleId);

    // 2. 生成样品编号并写入TSDB
    char sample_id[18];
    if (generate_sample_id(sample_id, sizeof(sample_id)))
    {
        DiscardLogRecord log_record = {0};
        memcpy(log_record.sample_id, sample_id, 18);
        log_record.discard_time = rtc_counter_get();
        log_record.bottle_number = bottle_number;
        log_record.result = success;
        log_record.error_code = fail_reason;
        log_discard_record(&log_record);
    }

    printf("[弃样] 记录：瓶%d，数量%d，结果%d\r\n",
           bottle_number, bottle_count, success);
}

// 更新瓶号显示的函数（供外部调用）
void update_bottle_display(void)
{
    // SampleBottle1: 最后一个已留样的瓶（找到已使用的最大瓶号）
    g_State.SampleBottle1 = 0;
    for (int i = BOTTLE_COUNT; i >= 1; i--)
    {
        if (is_bottle_used(g_RetainBottleState.usedMask, i))
        {
            g_State.SampleBottle1 = i;
            break;
        }
    }

    // SampleBottle2: 当前准备使用的瓶
    g_State.SampleBottle2 = g_RetainSampleConfig.bottleNumber;
    if (g_State.SampleBottle2 < 1 || g_State.SampleBottle2 > BOTTLE_COUNT)
    {
        g_State.SampleBottle2 = 1;
    }

    // SampleBottle1: 上一个使用的瓶（应比SampleBottle2少1，24为循环）
    // 如果SampleBottle2为1，则SampleBottle1应为24
    g_State.SampleBottle1 = (g_State.SampleBottle2 == 1) ? BOTTLE_COUNT : (g_State.SampleBottle2 - 1);

    // 确保SampleBottle1确实是已使用的瓶
    if (!is_bottle_used(g_RetainBottleState.usedMask, g_State.SampleBottle1))
    {
        // 如果计算出的SampleBottle1没有使用过，则查找实际使用的最后一个瓶
        g_State.SampleBottle1 = 0;
        for (int i = BOTTLE_COUNT; i >= 1; i--)
        {
            if (is_bottle_used(g_RetainBottleState.usedMask, i))
            {
                g_State.SampleBottle1 = i;
                break;
            }
        }
    }

    // SampleBottle3: 下一个空瓶
    uint8_t next_empty = 0;
    uint8_t search_start = g_State.SampleBottle2 + 1;
    if (search_start > BOTTLE_COUNT)
        search_start = 1;
    if (find_next_empty_bottle(search_start, g_RetainBottleState.usedMask, &next_empty))
    {
        g_State.SampleBottle3 = next_empty;
    }
    else
    {
        // 如果从下一个开始找不到，从头开始找
        if (find_next_empty_bottle(1, g_RetainBottleState.usedMask, &next_empty))
        {
            g_State.SampleBottle3 = next_empty;
        }
        else
        {
            g_State.SampleBottle3 = 0; // 没有空瓶
        }
    }
}

//==============================================================================
// 留样判断模块函数实现
//==============================================================================

/**
 * @brief 留样判断模块初始化
 */
void retain_judge_init(uint16_t windowHalfSec)
{
    memset(&s_judge, 0, sizeof(s_judge));
    memset(&s_uart7, 0, sizeof(s_uart7));
    s_judge.is_initialized = 1;
    s_bottle_position_uncertain = 0;

    // 初始化时更新瓶号显示，确保主页能正确显示留样瓶号
    update_bottle_display();
}

/**
 * @brief 检测模拟量通道是否超标（通道0-4）
 */
uint8_t retain_judge_check_analog(uint32_t tsSec)
{
    if (retain_judge_uart7_should_run())
    {
        if (!allow_analog_trigger())
        {
            s_uart7.pending_trigger = 0;
            return 0;
        }
        if (!s_uart7.data_valid)
        {
            return 0;
        }
        uint8_t ch = s_uart7.pending_trigger;
        s_uart7.pending_trigger = 0;
        return ch;
    }

    // ★ 调试打印：每30秒打印一次当前状态
    static uint32_t last_print_time = 0;
    //    if (tsSec - last_print_time >= 30) {  // 每30秒打印一次
    //        printf("[调试模拟量] ========== Analog Check ==========\r\n");
    //        printf("[调试模拟量] Mode=%u (0=Alarm), allow_trigger=%d\r\n",
    //               g_RetainSampleConfig.Mode, allow_analog_trigger());
    //        last_print_time = tsSec;
    //    }

    // 模式过滤
    if (!allow_analog_trigger())
    {
        return 0;
    }

    // 逐通道检查
    for (int i = 0; i < 6; i++)
    {
        if (!g_RetainSampleConfig.channelLimits[i].Enable)
        {
            continue;
        }

        float value = g_RetainSampleConfig.channelData[i];
        float lower = g_RetainSampleConfig.channelLimits[i].LowerLimit;
        float upper = g_RetainSampleConfig.channelLimits[i].UpperLimit;

        // ★ 调试打印：每5秒打印一次通道值与阈值比较
        if (tsSec - last_print_time < 1)
        { // 刚打印过标题
            //            printf("[调试模拟量]   CH%d: value=%.2f, limit=[%.2f, %.2f], ",i+1, value, lower, upper);
            uint8_t is_over = (value < lower) || (value > upper);
            //            printf("%s\r\n", is_over ? "超标" : "正常");
        }

        uint8_t now_over = (value < lower) || (value > upper);

        if (now_over && !s_judge.over_state[i])
        {
            // 上升沿触发
            s_judge.over_state[i] = 1;

            // 不在触发时记录TSDB，留样完成后会记录最终结果
            // 这样可以避免TSDB中出现误导性的失败记录

            // 更新统计
            s_judge.analog_trigger_count++;
            s_judge.last_trigger_channel = i + 1;
            s_judge.last_trigger_value = value;
            s_judge.last_trigger_time = tsSec;

            printf("[留样判定] 模拟通道%d触发：数值=%.2f，阈值=%.2f-%.2f\r\n",
                   i + 1, value, lower, upper);

            // 不在这里记录TSDB，留样完成后会记录最终结果
            printf("[留样判定] 模拟量触发，等待留样执行...\r\n");

            return (i + 1); // 返回通道号
        }
        else if (!now_over && s_judge.over_state[i])
        {
            // 下降沿恢复
            s_judge.over_state[i] = 0;
        }
    }

    return 0; // 无触发
}

/**
 * @brief 检查流量通道是否触发（通道7）
 */
uint8_t retain_judge_check_flow(uint32_t tsSec)
{
    float flow = g_RetainSampleConfig.channelData[7]; // 索引7 = 流量专用通道（PA3）
    uint16_t fStart = g_SampleConfig.FlowStart;
    uint16_t fStop = g_SampleConfig.FlowStop;
    uint8_t prev = s_judge.flow_active;

    // 串口屏发送的阈值扩大了10倍，使用时需除以10
    float fStartReal = (float)fStart / 10.0f;
    float fStopReal = (float)fStop / 10.0f;

    if (!s_judge.flow_active && flow >= fStartReal)
    {
        s_judge.flow_active = 1;
        s_judge.flow_trigger_count++;
        printf("[留样判定] 流量触发：%.2f >= %.1f\r\n", flow, fStartReal);
        // 注意：流量触发不直接触发留样，需要特殊处理
    }
    else if (s_judge.flow_active && flow <= fStopReal)
    {
        s_judge.flow_active = 0;
        printf("[留样判定] 流量停止: %.2f <= %.1f\r\n", flow, fStopReal);
    }

    (void)prev;
    return 0; // 暂不返回触发信号
}

/**
 * @brief 开关量触发通知
 */
void retain_judge_notify_switch(uint8_t level, uint32_t tsSec)
{
    // 模式过滤
    if (!allow_switch_trigger())
    {
        return;
    }

    // 设置触发标志
    s_judge.switch_triggered = 1;
    s_judge.switch_trigger_count++;
    s_judge.last_trigger_channel = 7; // 开关量
    s_judge.last_trigger_value = level ? 1.0f : 0.0f;
    s_judge.last_trigger_time = tsSec;

    // 不在触发时记录TSDB，留样完成后会记录最终结果
    // 这样可以避免TSDB中出现误导性的失败记录

    printf("[留样判定] 开关量触发：电平=%d，等待留样执行...\r\n", level);
}

/**
 * @brief 通信触发通知
 */
void retain_judge_notify_modbus(uint8_t action, uint32_t tsSec)
{
    // action=3/4 直接放行（查询/复位）
    if (action == 3 || action == 4)
    {
        return;
    }

    // 模式过滤
    if (!allow_modbus_trigger())
    {
        return;
    }

    // 设置触发标志
    s_judge.modbus_triggered = 1;
    s_judge.modbus_trigger_count++;
    s_judge.last_trigger_channel = 8; // 通信
    s_judge.last_trigger_value = (float)action;
    s_judge.last_trigger_time = tsSec;

    // 不在触发时记录TSDB，留样完成后会记录最终结果
    // 这样可以避免TSDB中出现误导性的失败记录

    printf("[留样判定] Modbus触发：动作=%d，等待留样执行...\r\n", action);
}

/**
 * @brief 综合留样判定（task4调用）
 *
 * 注意：
 * - Mode=3时，work.c中直接执行留样，不经过此函数
 * - Mode!=3时，可以通过Modbus通知触发窗口判定
 */
uint8_t retain_judge_commit(uint8_t bucket_id, uint32_t tsSec)
{
    uint8_t result = 0;

    // ★ 优先检查是否留样开关（EnableSample）
    // 如果留样功能已禁用，直接返回0（不留样）
    if (!g_RetainSampleConfig.EnableSample)
    {
        return 0; // 不留样，不打印日志（避免频繁打印）
    }

    // ★ 优先检查Modbus触发标志（Mode!=3时的窗口判定模式）
    // Mode=3时不会设置此标志（因为allow_modbus_trigger返回0）
    if (s_judge.modbus_triggered)
    {
        s_judge.modbus_triggered = 0; // 清除标志
        result = 1;
        printf("[留样判定] Modbus触发已确认（窗口判定模式）\r\n");
        return result; // Modbus触发优先级最高，直接返回
    }

    switch (g_RetainSampleConfig.Mode)
    {
    case RETAIN_MODE_ALARM: // 0：超标留样
        if (retain_judge_check_analog(tsSec))
        {
            result = 1;
        }
        break;

    case RETAIN_MODE_DIRECT: // 1：直接留样
        result = 1;          // 每次都留样
        break;

    case RETAIN_MODE_COMPARE: // 2：比对留样
        // 比对留样模式暂未实现
        printf("[留样判定] 比对留样模式暂未实现（Mode=2）\r\n");
        result = 0;
        break;

    case RETAIN_MODE_MODBUS: // 3：通信触发
        // Mode=3时，work.c直接执行，不经过窗口判定
        // 这里不应该到达（因为work.c直接执行了）
        result = 0;
        break;

    case RETAIN_MODE_SYNC: // 4：同步留样
        // 同步留样模式暂未实现
        printf("[留样判定] 同步留样模式暂未实现（Mode=4）\r\n");
        result = 0;
        break;

    case RETAIN_MODE_NEVER: // 5：只送不留
        // 只送不留模式已废弃，请使用"是否留样"设置项（EnableSample）来控制
        printf("[留样判定] 只送不留模式（Mode=5）已废弃，请在是否留样中设置（EnableSample）\r\n");
        result = 0; // 每次都不留样
        break;

    case RETAIN_MODE_SWITCH: // 6：开关量触发
        if (s_judge.switch_triggered)
        {
            s_judge.switch_triggered = 0; // 清除标志
            result = 1;
            printf("[留样判定] 开关量触发已确认\r\n");
        }
        break;

    default:
        result = 0;
        break;
    }

    (void)bucket_id; // 暂未使用
    return result;
}

/**
 * @brief 重置判定状态
 */
void retain_judge_reset_state(void)
{
    memset(s_judge.over_state, 0, sizeof(s_judge.over_state));
    s_judge.switch_triggered = 0;
    s_judge.modbus_triggered = 0;
    s_judge.flow_active = 0;
    s_uart7.data_valid = 0;
    s_uart7.valid_mask = 0;
    retain_judge_uart7_reset_pending();
}

/**
 * @brief 获取触发统计
 */
void retain_judge_get_stats(RetainJudgeStats *out_stats)
{
    if (!out_stats)
        return;
    out_stats->analog_count = s_judge.analog_trigger_count;
    out_stats->flow_count = s_judge.flow_trigger_count;
    out_stats->switch_count = s_judge.switch_trigger_count;
    out_stats->modbus_count = s_judge.modbus_trigger_count;
}

//==============================================================================
// 留样流程执行函数实现
//==============================================================================

/* 紧急中断处理 */
static uint8_t _retain_abort(uint8_t bucket_id, uint8_t stage,
                             uint8_t bottle_number, uint8_t enable_acid)
{
    // 停止所有电机
    MotorStop(2); // 送样电机

    if (enable_acid)
    {
        AcidPumpStop; // 加酸泵
    }

    if (bucket_id == 0)
    {
        MixAStop;   // A桶混合电机
        DrainAStop; // A桶排空泵
    }
    else
    {
        MixBStop;   // B桶混合电机
        DrainBStop; // B桶排空泵
    }

    // 阀位复位
    SampleThreeWayValveSample;

    //  确保瞬时阀恢复到直通状态
    InstantThreeWayValveDirect;

    // 记录TSDB中断事件
    _retain_tsdb(0xF0u, bucket_id + 1, stage, bottle_number);

    // ★ 恢复桶状态为空闲（留样中止）
    if (bucket_id == 0)
    {
        g_State.ABucketState = 0; // 空闲
        g_State.CurrentBucketRunState = 0;
    }
    else
    {
        g_State.BBucketState = 0; // 空闲
        g_State.CurrentBucketRunState = 0;
    }

    // ★ 填充大岳协议留样信息（失败情况）
    g_LastRetainInfo.year = g_SystemSettingConfig.Year;
    g_LastRetainInfo.month = g_SystemSettingConfig.Month;
    g_LastRetainInfo.day = g_SystemSettingConfig.Day;
    g_LastRetainInfo.hour = g_SystemSettingConfig.Hour;
    g_LastRetainInfo.minute = g_SystemSettingConfig.Minute;
    g_LastRetainInfo.second = g_SystemSettingConfig.Second;
    g_LastRetainInfo.result = 0;          // 失败
    g_LastRetainInfo.failReason = 0x0002; // 中断
    g_LastRetainInfo.startBottle = bottle_number;
    g_LastRetainInfo.bottleCount = 0;
    g_LastRetainInfo.volume = 0;
    g_LastRetainInfo.mode = g_RetainSampleConfig.Mode;
    g_LastRetainInfo.trigger = 0;
    g_LastRetainInfo.addAcid = enable_acid;
    g_LastRetainInfo.acidType = 0;  // 硬编码
    g_LastRetainInfo.acidRatio = 0; // 硬编码
    memset(g_LastRetainInfo.sampleId, 0, sizeof(g_LastRetainInfo.sampleId));

    printf("[留样中止] 紧急停止在阶段%d，瓶%d，桶状态已恢复为空闲\r\n",
           stage, bottle_number);

    return 0; // 表示失败
}

/**
 * @brief 排空桶流程执行函数
 */
uint8_t drain_execute(uint8_t bucket_id)
{
    uint32_t dur = g_SampleConfig.BucketDrainTime ? g_SampleConfig.BucketDrainTime : 30u;
    uint32_t t0 = g_tmr2_seconds;
    const char bucket_char = (bucket_id == 0) ? 'A' : 'B';

    printf("[排空] 排空开始：桶_%c，持续时间=%lu 秒\r\n", bucket_char, dur);

    // 启动排空泵
    if (bucket_id == 0)
    {
        DrainARun;
        g_State.DrainA = 1;
        g_State.DrainAComplete = 0;
        g_State.ABucketState = 45; // 排空中
    }
    else
    {
        DrainBRun;
        g_State.DrainB = 1;
        g_State.DrainBComplete = 0;
        g_State.BBucketState = 45; // 排空中
    }

    // 延时等待（添加喂狗防止看门狗超时）
    // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
    extern EventGroupHandle_t event_handle;
    while ((g_tmr2_seconds - t0) < dur)
    {
        vTaskDelay(pdMS_TO_TICKS(200));
        xEventGroupSetBits(event_handle, (1 << 2)); // 设置TASK4事件位
    }

    // 停止排空泵
    if (bucket_id == 0)
    {
        DrainAStop;
        g_State.DrainA = 0;
        g_State.SaveWarterA = 0;
        g_State.ABucketState = 0;          // 空闲
        g_State.CurrentBucketRunState = 0; // 褰撳墠妗惰繍琛岀姸鎬侊細寰呮満涓?
        g_State.DrainAComplete = 1;
        _record_water_volume_change(0, 0, "DRAIN");
    }
    else
    {
        DrainBStop;
        g_State.DrainB = 0;
        g_State.SaveWarterB = 0;
        g_State.BBucketState = 0;          // 空闲
        g_State.CurrentBucketRunState = 0; // 褰撳墠妗惰繍琛岀姸鎬侊細寰呮満涓?
        g_State.DrainBComplete = 1;
        _record_water_volume_change(1, 0, "DRAIN");
    }

    printf("[排空] 排空完成：桶_%c\r\n", bucket_char);
    return 1;
}

/**
 * @brief 留样流程执行函数
 */
uint8_t retention_execute(uint8_t bucket_id, uint32_t delivery_time)
{
    const char bucket_char = (bucket_id == 0) ? 'A' : 'B';
    uint8_t bucket12 = (uint8_t)(bucket_id == 0 ? 1 : 2);

    printf("[留样] 开始：桶_%c，送样时间=%lu\r\n", bucket_char, delivery_time);

    //==========================================================================
    // ★ 阶段-2：首次留样时初始化留样瓶系统
    //==========================================================================
    if (!bottle_ensure_initialized())
    {
        printf("[留样] 留样瓶初始化失败，跳过留样\r\n");
        return 0;
    }

    //==========================================================================
    // ★ 阶段-1：留样瓶故障检查
    //==========================================================================
    if (bottle_is_fault_active())
    {
        uint8_t fault_code = bottle_get_fault();
        printf("[留样] 留样瓶系统故障(0x%02X)，跳过留样\r\n", fault_code);
        // 故障时跳过留样，不排空桶，由调度器处理
        return 0;
    }

    //==========================================================================
    // 阶段0：互斥检查 - 等待采样和送样都完成
    //==========================================================================

    // ?? 重要：采样和送样可以同时进行（边采边送），但留样必须等待两者都完成

    uint8_t sampling_status = sampling_get_status();
    uint8_t delivery_status = delivery_get_status();

    if (sampling_status == 1 || delivery_status == 1)
    {
        printf("[留样] 等待操作完成（采样=%d，送样=%d）...\r\n",
               sampling_status, delivery_status);

        uint32_t wait_start = g_tmr2_seconds;
        const uint32_t WAIT_TIMEOUT_SEC = 300; // ★ 超时时间：300秒（5分钟）

        while (1)
        {
            sampling_status = sampling_get_status();
            delivery_status = delivery_get_status();

            // 两者都不在运行中才退出
            if (sampling_status != 1 && delivery_status != 1)
            {
                break;
            }

            // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
            extern EventGroupHandle_t event_handle;
            xEventGroupSetBits(event_handle, (1 << 2));

            vTaskDelay(pdMS_TO_TICKS(100));

            uint32_t wait_time = g_tmr2_seconds - wait_start;

            // ★ 超时检查：防止无限等待导致系统卡死
            if (wait_time >= WAIT_TIMEOUT_SEC)
            {
                printf("[留样] 警告：等待超时（%lu秒），强制退出！采样=%d，送样=%d\r\n",
                       wait_time, sampling_status, delivery_status);
                break;
            }

            // 每10秒打印一次等待日志
            if (wait_time > 0 && wait_time % 10 == 0)
            {
                static uint32_t last_print = 0;
                if (wait_time != last_print)
                {
                    printf("[留样] 仍在等待...采样=%d，送样=%d（%lu 秒）\r\n",
                           sampling_status, delivery_status, wait_time);
                    last_print = wait_time;
                }
            }
        }

        uint32_t total_wait = g_tmr2_seconds - wait_start;
        printf("[留样] 操作已完成，等待%lu 秒\r\n", total_wait);
    }

    //==========================================================================
    // 阶段1：判断是否留样
    //==========================================================================

    // 检查 EnableSample
    if (!g_RetainSampleConfig.EnableSample)
    {
        printf("[留样] 留样已禁用（EnableSample=0），返回不排空\r\n");
        return 0; // 不留样，不排空桶，由调度器处理
    }

    // 根据Mode判断
    if (g_RetainSampleConfig.Mode == RETAIN_MODE_NEVER)
    {
        // 只送不留模式已废弃，请使用"是否留样"设置项（EnableSample）来控制
        printf("[留样] 只送不留模式（Mode=5）已废弃，请在是否留样中设置（EnableSample），返回不排空\r\n");
        return 0; // 不留样，不排空桶，由调度器处理
    }

    // 解析配置参数
    uint8_t parallel_count = g_RetainSampleConfig.ParallelCount;
    uint8_t mix_count = g_RetainSampleConfig.MixCount;
    uint16_t sample_volume = g_RetainSampleConfig.SampleVolume;
    uint16_t tube_hold_time = _clamp_u16(g_RetainSampleConfig.TubeHoldTime, 0, 36000);
    uint16_t blowback_time = _clamp_u16(g_RetainSampleConfig.BlowbackTime, 0, 36000);
    uint16_t backdraw_time = _clamp_u16(g_RetainSampleConfig.BackdrawTime, 0, 36000);
    uint8_t enable_acid = g_RetainSampleConfig.EnableAcid ? 1 : 0;
    uint16_t rpmR1 = g_SystemSettingConfig.Motorspeed;

    // 参数夹持
    if (parallel_count == 0)
        parallel_count = 1;
    if (parallel_count > BOTTLE_COUNT)
        parallel_count = BOTTLE_COUNT;
    if (mix_count == 0)
        mix_count = 1;
    if (mix_count > 16)
        mix_count = 16;

    printf("[留样] 配置：并行=%d，混样=%d，体积=%u，加酸=%d\r\n",
           parallel_count, mix_count, sample_volume, enable_acid);

    //==========================================================================
    // 阶段2/3：阀位切换
    //==========================================================================

    // 选择出水桶
    if (bucket_id == 0)
    {
        OutletThreeWayValveA();
        g_State.OutletThreeWayValve = 0;
        printf("[留样] 出水阀 -> 桶-A\r\n");
    }
    else
    {
        OutletThreeWayValveB();
        g_State.OutletThreeWayValve = 1;
        printf("[留样] 出水阀 -> 桶-B\r\n");
    }

    // 留样三通阀切瓶
    SampleThreeWayValveSTAY;
    g_State.SampleThreeWayValve = 1; // 留样
    printf("[留样] 采样阀 -> 留样\r\n");

    // 更新上位机状态码：留样开始
    update_bucket_state(bucket_id, BUCKET_STATE_RETENTION);
    update_system_running_state(1);

    // ★ 设置桶状态为留样中（20），阻止采样在留样期间启动
    if (bucket_id == 0)
    {
        g_State.ABucketState = 38; // 留样中
        g_State.ABucketCountDown[0] = 0;
        g_State.ABucketCountDown[1] = 0;
        g_State.ABucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
    }
    else
    {
        g_State.BBucketState = 38; // 留样中
        g_State.BBucketCountDown[0] = 0;
        g_State.BBucketCountDown[1] = 0;
        g_State.BBucketCountDown[2] = 0;
        g_State.CurrentBucketCountDown[0] = 0;
        g_State.CurrentBucketCountDown[1] = 0;
        g_State.CurrentBucketCountDown[2] = 0;
    }
    printf("[留样] 桶_%c 状态设置为留样中（20）\r\n", bucket_char);

    //==========================================================================
    // 平行留样循环
    //==========================================================================

    // ★ 修改:留样完成后已经移动到下一个空瓶,所以这里直接使用bottleNumber,不再+1
    // bottleNumber存储的是"当前准备好的空瓶号"(上次留样完成后已移动到位)
    uint8_t start_bottle = g_RetainSampleConfig.bottleNumber;
    if (start_bottle < 1 || start_bottle > BOTTLE_COUNT)
        start_bottle = 1; // 防御性检查
    uint8_t target_bottle = 0;
    uint8_t last_success_bottle = 0;

    for (uint8_t parallel_idx = 0; parallel_idx < parallel_count; parallel_idx++)
    {
        // 直接使用准备好的空瓶号（根据设计，bottleNumber存储的是上次留样完成后已移动到位的空瓶号）
        target_bottle = start_bottle;
        printf("[留样] 使用准备好的空瓶%d\r\n", target_bottle);
        update_bottle_display(); // 更新瓶号显示
        //======================================================================
        // 阶段2：瓶位移动
        //======================================================================

        // 检查瓶位是否不确定（手动测试后需归零）
        if (s_bottle_position_uncertain)
        {
            printf("[留样] 瓶位不确定，先归零\r\n");
            bottle_home_to_1(100, 15000);    // 归零
            vTaskDelay(pdMS_TO_TICKS(2000)); // 等待归零完成
            s_bottle_position_uncertain = 0; // 清除标志
        }

        // 瓶位已在正确位置（上次留样完成后已移动到位），无需再次移动
        g_current_bottle_number = target_bottle;
        printf("[留样] 瓶位已在正确位置%d\r\n", target_bottle);

        // 瓶级开始
        _retain_tsdb(0x10u, bucket12, 0, target_bottle);

        //======================================================================
        // 阶段3：阀位切换稳定延时（非阻塞10秒）
        //======================================================================

        printf("[留样] 等待送留样阀切换稳定（10秒）\r\n");
        vTaskDelay(pdMS_TO_TICKS(10000)); // 非阻塞延时10秒
        printf("[留样] 送留样阀切换稳定完成\r\n");

        //======================================================================
        // 混样循环
        //======================================================================

        for (uint8_t mix_idx = 0; mix_idx < mix_count; mix_idx++)
        {
            printf("[留样] 混样周期%d/%d，瓶%d\r\n",
                   mix_idx + 1, mix_count, target_bottle);

            //==================================================================
            // 阶段5：反吹清线（? 瞬时阀切换到瞬时状态）
            //==================================================================

            // ??? 关键：反吹阶段必须将瞬时阀切换到瞬时状态
            InstantThreeWayValveInstant;
            printf("[留样] 瞬时阀 -> 瞬时（用于反吹）\r\n");

            uint32_t t0 = g_tmr2_seconds;
            _retain_tsdb(0x11u, bucket12, blowback_time, target_bottle);

            MotorRun(2, 0, rpmR1); // 反转
            g_State.DeliveryMotor = 2;
            printf("[留样] 反吹开始：%u 秒\r\n", blowback_time);

            while ((g_tmr2_seconds - t0) < blowback_time)
            {
                if (scheduler_is_emergency_active())
                {
                    MotorStop(2);
                    return _retain_abort(bucket_id, 1, target_bottle, enable_acid);
                }
                // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
                extern EventGroupHandle_t event_handle;
                xEventGroupSetBits(event_handle, (1 << 2));
                vTaskDelay(pdMS_TO_TICKS(200));
                MotorRun(2, 0, rpmR1);
            }

            MotorStop(2);
            g_State.DeliveryMotor = 0;
            _retain_tsdb(0x12u, bucket12, (uint16_t)(g_tmr2_seconds - t0), target_bottle);

            // ??? 关键：反吹结束，瞬时阀恢复到直通状态
            InstantThreeWayValveDirect;
            printf("[留样] 瞬时阀 -> 直通（反吹后）\r\n");

            vTaskDelay(pdMS_TO_TICKS(2000)); // 延时2秒

            //==================================================================
            // 阶段5：启动混合电机
            //==================================================================

            if (bucket_id == 0)
            {
                MixARun;
                printf("[留样] 混合-A 运行\r\n");
            }
            else
            {
                MixBRun;
                printf("[留样] 混合-B 运行\r\n");
            }

            //==================================================================
            // 阶段6：管存阶段
            //==================================================================

            t0 = g_tmr2_seconds;
            _retain_tsdb(0x15u, bucket12, tube_hold_time, target_bottle);

            MotorRun(2, 1, rpmR1); // 正转
            g_State.DeliveryMotor = 1;
            printf("[留样] 管存开始：%u 秒\r\n", tube_hold_time);

            while ((g_tmr2_seconds - t0) < tube_hold_time)
            {
                if (scheduler_is_emergency_active())
                {
                    MotorStop(2);
                    return _retain_abort(bucket_id, 2, target_bottle, enable_acid);
                }
                // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
                extern EventGroupHandle_t event_handle;
                xEventGroupSetBits(event_handle, (1 << 2));
                vTaskDelay(pdMS_TO_TICKS(200));
                MotorRun(2, 1, rpmR1);
            }

            _retain_tsdb(0x16u, bucket12, (uint16_t)(g_tmr2_seconds - t0), target_bottle);

            //==================================================================
            // 阶段7：留样 + 加酸
            //==================================================================

            // 启动加酸泵（如果启用）
            if (enable_acid)
            {
                AcidPumpRun;
                printf("[留样] 加酸泵 运行\r\n");
            }

            // 计算留样时间
            uint16_t retain_time = calc_retain_time_by_volume(sample_volume);
            if (retain_time == 0)
            {
                if (enable_acid)
                    AcidPumpStop;
                printf("[留样] 计算留样时间失败\r\n");
                return _retain_abort(bucket_id, 3, target_bottle, enable_acid);
            }

            t0 = g_tmr2_seconds;
            _retain_tsdb(0x17u, bucket12, retain_time, target_bottle);

            MotorRun(2, 1, rpmR1); // 继续正转
            g_State.DeliveryMotor = 1;
            printf("[留样] 计量开始：%u 秒，体积=%u ml\r\n",
                   retain_time, sample_volume);

            while ((g_tmr2_seconds - t0) < retain_time)
            {
                if (scheduler_is_emergency_active())
                {
                    MotorStop(2);
                    if (enable_acid)
                        AcidPumpStop;
                    return _retain_abort(bucket_id, 3, target_bottle, enable_acid);
                }
                // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
                extern EventGroupHandle_t event_handle;
                xEventGroupSetBits(event_handle, (1 << 2));
                vTaskDelay(pdMS_TO_TICKS(200));
                MotorRun(2, 1, rpmR1);
            }

            _retain_tsdb(0x18u, bucket12, (uint16_t)(g_tmr2_seconds - t0), target_bottle);

            // 停止加酸泵
            if (enable_acid)
            {
                AcidPumpStop;
                printf("[留样] 加酸泵 停止\r\n");
            }

            // 更新桶内水量（减少）
            if (bucket_id == 0)
            {
                g_State.SaveWarterA = (g_State.SaveWarterA > sample_volume) ? (g_State.SaveWarterA - sample_volume) : 0;
                _record_water_volume_change(0, g_State.SaveWarterA, "RETENTION");
            }
            else
            {
                g_State.SaveWarterB = (g_State.SaveWarterB > sample_volume) ? (g_State.SaveWarterB - sample_volume) : 0;
                _record_water_volume_change(1, g_State.SaveWarterB, "RETENTION");
            }

            //==================================================================
            // 阶段8：反抽清理（? 瞬时阀切换到瞬时状态）
            //==================================================================

            // 关键：反抽阶段必须将瞬时阀切换到瞬时状态
            InstantThreeWayValveInstant;
            printf("[留样] 瞬时阀 -> 瞬时（用于反抽）\r\n");

            t0 = g_tmr2_seconds;
            _retain_tsdb(0x19u, bucket12, backdraw_time, target_bottle);

            vTaskDelay(500);
            MotorRun(2, 0, rpmR1); // 反转
            g_State.DeliveryMotor = 2;
            printf("[留样] 反抽开始：%u 秒\r\n", backdraw_time);

            while ((g_tmr2_seconds - t0) < backdraw_time)
            {
                if (scheduler_is_emergency_active())
                {
                    MotorStop(2);
                    return _retain_abort(bucket_id, 4, target_bottle, enable_acid);
                }
                // ★ 喂狗:防止TASK4长时间阻塞导致WDT超时 (TASK4_EVENT_BIT = 1 << 2)
                extern EventGroupHandle_t event_handle;
                xEventGroupSetBits(event_handle, (1 << 2));
                vTaskDelay(pdMS_TO_TICKS(200));
                MotorRun(2, 0, rpmR1);
            }

            MotorStop(2);
            g_State.DeliveryMotor = 0;
            _retain_tsdb(0x1Au, bucket12, (uint16_t)(g_tmr2_seconds - t0), target_bottle);

            // ??? 关键：反抽结束，瞬时阀恢复到直通状态
            InstantThreeWayValveDirect;
            printf("[留样] 瞬时阀 -> 直通（反抽后）\r\n");

            // 阀位复位到采样路
            SampleThreeWayValveSample;
            g_State.SampleThreeWayValve = 0; // 送样
            printf("[留样] 采样阀 -> 采样\r\n");

            // 混样间隔
            if (mix_idx < (mix_count - 1))
            {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }

        // 停止混合电机
        if (bucket_id == 0)
        {
            MixAStop;
            printf("[留样] 混合-A 停止\r\n");
        }
        else
        {
            MixBStop;
            printf("[留样] 混合-B 停止\r\n");
        }

        // 瓶级结束
        _retain_tsdb(0x1Fu, bucket12, 1, target_bottle);

        // 更新瓶位状态
        mark_bottle_used(&g_RetainBottleState.usedMask, target_bottle);
        last_success_bottle = target_bottle;

        printf("[留样] 瓶%d已完成\r\n", target_bottle);

        // 准备下一个平行瓶
        start_bottle = target_bottle + 1;
        if (start_bottle > BOTTLE_COUNT)
            start_bottle = 1;
    }

    //==========================================================================
    // 阶段10：留样成功处理 - 记录TSDB + 排空桶 + 记录KVDB
    //==========================================================================

    printf("[留样] 留样已完成，正在记录到TSDB...\r\n");
    update_bottle_display(); // 更新瓶号显示

    // 记录留样成功日志到TSDB
    // 从桶上下文继承sample_id
    extern WaterSampleContext g_water_ctx_A, g_water_ctx_B;
    WaterSampleContext *water_ctx = (bucket_id == 0) ? &g_water_ctx_A : &g_water_ctx_B;

    RetainLogRecord retain_log = {0};

    // 继承sample_id
    if (water_ctx->valid)
    {
        strcpy(retain_log.sample_id, water_ctx->sample_id);
        printf("[留样] 继承sample_id=%s（桶%c）\r\n", water_ctx->sample_id, bucket_char);

        // 验证时间窗口：留样应该在送样完成后的合理时间内
        uint32_t now = rtc_counter_get();
        uint32_t elapsed_since_delivery = now - water_ctx->delivery_complete_time;
        if (elapsed_since_delivery > 3600)
        {
            printf("[留样] 警告：送样已完成%lu秒，可能不是同一份水\r\n", elapsed_since_delivery);
        }
    }
    else
    {
        printf("[留样] 警告：桶%c的sample_id无效，留样记录将缺失sample_id\r\n", bucket_char);
        retain_log.sample_id[0] = '\0';
    }

    retain_log.retain_mode = g_RetainSampleConfig.Mode;
    // 记录实际的触发原因：1-6=模拟量通道，7=开关量，8=通信，0=其他
    retain_log.retain_reason = (s_judge.last_trigger_channel <= 8) ? s_judge.last_trigger_channel : 0;
    retain_log.start_time = rtc_counter_get();                      // 留样开始时间
    retain_log.end_time = rtc_counter_get();                        // 留样结束时间
    retain_log.delivery_time = delivery_time;                              // 对应的送样时间
    retain_log.retain_volume = sample_volume * parallel_count * mix_count; // 总留样量
    retain_log.bottle_number = last_success_bottle;                        // 留样瓶号
    retain_log.trigger_value = s_judge.last_trigger_value;                 // 记录触发值
    retain_log.result = 1; // 成功
    retain_log.error_code = 0;
    retain_log.acid_added = enable_acid;  // 记录是否加酸

    log_retain_record(&retain_log);

    // 添加到缓存
    extern void cache_add_retain(const RetainLogRecord *record);
    cache_add_retain(&retain_log);

    printf("[留样] TSDB已记录：瓶=%d，体积=%d，送样时间=%lu\r\n",
           last_success_bottle, retain_log.retain_volume, delivery_time);

    // ★ 发送留样瓶号到串口屏 (5A A5 05 82 52 3B 00 XX, XX=0x01-0x18表示1-24号瓶)
    {
        uint8_t bottle_screen_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x3B, 0x00, 0x00};
        bottle_screen_buf[7] = (uint8_t)last_success_bottle;  // 瓶号1-24对应0x01-0x18
        screen_send_notify(USART_SCREEN, bottle_screen_buf, 8, 3);
        printf("[留样] 已发送瓶号%d到串口屏\r\n", last_success_bottle);
    }

    // ★ 填充大岳协议留样信息（成功情况）
    g_LastRetainInfo.year = g_SystemSettingConfig.Year;
    g_LastRetainInfo.month = g_SystemSettingConfig.Month;
    g_LastRetainInfo.day = g_SystemSettingConfig.Day;
    g_LastRetainInfo.hour = g_SystemSettingConfig.Hour;
    g_LastRetainInfo.minute = g_SystemSettingConfig.Minute;
    g_LastRetainInfo.second = g_SystemSettingConfig.Second;
    g_LastRetainInfo.result = 1; // 成功
    g_LastRetainInfo.failReason = 0;
    g_LastRetainInfo.startBottle = start_bottle;
    g_LastRetainInfo.bottleCount = parallel_count;
    g_LastRetainInfo.volume = sample_volume;
    g_LastRetainInfo.mode = g_RetainSampleConfig.Mode;
    g_LastRetainInfo.trigger = _map_trigger_channel(s_judge.last_trigger_channel);
    g_LastRetainInfo.addAcid = enable_acid;
    g_LastRetainInfo.acidType = 0;  // 硬编码
    g_LastRetainInfo.acidRatio = 0; // 硬编码
    _fill_sample_id(g_LastRetainInfo.sampleId);

    // 更新西安协议留样日志
    extern XianRetainLog_t g_XianRetainLog;

    g_XianRetainLog.mode = g_LastRetainInfo.mode;
    g_XianRetainLog.reason = g_LastRetainInfo.trigger; // 触发原因
    g_XianRetainLog.year = g_LastRetainInfo.year;
    g_XianRetainLog.month = g_LastRetainInfo.month;
    g_XianRetainLog.day = g_LastRetainInfo.day;
    g_XianRetainLog.hour = g_LastRetainInfo.hour;
    g_XianRetainLog.minute = g_LastRetainInfo.minute;
    g_XianRetainLog.bottleId = last_success_bottle;
    g_XianRetainLog.volume = g_LastRetainInfo.volume;
    g_XianRetainLog.result = 1; // 1=成功

    printf("[留样] 大岳协议留样信息已更新：瓶%d，模式%d，体积%d\r\n",
           g_LastRetainInfo.startBottle, g_LastRetainInfo.mode, g_LastRetainInfo.volume);

    printf("[留样] 正在排空桶_%c\r\n", bucket_char);

    // 排空AB桶（留样成功后无条件执行）
    drain_execute(bucket_id);

    // 留样完成后寻找下一个空瓶并转位
    if (last_success_bottle > 0)
    {
        // 检查是否是24号瓶留样完成
        if (last_success_bottle == 24)
        {
            if (g_RetainSampleConfig.EnableVacuum == 0)
            {
                // EnableVacuum=0：24号瓶是最后一个留样瓶，留样完成后停止
                g_RetainSampleConfig.EnableSample = 0;  // 禁用留样功能
                cfg_save_retain(&g_RetainSampleConfig);
                printf("[留样] 24号瓶留样完成，EnableVacuum=0，已禁用留样功能\r\n");
            }
            else if (g_RetainSampleConfig.EnableVacuum == 1)
            {
                // EnableVacuum=1：下一个留样瓶是1号瓶，需要先进行瓶排空
                printf("[留样] 24号瓶留样完成，EnableVacuum=1，准备瓶排空后从1号瓶开始\r\n");

                // 先排空1号瓶
                printf("[留样] 正在排空1号瓶...\r\n");
                if (emptybottle(1, 40, 5000))
                {
                    printf("[留样] 1号瓶排空完成\r\n");
                    // ★ 关键修复：排空成功后清除usedMask中1号瓶的标记
                    clear_bottle_used(&g_RetainBottleState.usedMask, 1);
                    printf("[留样] 已清除1号瓶的使用标记，可重新使用\r\n");
                    // 记录弃样信息
                    _record_discard_info(1, 1, 1, 0);
                }
                else
                {
                    printf("[留样] 1号瓶排空失败\r\n");
                    // 记录弃样失败信息
                    _record_discard_info(1, 1, 0, 0x0001);
                }

                // 设置下一个瓶号为1
                g_RetainSampleConfig.bottleNumber = 1;
            }
        }

        // 只有在留样功能启用的情况下才移动到下一个瓶
        if (g_RetainSampleConfig.EnableSample || (last_success_bottle == 24 && g_RetainSampleConfig.EnableVacuum == 1))
        {
            uint8_t next_start = last_success_bottle + 1;
            if (next_start > BOTTLE_COUNT)
                next_start = 1;
            uint8_t next_bottle = 0;

            // 如果是24号瓶且EnableVacuum=1，直接从1号瓶开始（已在上面排空）
            if (last_success_bottle == 24 && g_RetainSampleConfig.EnableVacuum == 1)
            {
                next_bottle = 1;
            }
            else if (find_next_empty_bottle(next_start, g_RetainBottleState.usedMask, &next_bottle))
            {
                // 正常查找下一个空瓶
            }
            else
            {
                // ★ 关键修复：EnableVacuum=1时，如果找不到空瓶，排空下一个瓶
                if (g_RetainSampleConfig.EnableVacuum == 1)
                {
                    next_bottle = next_start; // 使用下一个瓶号
                    printf("[留样] 无空瓶，EnableVacuum=1，准备排空%d号瓶\r\n", next_bottle);

                    // 排空下一个瓶
                    if (emptybottle(next_bottle, 40, 5000))
                    {
                        printf("[留样] %d号瓶排空完成\r\n", next_bottle);
                        // 排空成功后清除usedMask中该瓶的标记
                        clear_bottle_used(&g_RetainBottleState.usedMask, next_bottle);
                        printf("[留样] 已清除%d号瓶的使用标记，可重新使用\r\n", next_bottle);
                        // 记录弃样信息
                        _record_discard_info(next_bottle, 1, 1, 0);
                    }
                    else
                    {
                        printf("[留样] %d号瓶排空失败\r\n", next_bottle);
                        // 记录弃样失败信息
                        _record_discard_info(next_bottle, 1, 0, 0x0001);
                        next_bottle = 0; // 排空失败，不移动
                    }
                }
                else
                {
                    printf("[留样] 留样后无空瓶可用\r\n");
                    next_bottle = 0;
                }
            }

            if (next_bottle > 0)
            {
                if (bottle_move_to(next_bottle, 150, 120000))
                {
                    g_current_bottle_number = next_bottle;
                    g_RetainBottleState.currentBottle = next_bottle;
                    g_RetainSampleConfig.bottleNumber = next_bottle;
                    printf("[留样] 已定位到下一个空瓶%d\r\n", next_bottle);
                }
                else
                {
                    printf("[留样] 移动到下一个空瓶%d失败\r\n", next_bottle);
                }
            }
        }
    }

    // 持久化瓶位状态与配置
    cfg_save_retain_state(&g_RetainBottleState);
    cfg_save_retain(&g_RetainSampleConfig);

    // 更新上位机状态码：回到空闲状态
    update_bucket_state(bucket_id, BUCKET_STATE_IDLE);
    if (bucket_id == 0)
    {
        g_State.ABucketState = 0; //
        g_State.CurrentBucketRunState = 0;
        g_State.CurrentBucketRunState = 0;

        // 清零A桶倒计时器
        memset(g_State.ABucketCountDown, 0, sizeof(g_State.ABucketCountDown));
        printf("[系统] A桶状态转为待机，倒计时器已清零\r\n");
    }
    else
    {
        g_State.BBucketState = 0;
        g_State.CurrentBucketRunState = 0;
        g_State.CurrentBucketRunState = 0;

        // 清零B桶倒计时器
        memset(g_State.BBucketCountDown, 0, sizeof(g_State.BBucketCountDown));
        printf("[系统] B桶状态转为待机，倒计时器已清零\r\n");
    }

    // 清零当前桶倒计时器
    memset(g_State.CurrentBucketCountDown, 0, sizeof(g_State.CurrentBucketCountDown));

    printf("[留样] 成功：桶_%c，已使用瓶，已排空\r\n", bucket_char);
    return 1; // 成功（已排空桶）
}

/**
 * @brief 清空所有瓶位
 */
void retain_clear_all_bottles(void)
{
    g_RetainBottleState.usedMask = 0;
    g_RetainBottleState.currentBottle = 1;
    g_State.SampleBottle1 = 0; // 没有已留样的瓶
    g_State.SampleBottle2 = 1; // 第一个准备留样的瓶
    g_State.SampleBottle3 = 2; // 第一个空瓶

    cfg_save_retain_state(&g_RetainBottleState);
    printf("[留样] 所有瓶已清空\r\n");
}

/**
 * @brief 设置瓶位不确定标志
 */
void retain_set_bottle_position_uncertain(uint8_t uncertain)
{
    s_bottle_position_uncertain = uncertain ? 1 : 0;
    printf("[留样] 瓶位不确定标志设置为%d\r\n", s_bottle_position_uncertain);
}

/**
 * @brief 查询指定留样瓶的状态
 */
uint8_t retain_get_bottle_status(uint8_t bottle_number)
{
    // 参数检查
    if (bottle_number < 1 || bottle_number > BOTTLE_COUNT)
    {
        return 0xFF; // 无效瓶号
    }

    // 检查位掩码（bit0=瓶1, bit1=瓶2, ..., bit23=瓶24）
    uint32_t mask = 1u << (bottle_number - 1);
    uint8_t is_used = (g_RetainBottleState.usedMask & mask) ? 1 : 0;

    // 返回协议值：0=空瓶，2=满瓶
    return is_used ? 2 : 0;
}

//==============================================================================
// ADC三级滤波与留样判定函数
//==============================================================================

/**
 * @brief ADC采集与三级滤波处理（2ms周期调用）
 *
 * 三级滤波架构（12秒完成，递进增强平滑）：
 *
 * 第一级滤波（2ms周期）：
 *   - 输入：200点ADC原始值
 *   - 处理：去20个最大值、20个最小值，取160点平均
 *   - 输出：每2ms产生1个电流值（mA）
 *   - 缓存：存入第二级滤波缓冲区（200点）
 *
 * 第二级滤波（400ms周期）：
 *   - 输入：200个第一级滤波结果（400ms累积）
 *   - 处理：去20个最大值、20个最小值，取160点平均
 *   - 输出：每400ms产生1个平滑电流值
 *   - 缓存：存入第三级滤波缓冲区（30点）
 *
 * 第三级滤波（12秒周期）：
 *   - 输入：30个第二级滤波结果（12秒累积）
 *   - 处理：去5个最大值、5个最小值，取20点平均
 *   - 输出：每12秒产生1个超稳定电流值
 *   - 用途：channelData[i]，用于留样判定
 *
 * 时间轴（周期25ms，等待DMA完全更新）：
 *   25ms   → L1滤波 → channelCurrent[i]（实时显示用）
 *   5s     → L2滤波 → 中间结果（200次×25ms）
 *   150s   → L3滤波 → channelData[i]（留样判定用，30次×5s）
 *
 * 调用时机：task6中每25ms调用一次
 */
void retain_judge_process(void)
{
    // ?? 适配说明：移除了原代码中的阻塞等待，改为直接10ms周期调用
    // 原代码：while ((uint32_t)(g_tmr4_seconds - s_last_t4) < 2) { taskYIELD(); }
    // 现代码：由task6保证10ms周期，直接执行

//	/* 调试打印：首次调用时打印提示 */
//	static uint8_t first_run = 1;
//	static uint32_t call_count = 0;
//	call_count++;

//	if (first_run)
//	{
//			//        printf("[RETAIN_JUDGE] ADC process started, collecting 9 channels with 2.5V ref calibration...\r\n");
//			first_run = 0;
//	}

//	/* ?? 调试打印：每100次（1秒）打印一次调用计数 */
//	if (call_count % 100 == 0)
//	{
//			//        printf("[ADC_DEBUG] retain_judge_process called %lu times (%.1f seconds)\r\n", call_count, call_count / 100.0f);
//	}

	/* 步骤1：计算2.5V基准源的校准系数 */
    float ref_sum = 0.0f;
    float ref_sorted[200];  // 用于排序的临时数组

    // 读取2.5V基准源的200点ADC数据（索引8）
    for (int j = 0; j < 200; j++)
    {
        ref_sorted[j] = (float)(adc1_ordinary_valuetab[j][ADC_REF_CHANNEL_IDX]);
        ref_sum += ref_sorted[j];
    }

    // 插入排序（升序）
    for (int k = 1; k < 200; k++)
    {
        float key = ref_sorted[k];
        int t = k - 1;
        while (t >= 0 && ref_sorted[t] > key)
        {
            ref_sorted[t + 1] = ref_sorted[t];
            t--;
        }
        ref_sorted[t + 1] = key;
    }

    // 去除最小20个和最大20个，取中间160个平均
    float ref_sum160 = 0.0f;
    for (int k = 20; k < 180; k++)
    {
        ref_sum160 += ref_sorted[k];
    }
    float ref_adc_avg = ref_sum160 / 160.0f;

    // 计算校准系数
    // 理论ADC值 = 2.5V / 3.295V * 4095 = (ADC_REF_VOLTAGE / ADC_SUPPLY_VOLTAGE) * 4095
    // 校准系数 = 理论值 / 实测值
    float theoretical_adc = (ADC_REF_VOLTAGE / ADC_SUPPLY_VOLTAGE) * 4095.0f;
    float calibration_factor = theoretical_adc / ref_adc_avg;

    // 限制校准系数范围，防止异常值（正常应在0.95~1.05之间）
    if (calibration_factor < 0.90f || calibration_factor > 1.10f)
    {
        static uint8_t error_printed = 0;
        if (!error_printed)
        {
            printf("[ADC校准] 警告: 校准系数超出范围 (%.4f), 使用 1.0\r\n", calibration_factor);
            error_printed = 1;
        }
        calibration_factor = 1.0f; // 异常时不校准
    }

    /* 步骤2：第一级滤波 - 200点去20大20小后160点均值 + 校准 → channelCurrent[i] */
    for (int i = 0; i < 9; i++)
    {
        float tmp[200];  // 用于排序的临时数组

        // 遍历200点ADC数据并转换为电流值
        for (int j = 0; j < 200; j++)
        {
            // ADC转换链路：
            // 4-20mA → [3Ω采样电阻] → 12-60mV → [INA180×50放大] → 0.6-3.0V → [ADC采样]
            // 电流计算公式：I(mA) = V_adc / (R_sample × Gain)
            tmp[j] = (float)(adc1_ordinary_valuetab[j][i]) * calibration_factor / 4095.0f * ADC_SUPPLY_VOLTAGE / ADC_EQUIVALENT_RESISTOR_KOHM;
        }

        // 插入排序（升序）
        for (int k = 1; k < 200; k++)
        {
            float key = tmp[k];
            int t = k - 1;
            while (t >= 0 && tmp[t] > key)
            {
                tmp[t + 1] = tmp[t];
                t--;
            }
            tmp[t + 1] = key;
        }

        // 去除最小20个和最大20个，取中间160个平均
        float sum160 = 0.0f;
        for (int k = 20; k < 180; k++)
        {
            sum160 += tmp[k];
        }
        float L1_result = sum160 / 160.0f;

        // 第一级滤波结果：实时更新（用于显示）
        g_RetainSampleConfig.channelCurrent[i] = L1_result;

        // 缓存到第二级滤波缓冲区
        s_L2_buf[i][s_L2_idx] = L1_result;
    }

    /* ?? 已禁用L1打印（仅保留L3最终结果） */

    /* 累计200次（400ms）→ 第二级滤波：200点去20大20小后的160点均值 */
    s_L2_idx++;

    if (s_L2_idx >= 200)
    {
        s_L2_idx = 0; // 重置索引

        uint32_t tsSec = rtc_counter_get();

        // 对每个通道进行第二级滤波
        for (int i = 0; i < 9; i++)
        {
            // 拷贝到临时数组进行排序
            float tmp[200];
            for (int k = 0; k < 200; k++)
            {
                tmp[k] = s_L2_buf[i][k];
            }

            // 插入排序（升序）
            for (int k = 1; k < 200; k++)
            {
                float key = tmp[k];
                int t = k - 1;
                while (t >= 0 && tmp[t] > key)
                {
                    tmp[t + 1] = tmp[t];
                    t--;
                }
                tmp[t + 1] = key;
            }

            // 去除最小20个和最大20个，取中间160个平均
            float sum160 = 0.0f;
            for (int k = 20; k < 180; k++)
            {
                sum160 += tmp[k];
            }
            float L2_result = sum160 / 160.0f;

            // 缓存到第三级滤波缓冲区
            s_L3_buf[i][s_L3_idx] = L2_result;
            
            // ★ L2完成时更新channelCurrent和channelData（用于屏幕5秒显示）
            g_RetainSampleConfig.channelCurrent[i] = L2_result;
            
            // 计算输入值：4-20mA两点校准
            // 公式：输入值 = 校准值 / (校准电流 - 4) × (输入电流 - 4)
            // 4mA对应0，校准电流对应校准值
            if (i < 6) {
                float cal_current = retain_judge_cal_current_ma(g_RetainSampleConfig.channelCals[i].CalAD);
                float cal_value = g_RetainSampleConfig.channelCals[i].CalValue;
                if (cal_current > 4.0f) {
                    float value = cal_value / (cal_current - 4.0f) * (L2_result - 4.0f);
                    g_RetainSampleConfig.channelData[i] = (value < 0.0f) ? 0.0f : value;
                } else {
                    g_RetainSampleConfig.channelData[i] = 0.0f;
                }
            }
            else if (i == 7) {
                // ★ 流量通道（索引7）：L2完成时计算流量值（5秒响应）
                // 两点校准：4mA → 0流量，校准电流值 → 校准流量值
                float current_ma = L2_result;
                float i_cal = (float)g_CommSettingConfig.FlowADLower / 10.0f;  // 恢复0.1mA精度
                float flow_cal = g_CommSettingConfig.FlowMeterBase;
                
                if (i_cal <= 4.0f) {
                    g_RetainSampleConfig.channelData[7] = 0.0f;
                } else {
                    float flow = (current_ma - 4.0f) / (i_cal - 4.0f) * flow_cal;
                    if (flow < 0.0f) flow = 0.0f;
                    g_RetainSampleConfig.channelData[7] = flow;
                }

                /* 流量触发：使用60s L3滤波值做边沿检测（替代150s L3等待） */
                float flow_l3 = 0.0f;
                if (flow_l3_60s_update(g_RetainSampleConfig.channelData[7], &flow_l3))
                {
                    retain_judge_flow_edge_update(flow_l3, tsSec);
                }
            }
        }

        /* 累计30次（12秒）→ 第三级滤波：30点去5大5小后的20点均值 */
        s_L3_idx++;

        if (s_L3_idx >= 30)
        {
            s_L3_idx = 0; // 重置索引

            /* 调试打印：12秒边界提示 */
            static uint32_t L3_count = 0;
            L3_count++;

            // 对每个通道进行第三级滤波
            for (int i = 0; i < 9; i++)
            {
                // 拷贝到临时数组进行排序
                float tmp[30];
                for (int k = 0; k < 30; k++)
                {
                    tmp[k] = s_L3_buf[i][k];
                }

                // 插入排序（升序）
                for (int k = 1; k < 30; k++)
                {
                    float key = tmp[k];
                    int t = k - 1;
                    while (t >= 0 && tmp[t] > key)
                    {
                        tmp[t + 1] = tmp[t];
                        t--;
                    }
                    tmp[t + 1] = key;
                }

                // 去除最小5个和最大5个，取中间20个平均
                float sum20 = 0.0f;
                for (int k = 5; k < 25; k++)
                {
                    sum20 += tmp[k];
                }
                float L3_result = sum20 / 20.0f;

                // 第三级滤波结果：最稳定的数据（用于留样判定）
                if (i != 7)
                {
                    g_RetainSampleConfig.channelData[i] = L3_result;
                }
            }

            //            /* 调试打印：第三级滤波结果（最终稳定值） */
            //            printf("[ADC_L3_FINAL] CH0:%.2f CH1:%.2f CH2:%.2f CH7:%.2f mA (STABLE)\r\n",
            //                   g_RetainSampleConfig.channelData[0],
            //                   g_RetainSampleConfig.channelData[1],
            //                   g_RetainSampleConfig.channelData[2],
            //                   g_RetainSampleConfig.channelData[7]);
            //
            //            /* 调试打印：打印9个通道的4-20mA电流值 */
            //            printf("[ADC_4-20mA] CH0:%.2f CH1:%.2f CH2:%.2f CH3:%.2f CH4:%.2f CH5:%.2f CH6:%.2f CH7:%.2f CH8:%.2f mA\r\n",
            //                   g_RetainSampleConfig.channelData[0],
            //                   g_RetainSampleConfig.channelData[1],
            //                   g_RetainSampleConfig.channelData[2],
            //                   g_RetainSampleConfig.channelData[3],
            //                   g_RetainSampleConfig.channelData[4],
            //                   g_RetainSampleConfig.channelData[5],
            //                   g_RetainSampleConfig.channelData[6],
            //                   g_RetainSampleConfig.channelData[7],
            //                   g_RetainSampleConfig.channelData[8]);

            // 10秒边界：提交第三级滤波后的数据进行留样判定
            retain_judge_commit_adc_1s_internal(g_RetainSampleConfig.channelData, rtc_counter_get());
        }
    }
}

uint8_t retain_judge_uart7_should_run(void)
{
#if USE_UART7_AI
    return (g_RetainSampleConfig.Mode == RETAIN_MODE_ALARM) || (g_SampleConfig.SamplingMode == 3);
#else
    return 0;
#endif
}

void retain_judge_uart7_mark_invalid(void)
{
    s_uart7.data_valid = 0;
    s_uart7.valid_mask = 0;
    retain_judge_uart7_reset_pending();
}

void retain_judge_uart7_update(const uint16_t regs[6], uint32_t tsSec)
{
    uint8_t valid_mask = 0;
    float current_ma[6] = {0};
    float flow_current = 0.0f;
    uint8_t flow_valid = 0;

    for (int i = 0; i < 5; i++)
    {
        if (regs[i] > 0)
        {
            float v = (float)regs[i] / 1000.0f;
            if (v >= 4.0f && v <= 20.0f)
            {
                current_ma[i] = v;
                valid_mask |= (uint8_t)(1u << i);
            }
        }
    }

    if (regs[5] > 0)
    {
        float v = (float)regs[5] / 1000.0f;
        if (v >= 4.0f && v <= 20.0f)
        {
            flow_current = v;
            flow_valid = 1u;
        }
    }

    s_uart7.data_valid = 1;
    s_uart7.valid_mask = valid_mask;

    for (int i = 0; i < 5; i++)
    {
        g_RetainSampleConfig.channelCurrent[i] = current_ma[i];
        if (valid_mask & (1u << i))
        {
            float cal_current = retain_judge_cal_current_ma(g_RetainSampleConfig.channelCals[i].CalAD);
            float cal_value = g_RetainSampleConfig.channelCals[i].CalValue;
            if (cal_current > 0.0f)
            {
                g_RetainSampleConfig.channelData[i] = cal_value / cal_current * current_ma[i];
            }
            else
            {
                g_RetainSampleConfig.channelData[i] = 0.0f;
            }
        }
        else
        {
            g_RetainSampleConfig.channelData[i] = 0.0f;
        }
    }

    g_RetainSampleConfig.channelCurrent[5] = 0.0f;
    g_RetainSampleConfig.channelData[5] = 0.0f;

    g_RetainSampleConfig.channelCurrent[7] = flow_current;
    if (flow_valid)
    {
        float i_cal = (float)g_CommSettingConfig.FlowADLower / 10.0f;
        float flow_cal = g_CommSettingConfig.FlowMeterBase;
        if (i_cal > 4.0f)
        {
            float flow = (flow_current - 4.0f) / (i_cal - 4.0f) * flow_cal;
            if (flow < 0.0f)
            {
                flow = 0.0f;
            }
            g_RetainSampleConfig.channelData[7] = flow;
        }
        else
        {
            g_RetainSampleConfig.channelData[7] = 0.0f;
        }
        retain_judge_uart7_flow_update(g_RetainSampleConfig.channelData[7], 1u, tsSec);
    }
    else
    {
        g_RetainSampleConfig.channelData[7] = 0.0f;
        retain_judge_uart7_flow_update(0.0f, 0u, tsSec);
    }

    (void)retain_judge_uart7_check_analog(tsSec);
}

/**
 * @brief 1秒边界ADC数据提交与留样判定（内部函数）
 *
 * @param chCurrent 通道电流值数组（9个通道）
 * @param tsSec 当前时间戳（秒）
 *
 * 调用时机：每1秒调用一次（累积100次第一级滤波结果后）
 *
 * 职责：
 * - 换算channelData（电流 → 实际物理量）
 * - 流量通道边沿检测（上升沿/下降沿）
 * - 模拟量通道超标边沿检测
 * - 记录TSDB边沿事件
 * - 调用analysis_report_analog上报
 */
static void retain_judge_commit_adc_1s_internal(const float chCurrent[9], uint32_t tsSec)
{
    /* 调试打印：每1秒打印9个通道的4-20mA电流值 */
    //    printf("[ADC_4-20mA] CH0:%.2f CH1:%.2f CH2:%.2f CH3:%.2f CH4:%.2f CH5:%.2f CH6:%.2f CH7:%.2f CH8:%.2f mA\r\n",
    //           chCurrent[0], chCurrent[1], chCurrent[2], chCurrent[3], chCurrent[4],
    //           chCurrent[5], chCurrent[6], chCurrent[7], chCurrent[8]);

    /* 1) 换算 channelData：电流值 → 实际物理量 */
    // 注意：channelCurrent由L1滤波实时更新，此处不再用chCurrent覆盖
    // chCurrent参数实际传入的是L3滤波结果(channelData)，用于留样判定
    for (int i = 0; i < 9; ++i)
    {
        // 根据通道类型选择不同的校准换算方式
        if (i < 6)
        {
            // 通道0-5：简单比例校准
            // 公式：输入值 = 校准值 / 校准电流 × 输入电流
            float current_ma = g_RetainSampleConfig.channelCurrent[i];
            float cal_current = retain_judge_cal_current_ma(g_RetainSampleConfig.channelCals[i].CalAD); // 校准电流(mA)
            float cal_value = g_RetainSampleConfig.channelCals[i].CalValue;       // 校准值
            
            if (cal_current > 0.0f) {
                g_RetainSampleConfig.channelData[i] = cal_value / cal_current * current_ma;
            } else {
                g_RetainSampleConfig.channelData[i] = 0.0f; // 校准电流为0时，输出0
            }
        }
        else if (i == 6)
        {
            // 通道6（备用）：简单比例校准
            // 公式：输入值 = 校准值 / 校准电流 × 输入电流
            float current_ma = g_RetainSampleConfig.channelCurrent[i];
            float cal_current = retain_judge_cal_current_ma(g_RetainSampleConfig.channelCals[i].CalAD);
            float cal_value = g_RetainSampleConfig.channelCals[i].CalValue;
            
            if (cal_current > 0.0f) {
                g_RetainSampleConfig.channelData[i] = cal_value / cal_current * current_ma;
            } else {
                g_RetainSampleConfig.channelData[i] = 0.0f;
            }
        }
        else if (i == 7)
        {
            // 通道7（流量）：已在L2完成时计算（5秒响应），此处不再重复计算
            // 保持channelData[7]为L2计算的流量值
        }
        else
        {
            // 通道8（2.5V基准）：不需要校准换算，保持电流值
            g_RetainSampleConfig.channelData[i] = g_RetainSampleConfig.channelCurrent[i];
        }
    }

    /* 2) 流量通道（索引7）独立处理：更新流量触发状态 + TSDB边沿日志 */
    {
        float flow = g_RetainSampleConfig.channelData[7];
        if (s_flow_L3_60s_filled >= 12)
        {
            flow = s_flow_L3_60s_value;
        }
        retain_judge_flow_edge_update(flow, tsSec);
    }

    /* 3) 其他通道（0-5）超标边沿检测，并准备上报数组（流量通道7单独处理） */
    float vals[6];
    for (int i = 0; i < 6; i++)
    {
        vals[i] = g_RetainSampleConfig.channelData[i];
    }

    // 逐通道检查超标边沿
    for (int i = 0; i < 6; ++i)
    {
        if (g_RetainSampleConfig.channelLimits[i].Enable)
        {
            float v = g_RetainSampleConfig.channelData[i];
            float lo = g_RetainSampleConfig.channelLimits[i].LowerLimit;
            float hi = g_RetainSampleConfig.channelLimits[i].UpperLimit;
            uint8_t now_over = (uint8_t)((v < lo) || (v > hi));

            // 上升沿：从正常到超标
            if (now_over && !s_judge.over_state[i])
            {
                struct
                {
                    uint8_t code;
                    uint8_t ch;
                    float v;
                    float lo;
                    float hi;
                } ev = {0xA1u, (uint8_t)(i + 1), v, lo, hi};
                (void)tsdb_event_append(6u, &ev, sizeof(ev));
                s_judge.over_state[i] = 1u;

                printf("[ADC滤波] 通道 %d 超标: %.2f (%.2f-%.2f)\r\n",
                       i + 1, v, lo, hi);

                // 注释：已删除超标预记录（重复记录）
                // 新设计：只在retention_execute()成功时写一条完整的留样记录（包含sample_id）
                // 旧代码：在超标检测时写预记录，result=0待更新，但没有后续更新逻辑
                if (allow_analog_trigger())
                {
                    // 只打印日志，不写TSDB预记录
                    printf("[ADC滤波] 通道%d超标触发留样判定，等待留样执行完成后统一记录\r\n", i + 1);
                }
            }
            // 下降沿：从超标到正常
            else if (!now_over && s_judge.over_state[i])
            {
                struct
                {
                    uint8_t code;
                    uint8_t ch;
                    float v;
                    float lo;
                    float hi;
                } ev = {0xA0u, (uint8_t)(i + 1), v, lo, hi};
                (void)tsdb_event_append(6u, &ev, sizeof(ev));
                s_judge.over_state[i] = 0u;

                printf("[ADC滤波] 通道 %d 恢复正常: %.2f\r\n", i + 1, v);
            }
        }
    }

    /* 4) 向调度器上报模拟量（通道0-5，流量通道7单独处理） */
    if (allow_analog_trigger())
    {
        analysis_report_analog(vals, tsSec);
    }
}

//==============================================================================
// 屏幕电流值发送功能
//==============================================================================

/**
 * @brief 发送6路通道ADC数据到屏幕（进入通道设置页时5秒周期调用）
 * 
 * 每通道发送4项数据：
 * - 508A等: 输入电流 (channelCurrent, 动态计算)
 * - 508B等: 因子值/COD值 (channelData, 动态计算)
 * - 508C等: 校准电流 (CalAD, 用户输入的固定值)
 * - 508D等: 校准值 (CalValue, 用户输入的固定值)
 */
void retain_send_current_values_to_screen(void)
{
    // 6个通道的基地址映射 (508A, 508E, 5092, 5096, 509A, 509E)
    const uint16_t channel_addr[6] = {0x508A, 0x508E, 0x5092, 0x5096, 0x509A, 0x509E};

    for (int i = 0; i < 6; i++) {
        uint8_t buf[8];
        uint16_t value;

        // 构建数据包基础结构
        buf[0] = 0x5A;
        buf[1] = 0xA5;
        buf[2] = 0x05;  // 数据长度
        buf[3] = 0x82;

        // ========== 1. 发送输入电流到 508A/508E/5092/5096/509A/509E ==========
        if (i == 5) {
            // 通道6：显示流量输入电流（mA，0.01精度）
            value = (uint16_t)(g_RetainSampleConfig.channelCurrent[7] * 100.0f + 0.5f);
        } else {
            value = (uint16_t)(g_RetainSampleConfig.channelCurrent[i] * 100.0f + 0.5f);
        }
        buf[4] = (uint8_t)(channel_addr[i] >> 8);
        buf[5] = (uint8_t)(channel_addr[i] & 0xFF);
        buf[6] = (uint8_t)(value >> 8);
        buf[7] = (uint8_t)(value & 0xFF);
        screen_send_notify(USART_SCREEN, buf, 8, 3);

        // ========== 2. 发送因子值(COD等)到 508B/508F/5093/5097/509B/509F ==========
        uint16_t factor_value;
        if (i == 5) {
            // 通道6：显示流量值（1位小数）
            if (g_CommSettingConfig.FlowADLower == 0) {
                factor_value = 0;
            } else {
                factor_value = (uint16_t)(g_RetainSampleConfig.channelData[7] * 10.0f + 0.5f);
            }
        } else if (g_RetainSampleConfig.channelCals[i].CalAD == 0) {
            factor_value = 0;
        } else if (i == 0) {
            // 通道1：原值
            factor_value = (uint16_t)(g_RetainSampleConfig.channelData[i] + 0.5f);
        } else if (i == 2) {
            // 通道3：扩大100倍
            factor_value = (uint16_t)(g_RetainSampleConfig.channelData[i] * 100.0f + 0.5f);
        } else {
            // 通道2/4/5：扩大10倍
            factor_value = (uint16_t)(g_RetainSampleConfig.channelData[i] * 10.0f + 0.5f);
        }
        buf[4] = (uint8_t)((channel_addr[i] + 1) >> 8);
        buf[5] = (uint8_t)((channel_addr[i] + 1) & 0xFF);
        buf[6] = (uint8_t)(factor_value >> 8);
        buf[7] = (uint8_t)(factor_value & 0xFF);
        screen_send_notify(USART_SCREEN, buf, 8, 3);

        // ========== 3. 发送校准电流到 508C/5090/5094/5098/509C/50A0 ==========
        // Cal current uses 0.1mA units for screen display (legacy int mA <= 30).
        uint16_t cal_current;
        if (i == 5) {
            // 通道6：显示流量校准电流（已是0.1mA单位）
            cal_current = g_CommSettingConfig.FlowADLower;
        } else {
            cal_current = retain_judge_cal_current_to_screen(g_RetainSampleConfig.channelCals[i].CalAD);
        }
        buf[4] = (uint8_t)((channel_addr[i] + 2) >> 8);
        buf[5] = (uint8_t)((channel_addr[i] + 2) & 0xFF);
        buf[6] = (uint8_t)(cal_current >> 8);
        buf[7] = (uint8_t)(cal_current & 0xFF);
        screen_send_notify(USART_SCREEN, buf, 8, 3);

        // ========== 4. 发送校准值到 508D/5091/5095/5099/509D/50A1 ==========
        // 校准值是用户输入的固定值
        uint16_t cal_value;
        if (i == 0) {
            // 通道1：原值
            cal_value = (uint16_t)(g_RetainSampleConfig.channelCals[i].CalValue + 0.5f);
        } else if (i == 5) {
            // 通道6：显示流量校准值（1位小数）
            cal_value = (uint16_t)(g_CommSettingConfig.FlowMeterBase * 10.0f + 0.5f);
        } else {
            // 通道2-5：扩大10倍
            cal_value = (uint16_t)(g_RetainSampleConfig.channelCals[i].CalValue * 10.0f + 0.5f);
        }
        buf[4] = (uint8_t)((channel_addr[i] + 3) >> 8);
        buf[5] = (uint8_t)((channel_addr[i] + 3) & 0xFF);
        buf[6] = (uint8_t)(cal_value >> 8);
        buf[7] = (uint8_t)(cal_value & 0xFF);
        screen_send_notify(USART_SCREEN, buf, 8, 3);
    }
}

/**
 * @brief 发送流量相关数据到串口屏（流量触发模式下5秒周期调用）
 * 
 * 发送4项数据（通道六页面）：
 * - 509E: 流量输入电流（扩大10倍，1位小数）
 * - 509F: 计算得到的流量值（扩大10倍，1位小数）
 * - 50A0: 流量校准电流（扩大10倍）
 * - 50A1: 流量校准流量（扩大10倍）
 * 
 * 流量计算公式：流量 = (电流mA - 4) / (校准电流 - 4) × 校准流量
 */
void retain_send_flow_values_to_screen(void)
{
    // 仅在流量触发模式下启用
    if (g_SampleConfig.SamplingMode != 3) {
        return;
    }

    // 获取校准参数（存储时已缩小10倍）
    float i_cal = (float)g_CommSettingConfig.FlowADLower / 10.0f;  // 校准电流(mA)
    float flow_cal = g_CommSettingConfig.FlowMeterBase;            // 校准流量

    // 获取当前电流值（L2滤波后）
    float current_ma = g_RetainSampleConfig.channelCurrent[7];

    // 计算流量值：(电流 - 4) / (校准电流 - 4) × 校准流量
    float flow = 0.0f;
    if (i_cal > 4.0f) {
        flow = (current_ma - 4.0f) / (i_cal - 4.0f) * flow_cal;
        if (flow < 0.0f) flow = 0.0f;
    }

    uint8_t buf[8];
    buf[0] = 0x5A;
    buf[1] = 0xA5;
    buf[2] = 0x05;  // 数据长度
    buf[3] = 0x82;  // 写指令
    
    // ========== 1. 发送流量输入电流到 509E ==========
    uint16_t current_value = (uint16_t)(current_ma * 100.0f + 0.5f);
    buf[4] = 0x50;
    buf[5] = 0x9E;
    buf[6] = (uint8_t)(current_value >> 8);
    buf[7] = (uint8_t)(current_value & 0xFF);
    screen_send_notify(USART_SCREEN, buf, 8, 3);

    // ========== 2. 发送流量值到 509F ==========
    uint16_t flow_value = (uint16_t)(flow * 10.0f + 0.5f);
    buf[4] = 0x50;
    buf[5] = 0x9F;
    buf[6] = (uint8_t)(flow_value >> 8);
    buf[7] = (uint8_t)(flow_value & 0xFF);
    screen_send_notify(USART_SCREEN, buf, 8, 3);
    
    // ========== 3. 发送流量校准电流到 50A0 ==========
    // 直接发送存储值（已是扩大10倍的值）
    uint16_t cal_current = g_CommSettingConfig.FlowADLower;
    buf[4] = 0x50;
    buf[5] = 0xA0;
    buf[6] = (uint8_t)(cal_current >> 8);
    buf[7] = (uint8_t)(cal_current & 0xFF);
    screen_send_notify(USART_SCREEN, buf, 8, 3);
    
    // ========== 4. 发送流量校准流量到 50A1 ==========
    // 扩大10倍发送（1位小数精度）
    uint16_t cal_flow = (uint16_t)(flow_cal * 10.0f + 0.5f);
    buf[4] = 0x50;
    buf[5] = 0xA1;
    buf[6] = (uint8_t)(cal_flow >> 8);
    buf[7] = (uint8_t)(cal_flow & 0xFF);
    screen_send_notify(USART_SCREEN, buf, 8, 3);
}

//==============================================================================
// 四川协议超标留样窗口检查
//==============================================================================

extern SichuanExceedRetainCtx_t g_SichuanExceedRetainCtx;

/**
 * @brief 四川协议超标留样窗口检查（周期性调用）
 * @note 在task4中周期性调用，检查窗口是否超时
 *       窗口超时时自动执行排水
 */
void sichuan_exceed_retain_check(void)
{
    // 仅在四川协议模式下执行
    if (g_CommSettingConfig.AutoCalibration != COMM_PROTOCOL_SICHUAN) {
        return;
    }

    uint32_t now = rtc_counter_get();

    // 检查是否有待处理的超标留样上下文
    if (!g_SichuanExceedRetainCtx.pending) {
        return;
    }

    // 检查窗口是否超时
    if (now > g_SichuanExceedRetainCtx.window_end_time) {
        // 窗口超时，自动排水
        printf("[四川超标留样] 窗口超时，自动排水桶%c\r\n",
               g_SichuanExceedRetainCtx.bucket_id ? 'B' : 'A');
        drain_execute(g_SichuanExceedRetainCtx.bucket_id);
        g_SichuanExceedRetainCtx.pending = 0;
    }
}

/**
 * @brief 四川协议超标留样窗口初始化（送样完成时调用）
 * @param bucket_id 桶号（0=A桶，1=B桶）
 * @note 在送样完成回调中调用，初始化超标留样窗口上下文
 */
void sichuan_init_exceed_retain_window(uint8_t bucket_id)
{
    // 仅在四川协议模式下执行
    if (g_CommSettingConfig.AutoCalibration != COMM_PROTOCOL_SICHUAN) {
        return;
    }

    uint32_t now = rtc_counter_get();
    uint32_t analysis_sec = g_SampleConfig.AnalysisTime * 60;

    g_SichuanExceedRetainCtx.pending = 1;
    g_SichuanExceedRetainCtx.bucket_id = bucket_id;
    g_SichuanExceedRetainCtx.request_time = now;
    g_SichuanExceedRetainCtx.window_end_time = now + analysis_sec;

    printf("[四川超标留样] 窗口初始化: 桶%c, 超时=%lu秒\r\n",
           bucket_id ? 'B' : 'A', analysis_sec);
}

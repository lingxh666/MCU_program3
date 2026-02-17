/* add user code begin Header */
/**
 ******************************************************************************
 * File Name          : freertos_app.h
 * Description        : Code for freertos applications
 */
/* add user code end Header */

#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "event_groups.h"
#include "wk_system.h"

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */
#include <string.h>
#include "app_flashdb.h"
/* add user code end private includes */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

#define USART_SCREEN UART4
#define USART_4G USART6
#define USART_MOTOR UART5
#define USART_AI UART7
#define USART_USB USART3
#define USART_analyser USART2


#define InletThreeWayValveA gpio_bits_set(GPIOD, GPIO_PINS_14)   // 采样进水三通阀-A开B关 //R1--D14
#define InletThreeWayValveB gpio_bits_reset(GPIOD, GPIO_PINS_14) // 采样进水三通阀-B开A关

#define SampleThreeWayValveSample gpio_bits_set(GPIOD, GPIO_PINS_10)     // 送样留样三通阀-送样 //R5 D10
#define SampleThreeWayValveSTAY gpio_bits_reset(GPIOD, GPIO_PINS_10)     // 送样留样三通阀-留样
#define InstantThreeWayValveDirect gpio_bits_set(GPIOD, GPIO_PINS_11)    // 瞬时三通阀-直通 //R4  D11
#define InstantThreeWayValveInstant gpio_bits_reset(GPIOD, GPIO_PINS_11) // 瞬时三通阀-瞬时

#define AcidPumpStop gpio_bits_set(GPIOE, GPIO_PINS_10)  // 加酸蠕动泵-停止 加药  R7  E10
#define AcidPumpRun gpio_bits_reset(GPIOE, GPIO_PINS_10) // 加酸蠕动泵-运行 加药

#define ExternalPumpStop gpio_bits_set(GPIOE, GPIO_PINS_11)  // 远程泵外接泵-停止  R6 E11
#define ExternalPumpRun gpio_bits_reset(GPIOE, GPIO_PINS_11) // 远程泵外接泵-运行
#define MixAStop gpio_bits_set(GPIOE, GPIO_PINS_15)          // A桶混合-停止 R12  E15
#define MixARun gpio_bits_reset(GPIOE, GPIO_PINS_15)         // A桶混合-运行
#define MixBStop gpio_bits_set(GPIOE, GPIO_PINS_14)          // B桶混合-停止 R13 E14
#define MixBRun gpio_bits_reset(GPIOE, GPIO_PINS_14)         // B桶混合-运行

#define DrainAStop gpio_bits_set(GPIOB, GPIO_PINS_11)  // A桶排水-关闭  R10 B11
#define DrainARun gpio_bits_reset(GPIOB, GPIO_PINS_11) // A桶排水-运行
#define DrainBStop gpio_bits_set(GPIOB, GPIO_PINS_10)  // B桶排水-关闭     R11
#define DrainBRun gpio_bits_reset(GPIOB, GPIO_PINS_10) // B桶排水-运行
#define DoorStop gpio_bits_set(GPIOE, GPIO_PINS_13)    // 门禁-关闭  R14 E13
#define DoorRun gpio_bits_reset(GPIOE, GPIO_PINS_13)   // 门禁-运行
#define TriggerStop gpio_bits_set(GPIOE, GPIO_PINS_7)  // 触发输出-关闭/启动外设  R8 E7
#define TriggerRun gpio_bits_reset(GPIOE, GPIO_PINS_7) // 触发输出-运行/启动外设
#define CleanStop gpio_bits_set(GPIOB, GPIO_PINS_1)    // 清洗阀-关闭  R15
#define CleanRun gpio_bits_reset(GPIOB, GPIO_PINS_1)   // 清洗阀-运行

#define WasteWaterDrainOpen  gpio_bits_reset(GPIOB, GPIO_PINS_1)  // 废水排放-开启（浮子开关控制）PB1
#define WasteWaterDrainClose gpio_bits_set(GPIOB, GPIO_PINS_1)    // 废水排放-关闭（浮子开关控制）PB1

#define M485  gpio_bits_set(GPIOC,  GPIO_PINS_12);
#define M232  gpio_bits_reset(GPIOC,  GPIO_PINS_12);

/* app starting address */
#define APP_START_ADDR 0x08002000
#define TEMPLATE_START_ADDR 0x08041000
/* the previous sector of app starting address is ota upgrade flag */
#define OTA_UPGRADE_FLAG_ADDR 0x08001800

/* when app received cmd 0x5aa5 from pc-tool, will set up the flag,
indicates that an app upgrade will follow, see ota application note for more details */
#define OTA_UPGRADE_FLAG 0x41544B38

// 瓶排空  R16 R17

extern volatile uint32_t g_tmr2_seconds, g_tmr3_milliseconds, g_tmr4_seconds;
extern uint8_t SendMqttFlag;
extern volatile uint8_t Screenflag;
extern uint8_t SendMqttStatusFlag;    // 10分钟状态发送标志
extern uint8_t SendMqttSettingsFlag;  // 2小时设置发送标志
extern uint16_t MqttStatusCount;      // 10分钟计数器(600秒)
extern uint16_t MqttSettingsCount;    // 2小时计数器(7200秒)
extern uint8_t UART2_Buf[160], UART3_Buf[100], UART4_Buf[100], UART5_Buf[100], UART6_Buf[512], UART7_Buf[100], UART8_Buf[100];
extern __IO uint16_t adc1_ordinary_valuetab[200][9];

/* ★ 留样中止标志：用于瓶盘复位时中止正在进行的留样 */
extern volatile uint8_t g_retention_abort_flag;

typedef struct UART_Message
{
    uint16_t len;
    uint8_t data[100];
} UartMessage;

// 屏采样设置页面
typedef struct SampleConfig_tag
{
    uint8_t BucketAB;             // 采样AB桶选择
    uint8_t SamplingMode;         // 采样模式（0-7，对应8种模式）（时间等比  定时采样 通讯触发 流量触发 开关触发 流量等比 间歇排放 其他  总计8种模式）
    uint16_t SamplingImproveTime; // 采样提升时间，单位秒
    uint16_t SampleInterval;      // 采样间隔，单位分钟
    uint16_t TubeHoldTime;        // 采样管存时间，单位秒
    uint16_t SampleVolume;        // 单次采样量，单位毫升
    uint16_t CycleTime;           // 周期时间，单位分钟
    uint16_t BlowbackTime;        // 采样反吹时间，单位秒
    uint16_t BucketDrainTime;     // 采样桶排空时间，单位秒
    uint16_t AnalysisTime;        // 仪器分析时间，单位分钟
    uint16_t DischargeVolume;     // 排放等比：排放量，单位m3
    uint16_t FlowRatio;           // 流量比例，单位ml/m3
    uint16_t FlowStart;           // 流量跟随：流量触发，单位m3/h
    uint16_t FlowStop;            // 流量跟随：流量停止值，单位m3/h
} SampleConfig;
// 送样设置
typedef struct
{
    uint8_t Enable;    // 是否定时启动（1=启用，0=关闭）
    uint8_t StartHour; // 送样开始时间-小时
    uint8_t StartMin;  // 送样开始时间-分钟
    uint16_t Duration; // 送样时长，单位秒
    uint8_t EndHour;   // 送样结束时间-小时
    uint8_t EndMin;    // 送样结束时间-分钟
    uint16_t Interval; // 送样回抽，单位秒
    uint8_t fixedhour[24];
    uint8_t fixedmin;
} SampleDeliveryIntervalConfig;

// 留样设置
typedef struct
{
    uint16_t InputAD; // 输入AD值
    uint16_t ZeroAD;  // 0点AD值
    uint16_t CalAD;   // 校准AD值
    float CalValue;   // 校准值
} ChannelCalConfig;

typedef struct
{
    uint8_t FactorType;      // 因子类型 (0x0001~0x0010)
    float LowerLimit;        // 超标下限
    float UpperLimit;        // 超标上限
    uint8_t ParallelCount;   // 超标平行样数 (1~24瓶)
    uint16_t RetainInterval; // 超标留样间隔 (1~9999小时)
    uint8_t Enable;          // 是否启用 (0=停用, 1=启用)
} ChannelLimitConfig;

typedef struct
{
    uint8_t Mode;                        // 留样模式
    uint8_t bottleNumber;                // 留样瓶号  未在屏设置项
    uint8_t EnableSample;                // 是否留样
    uint8_t EnableAcid;                  // 是否加酸
    uint8_t EnableVacuum;                // 是否排空
    uint16_t SampleVolume;               // 单次留样量
    uint8_t ParallelCount;               // 平行样数量
    uint8_t MixCount;                    // 混样次数
    uint16_t TubeHoldTime;               // 留样管存放时间
    uint16_t BlowbackTime;               // 留样反吹
    uint16_t BackdrawTime;               // 留样回抽
    ChannelLimitConfig channelLimits[6]; // 通道超标设定（仅索引0-5）
    float channelData[9];                // ADC通道数据（索引0-5=超标判定，6=备用，7=流量，8=2.5V基准）
    float channelCurrent[9];             // ADC通道电流值（索引0-5=超标判定，6=备用，7=流量，8=2.5V基准）
    uint8_t channelDataType[6];          // 通道1-6传输数据类型(COD  NH4N等16项)
    ChannelCalConfig channelCals[9];     // 通道0-8校准设置（索引0-5=超标通道，6=备用，7=流量专用参数未使用此槽位，8=基准）
} RetainSampleModeConfig;

// 高级设置
// 串口屏协议选择（对应g_CommSettingConfig.AutoCalibration）
#define COMM_PROTOCOL_DAYUE    0
#define COMM_PROTOCOL_DAHU     1
#define COMM_PROTOCOL_SICHUAN  2
typedef struct
{
    uint8_t Protocol;      // 通讯协议（0=485，1=232，...，编号自定义）
    uint8_t DeviceAddr;    // 设备地址号
    uint8_t AutoCalibration; // 协议选择（0=大岳，1=大湖，2=四川管控）
    uint16_t FlowADUpper;  // 流量触发阈值 (存储值×10=实际值，单位m³/h)
    uint16_t FlowADLower;  // 校准电流值 (存储值/10=实际mA值，如233=23.3mA)
    float FlowMeterBase;   // 校准流量值 (单位m³/h，对应校准电流时的流量)
    char IPSET[24];
    char IDSET[24];
} CommSettingConfig;

extern CommSettingConfig g_CommSettingConfig;

typedef struct
{
    // 时间设置
    uint16_t Year;  // 年
    uint8_t Month;  // 月
    uint8_t Day;    // 日
    uint8_t Hour;   // 时
    uint8_t Minute; // 分
    uint8_t Second; // 秒

    // 模式设置
    uint8_t WaterStationMode; // 水站模式（1=启用，0=禁用）水站模式是指给信号就留样，所有留样都是瞬时样
    uint8_t AutoRunMode;      // 自动运行（1=启用，0=禁用）

    // 软件信息
    char SoftwareSerial[24];  // 软件序列号
    char SoftwareCoreVer[16]; // 软件核心板版本
    char SoftwareLcdVer[16];  // 软件液晶屏版本

    // 硬件信息
    char HardwareBaseVer[16]; // 硬件底板版本
    char HardwareCoreVer[16]; // 硬件核心板版本
    char HardwareLcdVer[16];  // 硬件液晶屏版本

    // 门禁卡号
    uint32_t CardId[10];
    uint8_t Motorspeed; // 蠕动泵转速
} SystemSettingConfig;

// 精度校准 单位是ml  s
typedef struct
{
    // 采样量校准
    struct
    {
        uint16_t time1;      // 采样量时间1校准
        uint16_t realValue1; // 采样量真实值1校准
        uint16_t time2;      // 采样量时间2校准
        uint16_t realValue2; // 采样量真实值2校准
        uint16_t time3;      // 采样量时间3校准
        uint16_t realValue3; // 采样量真实值3校准
    } samplingCalib;

    // 留样量校准
    struct
    {
        uint16_t time1;      // 留样量时间1校准
        uint16_t realValue1; // 留样量真实值1校准
        uint16_t time2;      // 留样量时间2校准
        uint16_t realValue2; // 留样量真实值2校准
        uint16_t time3;      // 留样量时间3校准
        uint16_t realValue3; // 留样量真实值3校准
    } retainSampleCalib;

    // 加酸量校准
    struct
    {
        uint16_t time1;      // 加酸量时间1校准
        uint16_t realValue1; // 加酸量真实值1校准
        uint16_t time2;      // 加酸量时间2校准
        uint16_t realValue2; // 加酸量真实值2校准
        uint16_t time3;      // 加酸量时间3校准
        uint16_t realValue3; // 加酸量真实值3校准
    } acidAdditionCalib;

    // 温度校准
    struct
    {
        uint16_t inputAD;     // 温度校准输入AD
        uint16_t zeroPointAD; // 温度校准0点AD
        uint16_t calibAD;     // 温度校准校准AD
        uint16_t calibValue;  // 温度校准校准值（实际值 = calibValue / 100.0f）
        uint16_t setTemp;     // 温度设置温度
        uint16_t upperDev;    // 温度设置上偏差
        uint16_t lowerDev;    // 温度设置下偏差
        uint16_t zeroTemp;    // 温度设置0点温度（实际值 = zeroTemp / 100.0f）
    } tempCalib;
} CalibrationParams_t;

// 手动控制设置
typedef struct
{
    uint8_t sampleBucket;      // 采样AB桶选择，1=A桶，2=B桶
    uint16_t sampleVolume;     // 采样量，单位毫升
    uint8_t deliveryMode;      // 送样方式  瞬时送样=0 A桶送样=1 B桶送样=2
    uint16_t deliveryVolume;   // 送样量
    uint8_t retainMode;        // 留样方式  瞬时留样=0 A桶留样=1 B桶留样=2 自选留样=3
    uint16_t retainVolume;     // 留样量
    uint8_t bottleNumber;      // 瓶号  1-24
    uint8_t turnbottleNumber;  // 转动到位瓶号  1-24
    uint8_t emptybottleNumber; // 排空瓶号  1-24

} SingleSampleTest_t;

// 首页状态全局变量
/*
g_State.SamplingMotor
运行状态  包括CurrentBucketRunState， ABucketState，BBucketState，InstantOperationState;
值0-50  0待机中  2等待采样  4采样前反吹  6启动外接泵 7采样管存 8采样中
9采样暂停  10采样后反吹  12采样完成 13等待送样  16送样前回抽  19送样中  22等待分析
30旋转分样盘 31留样瓶排空  34留样前回抽 35留前回抽完  37留样管存 38留样中  39留样暂停
40留样后回抽  42留样排空  47排空留样瓶 48排空瓶完成


 倒计时输入格式{时,分,秒},{0x0F,0x08,0x10}表示15:08:16  这里都是16进制输入的

时间变量Time[6]：输入编码字符串RTC时间   格式：{年,月,日,时,分,秒}  输入{0x25,0x08,0x30,0x20,0x27,0x03}表示25年8月30日20点27分30秒
*/

typedef struct
{
    uint8_t SamplingMotor;        // 采样蠕动泵   值0-2 停止/采样/反吹
    uint8_t DeliveryMotor;        // 送样蠕动泵   值0-2 停止/正转/反转
    uint8_t InletThreeWayValve;   // 进水三通阀 值1-A桶 2-B桶
    uint8_t OutletThreeWayValve;  // 出水三通阀  A桶/0 B桶/1
    uint8_t SampleThreeWayValve;  // 送样三通阀 值0-1 开/关
    uint8_t InstantThreeWayValve; // 瞬时三通阀 值0-1 直通/瞬时
    uint8_t DrainA;               // A桶排水  值0-1 停止/运行
    uint8_t DrainB;               // B桶排水  值0-1 停止/运行
    uint8_t DrainAComplete;
    uint8_t DrainBComplete;
    uint16_t SaveWarterA;                      // A桶存水量  值uint16 多少ml
    uint16_t SaveWarterB;                      // B桶存水量  值uint16 多少ml
    uint8_t SampleBottle1;                     // 留样瓶1-已经留样  值1-24
    uint8_t SampleBottle2;                     // 留样瓶2-准备留样  值1-24
    uint8_t SampleBottle3;                     // 留样瓶3-空瓶  值1-24
    uint8_t CurrentBucket;                     // 当前桶  值0-1 当前A桶/B桶  只采样流程
    uint8_t CurrentBucketRunState;             // 当前桶运行状态  值0-50
    uint8_t CurrentBucketCountDown[3];         // 当前桶倒计时 值：输入编码字符串 倒计时过程
    uint8_t ABucketState;                      // A桶状态 值0-50
    uint8_t ABucketCountDown[3];               // A桶状态倒计时 值：输入编码字符串 倒计时过程
    uint8_t BBucketState;                      // B桶状态 值0-50
    uint8_t BBucketCountDown[3];               // B桶状态倒计时 值：输入编码字符串 倒计时过程
    uint8_t SamplingTotalTimeCountDown[3];     // 采样总时倒计时 值：输入编码字符串 倒计时过程
    uint8_t SamplingIntervalCountDown[3];      // 采样间隔倒计时 值：输入编码字符串 倒计时过程
    uint8_t InstantOperationState;             // 瞬时操作状态 值0-50
    uint8_t InstantOperationStateCountDown[3]; // 瞬时操作状态倒计时 值：输入编码字符串 倒计时过程
    uint8_t Time[6];                           // 时间
    uint8_t State;                             // 仪器状态 值0-7  停止/运行/延时/空闲/维护/故障/启动/复位中
    uint8_t ExternalConnection;                // 外部连接 值0-3 断开/ai模块/数采仪/在线
} State;

/* 结构体定义完成后再声明全局变量，避免未定义类型错误 */
extern SampleConfig g_SampleConfig;
extern SampleDeliveryIntervalConfig g_DeliveryConfig;
extern RetainSampleModeConfig g_RetainSampleConfig;
extern CommSettingConfig g_CommSettingConfig;
extern SystemSettingConfig g_SystemSettingConfig;
extern CalibrationParams_t g_CalibrationParams;
extern SingleSampleTest_t g_SingleSampleTest;
extern State g_State;

// 因子数据存储（大岳485协议）
typedef struct
{
    uint16_t factorType; // 因子类型
    float factorValue;   // 因子值
} FactorData_t;

#define MAX_FACTOR_COUNT 10
extern FactorData_t g_FactorDataFromHost[MAX_FACTOR_COUNT];
extern uint8_t g_FactorCount;

// 留样信息记录（大岳485协议）
typedef struct
{
    uint16_t year, month, day, hour, minute, second;
    uint16_t result; // 0=失败, 1=成功
    uint16_t failReason;
    uint16_t startBottle;
    uint16_t bottleCount;
    uint16_t volume;
    uint16_t mode;
    uint16_t trigger;
    uint16_t addAcid;
    uint16_t acidType;
    uint16_t acidRatio;
    uint16_t sampleId[4];
} RetainSampleInfo_t;

extern RetainSampleInfo_t g_LastRetainInfo;
extern RetainSampleInfo_t g_LastInstantRetainInfo; // 瞬时留样信息

// 弃样信息记录（大岳485协议 0x00E7~0x00F4）
typedef struct {
    uint16_t year, month, day, hour, minute, second;  // 弃样时间
    uint16_t result;        // 执行结果: 0=失败, 1=成功
    uint16_t failReason;    // 失败原因码
    uint16_t startBottle;   // 起始瓶号 1~24
    uint16_t bottleCount;   // 总瓶数 1~24
    uint16_t sampleId[4];   // 样品编号(BCD格式)
} DiscardSampleInfo_t;

extern DiscardSampleInfo_t g_LastDiscardInfo;

// 门禁操作记录（用于大湖协议40053~40060）
typedef struct {
    uint16_t year, month, day, hour, minute, second;  // 操作时间
    uint32_t cardId;    // 门禁卡号或密码（0=手动开门）
} DoorAccessRecord_t;

// 门禁记录滑动窗口配置
#define DOOR_ACCESS_RECORD_MAX 10

// 门禁记录数组及计数（滑动窗口方式，参照断电记录）
extern DoorAccessRecord_t g_DoorAccessRecords[DOOR_ACCESS_RECORD_MAX];
extern uint8_t g_DoorAccessRecordCount;      // 记录计数

// 西安协议日志结构体定义
typedef struct
{
    uint16_t mode;     // 1=定时,2=时间等比例,3=流量等比例,4=流量触发,5=外部控制
    uint16_t bucketId; // 1=A桶,2=B桶
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t sequence; // 第几次/共几次
    uint16_t volume;   // 毫升
    uint16_t result;   // 1=成功,2=水量不足,3=故障,4=其它
} XianSamplingLog_t;

typedef struct
{
    uint16_t mode;     // 
    uint16_t bucketId; // 
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t volume; 
    uint16_t result; 
} XianDeliveryLog_t;

typedef struct
{
    uint16_t mode;   
    uint16_t reason; 
    uint16_t year;
    uint16_t month;
    uint16_t day;
    uint16_t hour;
    uint16_t minute;
    uint16_t bottleId; 
    uint16_t volume; 
		uint16_t result;   
} XianRetainLog_t;

extern XianSamplingLog_t g_XianSamplingLog;
extern XianDeliveryLog_t g_XianDeliveryLog;
extern XianRetainLog_t g_XianRetainLog;

// 四川协议留样编号结构（40109-40115）
typedef struct {
    uint16_t year;        // 40109: 年
    uint16_t month;       // 40110: 月
    uint16_t day;         // 40111: 日
    uint16_t start_hour;  // 40112: 开始时
    uint16_t start_min;   // 40113: 开始分
    uint16_t end_hour;    // 40114: 结束时
    uint16_t end_min;     // 40115: 结束分
} SichuanSampleId_t;

// 四川协议超标留样上下文
typedef struct {
    uint8_t pending;              // 是否有待处理的超标留样请求
    uint8_t bucket_id;            // 目标桶号
    uint16_t volume;              // 留样体积
    uint32_t request_time;        // 请求时间戳
    uint32_t window_end_time;     // 窗口结束时间（送样开始+AnalysisTime）
    SichuanSampleId_t sample_id;  // 留样编号
} SichuanExceedRetainCtx_t;

extern SichuanSampleId_t g_SichuanSampleId;
extern SichuanExceedRetainCtx_t g_SichuanExceedRetainCtx;

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

/* task handler */
extern TaskHandle_t task1_handle;
extern TaskHandle_t task2_handle;
extern TaskHandle_t task3_handle;
extern TaskHandle_t task4_handle;
extern TaskHandle_t task5_handle;
extern TaskHandle_t task6_handle;
extern TaskHandle_t task7_handle;
extern TaskHandle_t task8_handle;
extern TaskHandle_t task9_handle;
extern TaskHandle_t task10_handle;  /* 屏幕命令处理任务 */
/* declaration for task function */
void task01_func(void *pvParameters);
void task02_func(void *pvParameters);
void task03_func(void *pvParameters);
void task04_func(void *pvParameters);
void task05_func(void *pvParameters);
void task06_func(void *pvParameters);
void task07_func(void *pvParameters);
void task08_func(void *pvParameters);
void task09_func(void *pvParameters);
void task10_func(void *pvParameters);  /* 屏幕命令处理任务 */

/* queue handler */
extern QueueHandle_t queue_screen_handle;
extern QueueHandle_t queue_motor_handle;
extern QueueHandle_t queue_usb_handle;
extern QueueHandle_t queue_moduleAI_handle;
extern QueueHandle_t queue_analyser_handle;
extern QueueHandle_t queue_UART8_handle;
extern QueueHandle_t queue_screen_cmd;  /* 屏幕命令派发队列 */

/* mutex handler */
extern SemaphoreHandle_t mutex_flash_handle;
extern SemaphoreHandle_t mutex_4G_handle;
extern SemaphoreHandle_t g_screen_mtx;
extern SemaphoreHandle_t g_kvdb_mutex;  /* KVDB互斥锁 */
extern TaskHandle_t g_screen_waiter;
/* event handler */
extern EventGroupHandle_t event_handle;

/* 任务看门狗事件位定义 - 用于多任务软看门狗机制 */
#define TASK2_EVENT_BIT  (1 << 0)  // 0x01
#define TASK3_EVENT_BIT  (1 << 1)  // 0x02
#define TASK4_EVENT_BIT  (1 << 2)  // 0x04
#define TASK5_EVENT_BIT  (1 << 3)  // 0x08 - 屏幕分发任务
#define TASK6_EVENT_BIT  (1 << 4)  // 0x10
#define TASK7_EVENT_BIT  (1 << 5)  // 0x20
#define TASK8_EVENT_BIT  (1 << 6)  // 0x40
#define TASK9_EVENT_BIT  (1 << 7)  // 0x80
#define TASK10_EVENT_BIT (1 << 8)  // 0x100 - 屏幕命令处理任务

/* add user code begin 0 */

/* add user code end 0 */

void freertos_task_create(void);
void freertos_queue_create(void);
void freertos_semaphore_create(void);
void freertos_event_create(void);
void wk_freertos_init(void);

/* add user code begin 1 */

/* add user code end 1 */

#endif /* FREERTOS_APP_H */

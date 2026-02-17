/* add user code begin Header */
/**
 ******************************************************************************
 * File Name          : freertos_app.c
 * Description        : Code for freertos applications
 */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include <stdlib.h>
#include "sampling_time.h"
#include "work.h"
#include "screen.h"
#include "fal.h"
#include "flashdb.h"
#include "app_flashdb.h"
#include "spi_flash.h"
#include "at32f403a_407_rtc.h"
#include "fdb_low_lvl.h"
#include "retain_judge.h"
#include "wiegand.h"
#include "rtc.h"
#include "at32f403a_407_adc.h"
#include "ota.h"
#include "multi_button.h"
#include "bsp_button.h"
#include "Timetrigger.h"
#include "Flowtrigger.h"
#include "Switchtrigger.h"
#include "ota.h"
#include "screen_cache.h"
#include "mb.h"
#include "mb_instance.h"
#include "lbs_location.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */
/* 注意：TASK*_EVENT_BIT 宏定义已移至 freertos_app.h，供其他模块使用 */

// ★ 新增：系统初始化同步事件位
#define SCREEN_READY_BIT (1 << 9)    // 0x200 - 屏幕初始化完成
#define KVDB_READY_BIT   (1 << 10)   // 0x400 - KVDB完全就绪
#define TSDB_READY_BIT   (1 << 11)   // 0x800 - TSDB完全就绪
#define DRAIN_INIT_DONE_BIT (1 << 12) // 0x1000 - 开机排水完成（或无需排水）

/* ALL_TASKS_BITS: 根据UART7 485采集是否启用动态定义 */
#if USE_UART7_AI
#define ALL_TASKS_BITS (TASK2_EVENT_BIT | TASK3_EVENT_BIT | TASK4_EVENT_BIT | TASK5_EVENT_BIT | TASK6_EVENT_BIT | TASK7_EVENT_BIT | TASK8_EVENT_BIT | TASK9_EVENT_BIT | TASK10_EVENT_BIT)
#else
#define ALL_TASKS_BITS (TASK2_EVENT_BIT | TASK3_EVENT_BIT | TASK4_EVENT_BIT | TASK5_EVENT_BIT | TASK6_EVENT_BIT | TASK7_EVENT_BIT | TASK8_EVENT_BIT | TASK10_EVENT_BIT)
#endif

// UART6=4g模块/9600  UART4=串口屏/115200  UART2=上位机485/232可选/9600  UART7=AI模块/9600  UART5=电机485/19200  uart1=调试串口/115200  UART3=串口USB/115200

volatile uint32_t g_tmr2_seconds = 0, g_tmr3_milliseconds = 0, g_tmr4_seconds = 0;
// 通讯触发留样瓶数（1-24）
uint8_t g_comm_retain_bottle_count = 1;

/* ★ 留样中止标志：用于瓶盘复位时中止正在进行的留样 */
volatile uint8_t g_retention_abort_flag = 0;

/* ★ KVDB重载标志：是否需要Task01重新加载配置 */
static uint8_t g_need_kvdb_reload = 0;

/* ★ 开机排水超时检测标志 */
volatile uint8_t g_drain_ab_executed = 0;   // AB桶排水已执行标志（供中断访问）

// 通讯触发留样窗口上下文
CommRetainWindowContext g_comm_retain_ctx_a = {0};
CommRetainWindowContext g_comm_retain_ctx_b = {0};
uint8_t SendMqttFlag = 0;
volatile uint8_t Screenflag = 0; // ISR中更新，需要volatile
uint8_t SendMqttStatusFlag = 0;    // 10分钟状态发送标志
uint8_t SendMqttSettingsFlag = 0;  // 2小时设置发送标志
uint16_t MqttStatusCount = 0;      // 10分钟计数器(600秒)
uint16_t MqttSettingsCount = 0;    // 2小时计数器(7200秒)
uint8_t UART2_Buf[160], UART3_Buf[100], UART4_Buf[100], UART5_Buf[100], UART6_Buf[512], UART7_Buf[100], UART8_Buf[100];
__IO uint16_t adc1_ordinary_valuetab[200][9] = {0};
extern uint32_t testcount;

// 采样设置项
SampleConfig g_SampleConfig = {
    .BucketAB = 0,             // AB桶默认A桶
    .SamplingMode = 0,         // 采样模式  0=时间等比、1=定时采样、2=通讯触发、3=流量触发、4=开关触发
    .SamplingImproveTime = 15, // 采样提升时间30秒
    .SampleInterval = 15,      // 采样间隔15分钟
    .TubeHoldTime = 15,        // 采样管存放时间30秒
    .SampleVolume = 500,       // 单次采样量500毫升
    .CycleTime = 60,           // 周期时间60分钟
    .BlowbackTime = 15,        // 采样反吹时间15秒
    .BucketDrainTime = 60,     // 采样桶排空时间60秒
    .AnalysisTime = 55,        // 仪器分析时间50分钟
    .DischargeVolume = 3,      // 排放量3m3  每累计多少排放量进行一次留样 //预留待定
    .FlowRatio = 25,           // 流量比例25ml/m3    //预留待定
    .FlowStart = 15,           // 流量触发15m3/h //4-20mA换算 对应流量AD上限/通道6超标上限
    .FlowStop = 5              // 流量停止值5m3/h  //4-20mA换算 对应流量AD上限/通道6超标下限
};
// 送样设置项SampleDeliveryIntervalConfig
SampleDeliveryIntervalConfig g_DeliveryConfig = {
    .Enable = 1,     // 启用定时启动
    .StartHour = 0,  // 送样开始时间：0点
    .StartMin = 59,  // 送样开始时间：58分
    .Duration = 480, // 送样时长：240秒（4分钟）
    .EndHour = 1,    // 送样结束时间小时：1点 //自动计算得出，屏幕不能设置
    .EndMin = 2,     // 送样结束时间分钟：2分 //自动计算得出，屏幕不能设置
    .Interval = 0,  // 送样回抽：150秒
    .fixedhour = {0},
    .fixedmin = 0};
// 留样设置项
RetainSampleModeConfig g_RetainSampleConfig = {// g_RetainSampleConfig.SampleVolume 
    .Mode = 0,           // 留样模式 0=超标留样 1=直接留样 2=比对留样 3=通信触发 4=同步留样 5=只送不留  6=开关触发
    .bottleNumber = 1,   // 留样瓶号
    .EnableSample = 1,   // 启用留样 是否留样
    .EnableAcid = 0,     // 不启用加酸 是否加酸
    .EnableVacuum = 0,   // 启用排空 是否排空 g_RetainSampleConfig.EnableVacuum = 0,
    .SampleVolume = 500, // 单次留样量100ml
    .ParallelCount = 1,  // 平行样数量1
    .MixCount = 1,       // 混样次数1 多次留样在同一个瓶
    .TubeHoldTime = 15,  // 留样管存放时间15秒
    .BlowbackTime = 15,  // 留样反吹15秒  反吹在前
    .BackdrawTime = 15,  // 留样回抽15秒  回抽在后
    .channelLimits = {
        {1, 0.0f, 300.0f, 1}, // 通道1：启用，下限0，上限300     COD
        {2, 0.0f, 20.0f, 1},  // 通道2：启用，下限0，上限20      氨氮
        {3, 0.0f, 1.0f, 1}, // 通道3：启用，下限0，上限1.0       总磷
        {4, 0.0f, 20.0f, 1},   // 通道4：启用，下限0，上限20     总氮
        {5, 0.0f, 0.0f, 0},   // 通道5：禁用
        {6, 0.0f, 0.0f, 0}    // 通道6：禁用
    },
    .channelData = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},    // 9个通道数据初始化为0
    .channelCurrent = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}, // 9个通道电流初始化为0
    .channelDataType = {1, 2, 3, 4, 0, 0},                                    // 通道数据类型：COD, NH4N, 其他
    .channelCals = {
        // 输入AD填入校准AD，校准值是真实值的满量程数值，根据这个满量程数值显示 变量地址5082-5088的真实数值
        {0, 0, 12, 300.0f}, // 通道0校准：电流数值 4-20mA转换的AD值  0点AD=819  校准AD=4095  校准值=0
        {0, 0, 12, 20.0f}, // 通道1校准
        {0, 0, 12, 1.0f}, // 通道2校准
        {0, 0, 12, 20.0f}, // 通道3校准
        {0, 0, 0, 1.0f}, // 通道4校准
        {0, 0, 0, 1.0f}, // 通道5校准
        {0, 0, 0, 1.0f}, // 通道6校准（备用通道）
        {0, 0, 0, 0.0f},      // 通道7：2.5V基准，不需要校准换算
        {0, 0, 0, 1.0f}       // 通道8：流量专用，不使用此校准参数（使用FlowMeterBase）
    }};
// 高级设置项1
CommSettingConfig g_CommSettingConfig = {
    .Protocol = 0,           // 通讯协议：485=0 232=1
    .DeviceAddr = 1,         // 设备地址号：1
    .AutoCalibration = COMM_PROTOCOL_DAYUE, // 协议选择：0=大岳 1=大湖 2=四川管控
    .FlowADUpper = 0,     // 流量触发阈值
    .FlowADLower = 0,        // 流量校准电流值
    .FlowMeterBase = 100.0f, // 流量校准流量值
    .IDSET = "CYJ2601BZL000C",//CYJ2601BZL032C   CYJ_12121H1001B
    .IPSET = "124.222.59.221"};

// 大岳485协议全局变量
FactorData_t g_FactorDataFromHost[MAX_FACTOR_COUNT] = {0};
uint8_t g_FactorCount = 0;
RetainSampleInfo_t g_LastRetainInfo = {.result = 1};  // 初始为成功/正常
RetainSampleInfo_t g_LastInstantRetainInfo = {.result = 1}; // 瞬时留样信息，初始为成功
DiscardSampleInfo_t g_LastDiscardInfo = {.result = 1};     // 弃样信息，初始为成功

// 门禁记录滑动窗口（参照断电记录）
DoorAccessRecord_t g_DoorAccessRecords[DOOR_ACCESS_RECORD_MAX] = {0};
uint8_t g_DoorAccessRecordCount = 0;      // 记录计数

// 西安485协议全局变量
XianSamplingLog_t g_XianSamplingLog = {0};
XianDeliveryLog_t g_XianDeliveryLog = {0};
XianRetainLog_t g_XianRetainLog = {0};

// 四川协议全局变量
SichuanSampleId_t g_SichuanSampleId = {0};
SichuanExceedRetainCtx_t g_SichuanExceedRetainCtx = {0};

// FreeModbus实例
MBInstance_t g_mb_dayue;  // Modbus实例 (USART2，支持大岳/大湖/四川管控)
#if USE_PROTOCOL_XIAN
MBInstance_t g_mb_xian;   // 西安协议实例 (UART7)
#endif

// 高级设置项2
SystemSettingConfig g_SystemSettingConfig = {
    .Year = 2026,                             // 年
    .Month = 1,                               // 月
    .Day = 1,                                 // 日
    .Hour = 0,                                // 时
    .Minute = 0,                              // 分
    .Second = 0,                              // 秒
    .WaterStationMode = 0,                    // 水站模式：不启用
    .AutoRunMode = 1,                         // 自动运行：启用
    .SoftwareSerial = "CYJ2601BZL033C",    // 软件序列号
    .SoftwareCoreVer = "V1035",              // 软件核心板版本
    .SoftwareLcdVer = "V2048",               // 软件液晶屏版本
    .HardwareBaseVer = "V1.0.0",              // 硬件底板版本
    .HardwareCoreVer = "V1.0.0",              // 硬件核心板版本
    .HardwareLcdVer = "V1.0.0",               // 硬件液晶屏版本
    .CardId = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, // 门禁卡号初始化为0 g_CalibrationParams.retainSampleCalib.realValue3
    .Motorspeed = 150};
// 精度校准 应该出厂就校准正确
CalibrationParams_t g_CalibrationParams = {
    .samplingCalib = {// 采样
                      .time1 = 2,
                      .realValue1 = 10,
                      .time2 = 90,
                      .realValue2 = 500,
                      .time3 = 180,
                      .realValue3 = 1000},
    .retainSampleCalib = {// 留样
                          .time1 = 2,
                          .realValue1 = 10,
                          .time2 = 20,
                          .realValue2 = 100,
                          .time3 = 38,
                          .realValue3 = 200},
    .acidAdditionCalib = {// 加酸
                          .time1 = 2,
                          .realValue1 = 10,
                          .time2 = 57,
                          .realValue2 = 100,
                          .time3 = 57,
                          .realValue3 = 100},
    .tempCalib = {// 温度  没卵用
                  .inputAD = 0,
                  .zeroPointAD = 0,
                  .calibAD = 0,
                  .calibValue = 0,
                  .setTemp = 0,
                  .upperDev = 0,
                  .lowerDev = 0,
                  .zeroTemp = 0}};
// 流程测试参数
SingleSampleTest_t g_SingleSampleTest = {
    .sampleBucket = 1,
    .sampleVolume = 500,
    .deliveryMode = 2,
    .deliveryVolume = 500,
    .retainMode = 2,
    .retainVolume = 500,
    .bottleNumber = 1,
    .turnbottleNumber = 1, // 转动到位瓶号  1-24
    .emptybottleNumber = 1 // 排空瓶号  1-24
};

// 屏幕要求状态写入结构体
State g_State = {
    .SamplingMotor = 1,                           // 采样蠕动泵   值0-2 停止/采样/反吹
    .DeliveryMotor = 0,                           // 送样蠕动泵   值0-2 停止/正转/反转
    .InletThreeWayValve = 2,                      // 进水三通阀 值1-A桶 2-B桶
    .OutletThreeWayValve = 0,                     // 出水三通阀  A桶/0 B桶/1
    .SampleThreeWayValve = 1,                     // 送样三通阀 值0-1 开/关
    .InstantThreeWayValve = 1,                    // 瞬时三通阀 值0-1 直通/瞬时
    .DrainA = 0,                                  // A桶排水  值0-1 停止/运行
    .DrainB = 0,                                  // B桶排水  值0-1 停止/运行
    .SaveWarterA = 0X0,                           // A桶存水量  值uint16 多少ml
    .SaveWarterB = 0X0,                           // B桶存水量  值uint16 多少ml
    .SampleBottle1 = 0,                           // 留样瓶1-已经留样  值1-24
    .SampleBottle2 = 0,                           // 留样瓶2-准备留样  值1-24
    .SampleBottle3 = 0,                           // 留样瓶3-空瓶  值1-24
    .CurrentBucket = 0,                           // 当前桶  值0-1 当前A桶/B桶  只采样流程
    .CurrentBucketRunState = 0x0,                 // 当前桶运行状态  值0-50
    .CurrentBucketCountDown = {0x00, 0x00, 0x00}, // 当前桶倒计时 值：输入编码字符串 倒计时过程
    .ABucketState = 0X0,                          // A桶状态 值0-50
    .ABucketCountDown = {0, 0, 0},                // A桶状态倒计时 值：输入编码字符串 倒计时过程
    .BBucketState = 0X0,                          // B桶状态 值0-50
    .BBucketCountDown = {0, 0, 0},                // B桶状态倒计时 值：输入编码字符串 倒计时过程
    .SamplingTotalTimeCountDown = {0, 0, 0},      // 采样总时长倒计时 值：输入编码字符串 倒计时过程
    .SamplingIntervalCountDown = {0, 0, 0},       // 采样间隔倒计时 值：输入编码字符串 倒计时过程
    .InstantOperationState = 0x0,                 // 瞬时操作状态 值0-50
    .InstantOperationStateCountDown = {0, 0, 0},  // 瞬时操作状态倒计时 值：输入编码字符串 倒计时过程
    .Time = {0x25, 0x09, 0x02, 0x15, 0x26, 0x44}, // 时间 值：输入编码字符串 RTC时间
    .State = 0,                                   // 状态 值0-50
    .ExternalConnection = 0,                      // 外部连接 值0-1
};
/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */

/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* task handler */
TaskHandle_t task1_handle;
TaskHandle_t task2_handle;
TaskHandle_t task3_handle;
TaskHandle_t task4_handle;
TaskHandle_t task5_handle;
TaskHandle_t task6_handle;
TaskHandle_t task7_handle;
TaskHandle_t task8_handle;
TaskHandle_t task9_handle;
TaskHandle_t task10_handle;  /* 屏幕命令处理任务 */

/* queue handler */
QueueHandle_t queue_screen_handle;
QueueHandle_t queue_motor_handle;
QueueHandle_t queue_usb_handle;
QueueHandle_t queue_moduleAI_handle;
QueueHandle_t queue_analyser_handle;
QueueHandle_t queue_UART8_handle;
QueueHandle_t queue_screen_cmd;  /* 屏幕命令派发队列 */

/* mutex handler */
SemaphoreHandle_t mutex_flash_handle;
SemaphoreHandle_t mutex_4G_handle;
SemaphoreHandle_t g_screen_mtx;
TaskHandle_t g_screen_waiter;

/* event handler */
EventGroupHandle_t event_handle;

/* Idle task control block and stack */
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

static StaticTask_t idle_task_tcb;

/* External Idle and Timer task static memory allocation functions */
extern void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize);

/*
  vApplicationGetIdleTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize)
{
    *ppxIdleTaskTCBBuffer = &idle_task_tcb;
    *ppxIdleTaskStackBuffer = &idle_task_stack[0];
    *pulIdleTaskStackSize = (uint32_t)configMINIMAL_STACK_SIZE;
}

/* add user code begin 1 */

/* add user code end 1 */

/**
 * @brief  initializes all task.
 * @param  none
 * @retval none
 */
void freertos_task_create(void)
{

    xTaskCreate(task01_func, "task1", 2048, NULL, 4, &task1_handle); // 优先级4：看门狗（关键）

    xTaskCreate(task02_func, "task2", 1024, NULL, 3, &task2_handle); // 优先级3：通讯（关键）

    xTaskCreate(task03_func, "task3", 2048, NULL, 2, &task3_handle); // 优先级2：调度器（重要）

    xTaskCreate(task04_func, "task4", 1024, NULL, 2, &task4_handle); // 优先级2：留样判定（重要）

    xTaskCreate(task05_func, "task5", 4096, NULL, 4, &task5_handle); // 优先级4：屏幕分发器（最高），栈加大

    xTaskCreate(task06_func, "task6", 2048, NULL, 2, &task6_handle); // 优先级1：ADC采集（需要定时执行）

    xTaskCreate(task07_func, "task7", 1024, NULL, 2, &task7_handle); // 优先级0：低优先级任务

    xTaskCreate(task08_func, "task8", 1024, NULL, 4, &task8_handle); // 优先级0：低优先级任务

#if USE_UART7_AI
    xTaskCreate(task09_func, "task9", 1024, NULL, 3, &task9_handle); // UART7 485采集任务
#endif

    xTaskCreate(task10_func, "task10", 2048, NULL, 2, &task10_handle); // 优先级2：屏幕命令处理任务
}

/**
 * @brief  initializes all queue.
 * @param  none
 * @retval none
 */
void freertos_queue_create(void)
{
    queue_screen_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_motor_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_usb_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_moduleAI_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_analyser_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_UART8_handle = xQueueCreate(16, sizeof(UartMessage));
    queue_screen_cmd = xQueueCreate(8, sizeof(ScreenCommand)); /* 屏幕命令派发队列 */
}

/**
 * @brief  initializes all semaphore.
 * @param  none
 * @retval none
 */
void freertos_semaphore_create(void)
{
    mutex_flash_handle = xSemaphoreCreateMutex();
    mutex_4G_handle = xSemaphoreCreateMutex();

    // 创建调试缓冲区互斥锁
    extern SemaphoreHandle_t debug_mutex;
    debug_mutex = xSemaphoreCreateMutex();
    g_screen_mtx = xSemaphoreCreateMutex();

    // 创建KVDB互斥锁，确保在任务启动前初始化
    extern SemaphoreHandle_t g_kvdb_mutex;
    if (g_kvdb_mutex == NULL)
    {
        g_kvdb_mutex = xSemaphoreCreateMutex();
    }
}

/**
 * @brief  initializes all event.
 * @param  none
 * @retval none
 */
void freertos_event_create(void)
{
    /* Create the event */
    event_handle = xEventGroupCreate();
}

/**
 * @brief  带超时的KVDB初始化
 * @param  timeout_ms: 超时时间（毫秒）
 * @retval 0: 成功, -1: 失败
 */
static int kv_init_with_timeout(uint32_t timeout_ms)
{
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);

    while ((xTaskGetTickCount() - start_time) < timeout_ticks)
    {
        wdt_counter_reload();  // 喂狗

        // 尝试初始化FAL
        int fal_result = fal_init();
        if (fal_result > 0)
        {
            // FAL初始化成功，尝试初始化KVDB
            if (cfg_kv_init())
            {
                return 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));  // 等待10ms再重试
    }

    return -1;
}

/**
 * @brief  freertos init and begin run.
 * @param  none
 * @retval none
 */
void wk_freertos_init(void)
{
//    /* 第1步：带超时的KVDB初始化并加载配置到全局变量（必须在创建任务之前！） */
//    printf("\r\n[初始化] 步骤1: 快速KVDB初始化（超时500ms）...\r\n");

    // ★ 修改：尝试快速初始化KVDB，超时2000ms
    int kvdb_result = kv_init_with_timeout(2000);

    if (kvdb_result == 0)
    {
        // KVDB快速初始化成功，加载配置
//        printf("[初始化] KVDB快速初始化成功，加载配置...\r\n");
        settings_init_load();
        g_need_kvdb_reload = 0;  // ★ 标记不需要重新加载
    }
    else
    {
        // ★ 失败：使用安全默认配置
        // 设置安全默认值（确保ADC必须启动）
        g_RetainSampleConfig.Mode = 0;           // 超标留样模式
        g_SampleConfig.SamplingMode = 3;         // 流量触发
        g_SampleConfig.SampleVolume = 500;       // 默认500ml
        g_SampleConfig.SampleInterval = 15;      // 默认15分钟采样间隔
        g_SampleConfig.CycleTime = 60;           // 默认60分钟周期时间

        g_need_kvdb_reload = 1;  // ★ 标记需要Task01重新加载

        // 不记录错误到TSDB（此时TSDB未初始化）
    }
    
    /* 初始化缓存机制 */
    kvdb_cache_init();
    tsdb_cache_init();

    // 打印关键配置验证
    extern RetainSampleModeConfig g_RetainSampleConfig;
    /* 第2步：根据配置条件启动ADC连续转换 */
    // 只有以下情况需要ADC：
    // 1. 超标留样模式（Mode == 0）：需要监测模拟量通道
    // 3. 流量触发模式（SamplingMode == 3）：需要监测流量通道
    if (!retain_judge_uart7_should_run() &&
        (g_RetainSampleConfig.Mode == 0 || g_SampleConfig.SamplingMode == 3)) {
        adc_ordinary_software_trigger_enable(ADC1, TRUE);
    }


    /* enter critical */
    taskENTER_CRITICAL();

    /* 第3步：创建FreeRTOS资源 */
    freertos_semaphore_create();
    freertos_queue_create();
    freertos_event_create();

    /* 第4步：创建任务（此时KVDB配置、ADC已就绪，TSDB将在task7中初始化）*/
    freertos_task_create();

    /* exit critical */
    taskEXIT_CRITICAL();

    /* 第5步：启动调度器 */
    vTaskStartScheduler();
}

// 任务1：系统启动与看门狗监控
/**
 * @brief task1 function.
 * @param  none
 * @retval none
 */
// 任务1：系统启动与看门狗监控
void task01_func(void *pvParameters)
{
    EventBits_t bits;
    static uint8_t wdt_timeout_count = 0;  // 连续超时计数器

    // ★ 开机排水超时检测已移至TMR2硬件中断（at32f403a_407_int.c）
    printf("[任务1] 启动\r\n");

    /* ========== 阶段1：等待屏幕就绪 ========== */
    bits = xEventGroupWaitBits(
        event_handle,
        SCREEN_READY_BIT,
        pdFALSE,  // 不清除标志
        pdTRUE,   // 等待所有位
        pdMS_TO_TICKS(2000)  // 超时2秒
    );

    /* ========== 阶段2：完整KVDB初始化（如果需要） ========== */
        if (g_need_kvdb_reload)
        {

            // 重新尝试KVDB初始化（更长超时）
            int kvdb_result = kv_init_with_timeout(5000);

            if (kvdb_result == 0)
            {
                settings_init_load();
                // 刷新串口屏显示（因为之前用的是默认值）
                Screen_init();
            }
            else
            {
                // 错误将在TSDB就绪后记录
            }
    }
    else
    {
        // KVDB已在启动前加载
    }

    // 设置KVDB就绪标志
    xEventGroupSetBits(event_handle, KVDB_READY_BIT);

    /* ========== 阶段3：TSDB初始化 ========== */
    fdb_start_tasks();  // 原有逻辑，内部有超时保护

    // 等待TSDB真正就绪（带超时）
    uint32_t tsdb_wait_start = xTaskGetTickCount();
    uint32_t tsdb_wait_timeout = pdMS_TO_TICKS(5000);
    while (!tsdb_is_ready() &&
           (xTaskGetTickCount() - tsdb_wait_start) < tsdb_wait_timeout)
    {
        wdt_counter_reload();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (tsdb_is_ready())
    {
        xEventGroupSetBits(event_handle, TSDB_READY_BIT);
    }
    else
    {
        printf("[任务1] 警告：TSDB初始化超时，未就绪\r\n");
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    /* ========== 阶段3.5：废水排放模块初始化 ========== */
    waste_water_drain_init();

    /* ========== 阶段4：断电恢复检查（原有逻辑） ========== */
    extern RetainBottleState g_RetainBottleState;
    extern State g_State;

    // 从KVDB加载瓶位状态检查
    cfg_load_retain_state(&g_RetainBottleState);

    // 如果系统处于待机状态且瓶位状态有效，则执行断电恢复
    if (g_State.State == 0 && g_RetainBottleState.currentBottle > 0 && g_RetainBottleState.currentBottle <= 24)
    {
        // 导入启动函数声明
        extern void system_start_sequence(SystemStartMode start_mode);
        system_start_sequence(START_MODE_POWER_RECOVERY);
    }

    /* ========== 阶段5：看门狗监控循环（原有逻辑保持不变） ========== */
    while (1)
    {
        bits = xEventGroupWaitBits(event_handle, ALL_TASKS_BITS, pdTRUE, pdTRUE, pdMS_TO_TICKS(8000));
        if ((bits & ALL_TASKS_BITS) == ALL_TASKS_BITS)
        {
            wdt_counter_reload();
            wdt_timeout_count = 0;  // 重置计数器
            //			printf( "WWGT OK!!!\r\n" );
        }
        else
        {
            wdt_timeout_count++;
            printf("看门狗超时, 标志位=%x (第%d次)\r\n", bits, wdt_timeout_count);

            // 分析哪个任务没有响应
            printf("[分析] 缺失的任务标志位: ");
            if (!(bits & TASK2_EVENT_BIT)) printf("TASK2 ");
            if (!(bits & TASK3_EVENT_BIT)) printf("TASK3 ");
            if (!(bits & TASK4_EVENT_BIT)) printf("TASK4 ");
            if (!(bits & TASK5_EVENT_BIT)) printf("TASK5 ");
            if (!(bits & TASK6_EVENT_BIT)) printf("TASK6 ");
            if (!(bits & TASK7_EVENT_BIT)) printf("TASK7 ");
            if (!(bits & TASK8_EVENT_BIT)) printf("TASK8 ");
            if (!(bits & TASK9_EVENT_BIT)) printf("TASK9 ");
            printf("\r\n");

            // 如果是连续超时，可能某个任务卡死了
            if (wdt_timeout_count >= 10)
            {
                printf("[严重] 连续10次看门狗超时，系统复位\r\n");

                // 记录系统复位事件
                uint32_t reset_reason = 0xDEAD0002;  // 看门狗超时复位
                tsdb_event_append(0x0FFF, &reset_reason, sizeof(reset_reason));

                // 延迟100ms后复位
                vTaskDelay(pdMS_TO_TICKS(100));
                NVIC_SystemReset();
            }
            else
            {
                // 第一次或第二次超时，先尝试恢复
                printf("看门狗超时，尝试恢复...\r\n");

                // 记录超时事件
                uint32_t timeout_event = 0xDEAD0003 | (wdt_timeout_count << 8) | (bits & 0xFF);
                tsdb_event_append(0x0FFF, &timeout_event, sizeof(timeout_event));

                // 喂狗给系统恢复时间
                wdt_counter_reload();
                tmr_counter_enable(TMR4, FALSE);  // 原有的保护逻辑

                // 增加延迟，给卡死任务更多恢复时间
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        // 定期保存心跳时间戳（避免TASK8频繁写入Flash）
        extern void power_heartbeat_periodic_save(void);
        power_heartbeat_periodic_save();

        vTaskDelay(1000);
    }
}

// 任务2：4G/上位机通讯
/**
 * @brief task2 function - 4G/上位机通讯
 * @param  none
 * @retval none
 */
// 任务2：4G/上位机通讯
void task02_func(void *pvParameters)
{
        uint32_t notifyValue;
        uint8_t mqtt_retry_count = 0;
        uint8_t mqtt_connected = 0;
        uint32_t last_debug_send = 0;
        TickType_t ota_last_cmd_tick = 0;
        uint8_t ota_started = 0;
        static uint16_t s_saved_motorspeed = 150;  // 保存锁机前的转速

    // 等待任务1完成初始化
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 获取IMEI并设置为IDSET（在MQTT初始化之前）
    IMEI_GetAndSetIDSET(USART6, (char *)UART6_Buf);
    xEventGroupSetBits(event_handle, TASK2_EVENT_BIT); // 喂狗

    //    // MQTT初始化
        while (!mqtt_connected && mqtt_retry_count < 3)
        {
            xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

            if (MqttInit((char *)UART6_Buf, USART6))
            {
                mqtt_connected = 1;
                xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);
                
                // ★ MQTT连接成功后，启动LBS定位（非阻塞）
                vTaskDelay(pdMS_TO_TICKS(1000));
                LBS_StartInit(USART6);
                
                break;
            }
            else
            {
                mqtt_retry_count++;
               if (mqtt_retry_count < 5)
               {
                    printf("MQTT连接失败，第%d次重试... (等待10秒)\r\n", mqtt_retry_count);
                    vTaskDelay(pdMS_TO_TICKS(10000));
                    xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);
                }
            }
        }

    while (1)
    {
        xEventGroupSetBits(event_handle, TASK2_EVENT_BIT);

        //        // 优先处理调试消息
                extern uint8_t debug_mode;
                extern uint16_t debug_buffer_index;
                uint32_t current_tick = xTaskGetTickCount();
               if (debug_mode && debug_buffer_index > 0)
                {
                    // 限制调试消息发送频率，避免过于频繁
                    if ((current_tick - last_debug_send) > pdMS_TO_TICKS(200))
                    {
                        extern uint8_t ProcessDebugCache(usart_type * usart_x);
                        if (ProcessDebugCache(USART6))
                        {
                            last_debug_send = current_tick;
                            // 发送成功后短暂延迟，继续处理调试消息
                            vTaskDelay(pdMS_TO_TICKS(50));
                            continue;
                        }
                    }
                }

        //        // 处理任务通知
                if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifyValue, pdMS_TO_TICKS(1000)) == pdTRUE)
                {
                    // ★ LBS定位响应检测（非阻塞，优先检查）
                    if (LBS_GetState() == LBS_STATE_WAIT_RESP) {
                        if (strstr((char *)UART6_Buf, "+MLBSLOC:") != NULL) {
                            LBS_CheckResponse((char *)UART6_Buf);
                        }
                    }
                    
                    if (notifyValue == 0x66)
                    {   // OTA程序
                        ota_last_cmd_tick = xTaskGetTickCount();

                        // 第一次收到OTA命令，挂起其他任务
                        if (!ota_started) {
                            printf("[OTA] 启动升级流程，挂起其他任务...\r\n");
                            vTaskSuspend(task1_handle);
                            vTaskSuspend(task3_handle);
                            vTaskSuspend(task4_handle);
                            vTaskSuspend(task5_handle);
                            vTaskSuspend(task6_handle);
                            vTaskSuspend(task7_handle);
                            vTaskSuspend(task8_handle);
                            vTaskSuspend(task9_handle);
													  MotorStop(1); // 采样蠕动泵（主要）
														MotorStop(2); // 送样蠕动泵
                            ota_started = 1;
                        }

                        // 处理OTA数据
                        OTA_Process((char *)UART6_Buf, USART6);

                        // 检查OTA是否完成
                        if (OTA_GetState() == OTA_STATE_COMPLETE) {
                            printf("[OTA] 升级完成，准备重启系统...\r\n");
                            NVIC_SystemReset();
                        } else if (OTA_GetState() == OTA_STATE_ERROR) {
                            printf("[OTA] 升级失败，恢复任务执行...\r\n");
                            ota_started = 0;
                            ota_last_cmd_tick = 0;
                        }
                    }
                    // OTA过程中处理MQTTRXCV响应
                    else if (ota_started &&
                             (OTA_HasPendingPayload() ||
                              strstr((char *)UART6_Buf, "RXCV:") != NULL ||
                              strstr((char *)UART6_Buf, "URC: \"publish\"") != NULL))
                    {
                        ota_last_cmd_tick = xTaskGetTickCount();
                        OTA_Process((char *)UART6_Buf, USART6);
                        if (OTA_GetState() == OTA_STATE_COMPLETE) {
                            printf("[OTA] 升级完成，准备重启系统...\r\n");
                            NVIC_SystemReset();
                        } else if (OTA_GetState() == OTA_STATE_ERROR) {
                            printf("[OTA] 升级失败，恢复任务执行...\r\n");
                            ota_started = 0;
                            ota_last_cmd_tick = 0;
                        }
                    }
                    // SETIP处理
                    else if (notifyValue == 0x99)
                    {
                        char new_ip[20] = {0};
                        if (extract_ip_from_setmcu((char *)UART6_Buf, new_ip, sizeof(new_ip)) > 0)
                        {
                            printf("New IP: %s\r\n", new_ip);
                            strncpy(g_CommSettingConfig.IPSET, new_ip, sizeof(g_CommSettingConfig.IPSET) - 1);
                            g_CommSettingConfig.IPSET[sizeof(g_CommSettingConfig.IPSET) - 1] = '\0';
                            cfg_save_comm(&g_CommSettingConfig);
                            printf("IP saved to flash\r\n");
                            NVIC_SystemReset();
                        }
                    }
                    // 锁机命令
                    else if (notifyValue == 0xAA)
                    {
                        printf("[MQTT] 收到锁机命令\r\n");
                        if (g_SystemSettingConfig.Motorspeed > 0) {
                            s_saved_motorspeed = g_SystemSettingConfig.Motorspeed;
                        }
                        g_SystemSettingConfig.Motorspeed = 0;
                        cfg_save_system(&g_SystemSettingConfig);
                        printf("[锁机] 电机速度已设为0，原转速%d已保存\r\n", s_saved_motorspeed);
                    }
                    // 解锁命令
                    else if (notifyValue == 0xAB)
                    {
                        printf("[MQTT] 收到解锁命令\r\n");
                        g_SystemSettingConfig.Motorspeed = s_saved_motorspeed;
                        cfg_save_system(&g_SystemSettingConfig);
                        printf("[解锁] 电机速度已恢复为%d\r\n", s_saved_motorspeed);
                    }
                    // 转速调整命令
                    else if (notifyValue == 0xAC)
                    {
                        char *spd_pos = strstr((char *)UART6_Buf, "_SPD");
                        if (spd_pos != NULL) {
                            char *start = spd_pos + 4;
                            char *end = strchr(start, 'Y');
                            if (end != NULL && (end - start) < 8) {
                                char speed_str[8] = {0};
                                strncpy(speed_str, start, end - start);
                                uint16_t new_speed = (uint16_t)atoi(speed_str);
                                if (new_speed <= 300) {
                                    g_SystemSettingConfig.Motorspeed = new_speed;
                                    cfg_save_system(&g_SystemSettingConfig);
                                    printf("[转速] 已调整为: %d\r\n", new_speed);
                                }
                            }
                        }
                    }
                    // 调试指令处理
                    else if (CheckDebugCommand((char *)UART6_Buf))
                    {
                        // 调试指令已处理
                        memset(UART6_Buf, 0, sizeof(UART6_Buf)); // 清空缓冲区
                    }
                    // 调试消息通知（来自fputc）
                    else if ((notifyValue & 0x77) && debug_mode)
                    {
                        // 继续循环，优先发送调试消息
                        continue;
                    }
                    else
                    {
                        // 常规数据处理...
                    }
                }
                else
                {
                    // ★ 无通知时也检查LBS超时
                    if (LBS_GetState() == LBS_STATE_WAIT_RESP) {
                        LBS_CheckResponse(NULL);  // 仅检查超时
                    }
                }

        // MQTT重连逻辑 - 完全非阻塞设计
                if (!mqtt_connected && (!debug_mode || debug_buffer_index == 0))
                {
                    static uint32_t last_reconnect_try = 0;
                    static uint8_t reconnect_retry_count = 0;
                    static uint8_t reconnect_slow_mode = 0;      // 0=快速, 1=慢速
                    static uint8_t slow_reconnect_state = 0;     // 0=等待复位, 1=已复位等待连接
                    static uint32_t slow_reset_tick = 0;         // 复位时间戳

                    if (!reconnect_slow_mode)
                    {
                        // === 快速重连阶段：每60秒尝试1次 ===
                        if ((current_tick - last_reconnect_try) > pdMS_TO_TICKS(60000))
                        {
                            last_reconnect_try = current_tick;
                            reconnect_retry_count++;
                            printf("[MQTT] 快速重连第%d次...\r\n", reconnect_retry_count);

                            if (MqttInit((char *)UART6_Buf, USART6))
                            {
                                mqtt_connected = 1;
                                reconnect_retry_count = 0;
                                printf("[MQTT] 重连成功！\r\n");
                                
                                // ★ MQTT重连成功后，尝试LBS定位
                                vTaskDelay(pdMS_TO_TICKS(1000));
                                LBS_StartInit(USART6);
                            }
                            else if (reconnect_retry_count >= 10)
                            {
                                // 进入慢速重连阶段
                                reconnect_slow_mode = 1;
                                slow_reconnect_state = 0;
                                printf("[MQTT] 快速重连10次失败，进入慢速重连（每小时1次）\r\n");
                            }
                        }
                    }
                    else
                    {
                        // === 慢速重连阶段：每小时复位后尝试1次 ===
                        if (slow_reconnect_state == 0)
                        {
                            // 状态0：等待1小时后复位
                            if ((current_tick - last_reconnect_try) > pdMS_TO_TICKS(3600000))
                            {
                                printf("[MQTT] 慢速重连：复位4G模组...\r\n");
                                ResetModle(USART6);
                                slow_reset_tick = current_tick;
                                slow_reconnect_state = 1;  // 进入等待连接状态
                            }
                        }
                        else
                        {
                            // 状态1：复位后等待6秒再尝试连接
                            if ((current_tick - slow_reset_tick) > pdMS_TO_TICKS(6000))
                            {
                                printf("[MQTT] 慢速重连：尝试连接...\r\n");
                                last_reconnect_try = current_tick;

                                if (MqttInit((char *)UART6_Buf, USART6))
                                {
                                    mqtt_connected = 1;
                                    reconnect_retry_count = 0;
                                    reconnect_slow_mode = 0;
                                    slow_reconnect_state = 0;
                                    printf("[MQTT] 重连成功！\r\n");
                                    
                                    // ★ MQTT重连成功后，尝试LBS定位
                                    vTaskDelay(pdMS_TO_TICKS(1000));
                                    LBS_StartInit(USART6);
                                }
                                else
                                {
                                    slow_reconnect_state = 0;  // 回到等待状态
                                    printf("[MQTT] 慢速重连失败，等待下一小时\r\n");
                                }
                            }
                        }
                    }
                }

        // OTA超时保护：120s未收到上位机指令则失败并重启
                if (ota_started && ota_last_cmd_tick != 0 &&
                    (xTaskGetTickCount() - ota_last_cmd_tick) > pdMS_TO_TICKS(120000))
                {
                    printf("[OTA] 120s未收到上位机指令，升级失败，系统重启\r\n");
                    OTA_Error(USART6);
                    ota_started = 0;
                    ota_last_cmd_tick = 0;
                }

        // 正常的MQTT消息发送（在非调试模式或调试队列为空时）
                if (mqtt_connected && (!debug_mode || debug_buffer_index == 0))
                {
                    
                    // 新增: 15分钟状态上报(4条)
                    if (SendMqttStatusFlag)
                    {
                        if (OTA_GetState() == OTA_STATE_IDLE)
                        {
                            /* 仅发送10分钟事件缓存，取消原快照上报 */
                            MqttSendRecentEvents10m(USART6);
                            SendMqttStatusFlag = 0;
                        }
                    }

                    // 新增: 1小时设置上报(5条)，发送前先同步时间
                    if (SendMqttSettingsFlag)
                    {
                        if (OTA_GetState() == OTA_STATE_IDLE)
                        {
                            // 先同步时间
                            timesyc(USART6, (char *)UART6_Buf);
                            // 再发送设置
                            MqttSendSettingsAll((char *)UART6_Buf, USART6);
                            SendMqttSettingsFlag = 0;
                        }
                    }
                }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief task3 function.
 * @param  none
 * @retval none
 */
// 首次开机初始化标志（静态变量，移到函数外以便外部访问）
static uint8_t first_boot_init_done = 0;

/**
 * @brief 重置Task3初始化标志（用于系统复位后重新初始化）
 */
void task03_reset_init_flag(void)
{
    first_boot_init_done = 0;
}

// 任务3：瓶盘复位与自动运行准备
void task03_func(void *pvParameters)
{
    /* add user code begin task03_func 0 */

    /* add user code end task03_func 0 */

    /* add user code begin task03_func 2 */
    extern uint8_t sample_id_generator_init(void);
    extern uint8_t cache_manager_init(void);
    /* add user code end task03_func 2 */

    /* Infinite loop */
    while (1)
    {
        /* add user code begin task03_func 1 */

        // 喂狗
        xEventGroupSetBits(event_handle, TASK3_EVENT_BIT);

        // ★ 开机排水超时检测已移至TMR2硬件中断（at32f403a_407_int.c），不受任务卡死影响

        // ? 首次开机初始化（AutoRunMode=1时执行排空+启动调度器）
        // ★ 留样瓶初始化已移至首次留样时执行（bottle_ensure_initialized）
        static uint8_t bottle_init_state = 3;  // ★ 直接跳过瓶盘初始化（0/1/2），从case 3开始
        static uint8_t cache_init_pending = 0;
        static uint8_t target_bottle_saved = 1;

        if (!first_boot_init_done)
        {
            switch (bottle_init_state)
            {
            case 0:  // 启动归零
            {
                // 读取KVDB中保存的瓶号
                target_bottle_saved = g_RetainSampleConfig.bottleNumber;
                if (target_bottle_saved < 1 || target_bottle_saved > 24)
                {
                    target_bottle_saved = 1;
                }

                // 启动非阻塞归零（超时120秒）
                printf("[任务3] 启动瓶盘归零（非阻塞）...\r\n");
                bottle_home_to_1_start(100, 120000);
                bottle_init_state = 1;
                break;
            }

            case 1:  // 检查归零状态
            {
                uint8_t result = bottle_home_to_1_check();
                if (result == 1)  // 成功
                {
                    printf("[任务3] 瓶盘归零成功\r\n");
                    bottle_clear_fault();

                    // 检查是否需要移动到目标瓶号
                    if (target_bottle_saved != 1)
                    {
                        printf("[任务3] 启动移动到目标瓶号%d...\r\n", target_bottle_saved);
                        bottle_move_to_start(target_bottle_saved, 100, 60000);
                        bottle_init_state = 2;
                    }
                    else
                    {
                        g_current_bottle_number = 1;
                        g_RetainBottleState.currentBottle = 1;
                        bottle_init_state = 3;  // 跳过移动，直接完成
                    }
                }
                else if (result == 2)  // 超时失败
                {
                    printf("[任务3] 瓶盘归零超时，设置故障标志，继续后续流程\r\n");
                    bottle_set_fault(BOTTLE_FAULT_INIT_TIMEOUT);
                    bottle_init_state = 3;  // 跳过，继续后续流程
                }
                // result == 0 表示进行中，继续等待
                break;
            }

            case 2:  // 检查移动状态
            {
                uint8_t result = bottle_move_to_check();
                if (result == 1)  // 成功
                {
                    printf("[任务3] 移动到目标瓶号%d成功\r\n", target_bottle_saved);
                    g_current_bottle_number = target_bottle_saved;
                    g_RetainBottleState.currentBottle = target_bottle_saved;
                    bottle_init_state = 3;
                }
                else if (result == 2)  // 超时失败
                {
                    printf("[任务3] 移动到目标瓶号失败，设置故障标志\r\n");
                    bottle_set_fault(BOTTLE_FAULT_MOVE_TIMEOUT);
                    g_current_bottle_number = 1;
                    g_RetainBottleState.currentBottle = 1;
                    g_RetainSampleConfig.bottleNumber = 1;
                    cfg_save_retain(&g_RetainSampleConfig);
                    bottle_init_state = 3;
                }
                // result == 0 表示进行中，继续等待
                break;
            }

            case 3:  // 瓶盘初始化完成，继续后续流程
            {
                // ★ 第2步：AutoRunMode=1时执行排空+启动调度器
                if (g_SystemSettingConfig.AutoRunMode == 1)
                {
                    // ★ 等待TSDB就绪（缓存管理器依赖TSDB），添加超时保护
                    EventBits_t bits = xEventGroupWaitBits(event_handle, TSDB_READY_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
                    uint8_t tsdb_ready = (bits & TSDB_READY_BIT) ? 1 : 0;
                    if (!tsdb_ready && tsdb_is_ready())
                    {
                        tsdb_ready = 1;
                        xEventGroupSetBits(event_handle, TSDB_READY_BIT);
                    }
                    if (!tsdb_ready)
                    {
                        printf("[任务3] 警告：TSDB未就绪，缓存初始化延后\r\n");
                        cache_init_pending = 1;
                    }

                    // ★★★ 开机排水前初始化UART4（串口屏）★★★
                    // 避免屏幕开机发送数据导致系统卡死，延迟到此处初始化确保FreeRTOS任务完全就绪
                    extern void wk_uart4_init(void);
                    wk_uart4_init();
                    printf("[任务3] UART4初始化完成（排水前），避免屏幕干扰启动\r\n");

                    // ★ 首先初始化sample_id生成器
                    sample_id_generator_init();

                    // ★ 先启动排空，然后在排空期间初始化缓存管理器
                    uint32_t drain_dur = g_SampleConfig.BucketDrainTime ? g_SampleConfig.BucketDrainTime : 30u;
                    uint32_t t0 = g_tmr2_seconds;
                    printf("[任务3] 开机后首次排水开始，目标时长=%u秒\r\n", drain_dur);

                    // 同时启动A桶和B桶排空泵
                    DrainARun;
                    DrainBRun;
                    g_State.DrainA = 1;
                    g_State.DrainB = 1;
                    g_State.ABucketState = 45; // 排空中
                    g_State.BBucketState = 45; // 排空中

                    // ★ 标记AB桶排水已执行（用于超时检测）
                    g_drain_ab_executed = 1;
                    printf("[任务3] AB桶排水已启动\r\n");

                    if (tsdb_ready)
                    {
                        // ★ 在排空期间初始化缓存管理器（利用排空间隙）
                        if (cache_manager_init())
                        {
                            cache_init_pending = 0;
                        }
                    }

                    // 等待排空完成
                    while ((g_tmr2_seconds - t0) < drain_dur)
                    {
                        vTaskDelay(pdMS_TO_TICKS(200));
                        xEventGroupSetBits(event_handle, TASK3_EVENT_BIT); // 持续喂狗
                    }

                    // 同时停止A桶和B桶排空泵
                    DrainAStop;
                    DrainBStop;
                    g_State.DrainA = 0;
                    g_State.DrainB = 0;
                    g_State.SaveWarterA = 0;
                    g_State.SaveWarterB = 0;
                    g_State.ABucketState = 0; // 空闲
                    g_State.BBucketState = 0; // 空闲
                    g_State.DrainAComplete = 1;
                    g_State.DrainBComplete = 1;

                    uint32_t drain_elapsed = g_tmr2_seconds - t0;
                    printf("[任务3] 开机后首次排水结束，实际用时=%u秒\r\n", drain_elapsed);

                    // 标记开机排水完成，允许后续跳转主页
                    xEventGroupSetBits(event_handle, DRAIN_INIT_DONE_BIT);

                    // ★ 检查调度器是否已经运行（避免重复初始化）
                    uint8_t scheduler_already_running = 0;

                switch (g_SampleConfig.SamplingMode)
                {
                case 0: // 时间等比模式
                    scheduler_already_running = tp_scheduler_is_running();
                    break;
                case 1: // 定时采样模式
                    scheduler_already_running = fixed_time_scheduler_is_running();
                    break;
                case 3: // 流量触发模式
                    // 流量触发调度器状态检查（如需要可添加）
                    break;
                case 4: // 开关量触发模式
                    // 开关量触发调度器状态检查（如需要可添加）
                    break;
                }

                if (!scheduler_already_running)
                {
                    // ★ 根据采样模式启动对应的调度器（函数已在sampling.h中声明）
                    switch (g_SampleConfig.SamplingMode)
                    {
                    case 0: // 时间等比模式
                        tp_scheduler_init();
                        tp_scheduler_start();
                        break;

                    case 1: // 定时采样模式
                        fixed_time_scheduler_init();
                        fixed_time_scheduler_start();
                        break;

                    case 2: // 通讯触发模式
                        break;

                    case 3: // 流量触发模式
                        ft_scheduler_init();
                        // ★ 不直接启动调度器，等待流量开始信号触发
                        // ft_scheduler_start() 会在 flow_trigger_notify_start() 中被调用
                        break;

                    case 4: // 开关量触发模式
                        st_scheduler_init(0); // 0=正常初始化（非复位后）
                        st_scheduler_start();
                        break;

                    default:
                        break;
                    }
                }

                // 设置为运行状态
                g_State.State = 1;

                // 检查屏幕是否已就绪，就绪后再刷新屏幕
                if (xEventGroupGetBits(event_handle) & SCREEN_READY_BIT)
                {
                    Screen_init(); // 屏幕已就绪，刷新显示
                }

                first_boot_init_done = 1;
            }
            else
            {
                // 未启用自动运行，无需排水也标记完成

                // ★★★ 即使不自动运行也需要初始化UART4（串口屏）★★★
                // 确保屏幕通讯正常工作
                extern void wk_uart4_init(void);
                wk_uart4_init();
                printf("[任务3] UART4初始化完成（无自动运行模式）\r\n");

                g_drain_ab_executed = 1;  // ★ 避免超时检测误重启
                xEventGroupSetBits(event_handle, DRAIN_INIT_DONE_BIT);
                first_boot_init_done = 1;
            }
            bottle_init_state = 4;  // 标记完成
            break;
            }  // end case 3

            default:
                break;
            }  // end switch (bottle_init_state)
        }  // end if (!first_boot_init_done)

        if (cache_init_pending && tsdb_is_ready())
        {
            if (cache_manager_init())
            {
                cache_init_pending = 0;
                xEventGroupSetBits(event_handle, TSDB_READY_BIT);
                printf("[任务3] TSDB已就绪，缓存初始化完成\r\n");
            }
        }

        // 推进采样送样状态机（独立于调度器）
        sampling_step_if_active();
        delivery_step_if_active();
        update_all_timers();

        // 系统复位状态机更新（非阻塞）
        system_reset_update();

        // 调度器核心（根据模式分发）
        scheduler_dispatcher();
        
        // 处理门锁事件（写入FlashDB）
        door_event_process();

        // ★ 周期性留样瓶故障恢复检查（300秒/10次）
        static uint32_t last_fault_check_time = 0;
        static uint8_t fault_recovery_state = 0;  // 0=空闲, 1=恢复中
        const uint32_t FAULT_CHECK_INTERVAL = 300;  // 每300秒检查一次
        const uint8_t MAX_FAULT_RETRY = 10;         // 最多尝试10次

        if (first_boot_init_done && bottle_is_fault_active() &&
            g_bottle_fault_retry_count < MAX_FAULT_RETRY)
        {
            uint32_t current_time = g_tmr2_seconds;

            if (fault_recovery_state == 0)  // 空闲状态
            {
                if ((current_time - last_fault_check_time) >= FAULT_CHECK_INTERVAL)
                {
                    last_fault_check_time = current_time;
                    g_bottle_fault_retry_count++;
                    printf("[任务3] 留样瓶故障恢复尝试 %d/%d\r\n",
                           g_bottle_fault_retry_count, MAX_FAULT_RETRY);

                    // 启动非阻塞恢复
                    bottle_home_to_1_start(100, 30000);
                    fault_recovery_state = 1;
                }
            }
            else  // 恢复中
            {
                uint8_t result = bottle_home_to_1_check();
                if (result == 1)  // 成功
                {
                    printf("[任务3] 留样瓶故障恢复成功！\r\n");
                    bottle_clear_fault();
                    fault_recovery_state = 0;
                }
                else if (result == 2)  // 失败
                {
                    printf("[任务3] 留样瓶故障恢复失败，将在%d秒后重试\r\n",
                           FAULT_CHECK_INTERVAL);
                    fault_recovery_state = 0;
                }
                // result == 0 表示进行中，继续等待
            }
        }

        // 循环周期100ms
        vTaskDelay(pdMS_TO_TICKS(100));

        /* add user code end task03_func 1 */
    }
}

// 任务4：留样判定与流程协调
/**
 * @brief task4 function.
 * @param  none
 * @retval none
 */
// 任务4：留样判定与流程协调
void task04_func(void *pvParameters) // 留样判定
{
    /* add user code begin task04_func 0 */

    /* add user code end task04_func 0 */

    /* add user code begin task04_func 2 */
    // 留样判定窗口上下文
    typedef struct
    {
        uint8_t bucket_id;         // 等待判定的桶（0=A, 1=B）
        uint32_t delivery_time;    // 送样完成时间戳
        uint32_t window_start_sec; // 判定窗口开始时间（秒）
        uint32_t window_end_sec;   // 判定窗口结束时间（秒）
        uint8_t in_window;         // 是否在窗口内（0/1）
        uint8_t retain_triggered;  // 是否触发留样（0/1）
        uint8_t retain_executed;   // 是否已执行留样（防止重复执行）
        uint8_t window_checked;    // 窗口是否已检查（防止重复处理）
        uint8_t gpio_checked;      // GPIO是否已检测（开关量留样模式：避免重复检测）
        uint8_t signal_blocked;    // 信号是否已屏蔽（防止同周期重复触发）
        uint8_t retain_success;    // 留样是否成功标志（新增：留样成功后置位）
        uint8_t cycle_completed;   // 周期是否完成（新增：整个留样周期完成标志）
    } RetainJudgeContext;

    static RetainJudgeContext g_retain_ctx_a = {0};
    static RetainJudgeContext g_retain_ctx_b = {0};

    static uint32_t g_post_drain_deadline_a = 0;
    static uint32_t g_post_drain_deadline_b = 0;

    // 延迟执行留样结构体（用于处理与采样/送样的冲突）
    typedef struct
    {
        uint8_t pending;        // 是否有待执行的留样
        uint8_t bucket_id;      // 待留样的桶
        uint32_t delivery_time; // 送样时间
        uint32_t trigger_time;  // 触发时间
    } PendingRetain;

    static PendingRetain s_pending_retain = {0};

    // 初始化留样判断模块
    retain_judge_init(120);

    /* add user code end task04_func 2 */

    /* Infinite loop */
    while (1)
    {
        /* add user code begin task04_func 1 */

        // 喂狗
        xEventGroupSetBits(event_handle, TASK4_EVENT_BIT);

        //==========================================================================
        // 第1步：接收task3的送样完成通知 或 流量停止通知
        //==========================================================================

        uint32_t bucket_notify = ulTaskNotifyTake(pdTRUE, 0);
        if (bucket_notify > 0)
        {
            // 检查是否为流量停止通知（0xFF）
            if (bucket_notify == 0xFF)
            {
                // 流量停止通知：检查所有有水的桶，执行留样判定和排空
                printf("[任务4] 已收到流量停止通知\r\n");

                // ★ 优先检查是否留样开关（EnableSample）
                // 如果留样功能已禁用，直接排空所有有水的桶，不进行留样判定
                if (!g_RetainSampleConfig.EnableSample)
                {
                    printf("[任务4] 留样已禁用（EnableSample=0），直接排空所有有水的桶\r\n");

                    // 检查A桶
                    if (g_State.SaveWarterA > 0)
                    {
                        printf("[任务4] A桶有水(%d ml)，正在排空...\r\n", g_State.SaveWarterA);
                        drain_execute(0);
                    }

                    // 检查B桶
                    if (g_State.SaveWarterB > 0)
                    {
                        printf("[任务4] B桶有水(%d ml)，正在排空...\r\n", g_State.SaveWarterB);
                        drain_execute(1);
                    }

                    // 通知调度器流量停止留样处理完成
                    printf("[任务4] 流量停止留样完成，正在通知调度器...\r\n");
                    flow_trigger_retention_complete_callback();
                    continue; // 继续下一次循环
                }

                uint32_t ts = rtc_counter_get();

                // 检查A桶
                if (g_State.SaveWarterA > 0)
                {
                    printf("[任务4] A桶有水(%d ml)，正在判定留样...\r\n", g_State.SaveWarterA);

                    // ★ 直接留样模式快速通道
                    uint8_t triggered = 0;
                    if (g_RetainSampleConfig.Mode == RETAIN_MODE_DIRECT)
                    {
                        printf("[任务4] 模式=直接，立即执行留样\r\n");
                        triggered = 1; // 直接留样模式，每次都留样
                    }
                    else
                    {
                        triggered = retain_judge_commit(0, ts);
                    }

                    if (triggered)
                    {
                        printf("[任务4] A桶留样已触发，正在执行...\r\n");
                        // 使用实际送样完成时间，确保TSDB记录正确关联
                        uint32_t delivery_ts = (g_last_delivery_bucket == 0 && g_last_delivery_time > 0)
                                                   ? g_last_delivery_time
                                                   : ts;
                        uint8_t ret = retention_execute(0, delivery_ts);
                        if (ret == 0)
                        {
                            // 留样失败，手动排空
                            printf("[任务4] A桶留样失败，正在排空...\r\n");
                            drain_execute(0);
                        }
                    }
                    else
                    {
                        printf("[任务4] A桶留样未触发，正在排空...\r\n");
                        drain_execute(0);
                    }
                }

                // 检查B桶
                if (g_State.SaveWarterB > 0)
                {
                    printf("[任务4] B桶有水(%d ml)，正在判定留样...\r\n", g_State.SaveWarterB);

                    // ★ 直接留样模式快速通道
                    uint8_t triggered = 0;
                    if (g_RetainSampleConfig.Mode == RETAIN_MODE_DIRECT)
                    {
                        printf("[任务4] 模式=直接，立即执行留样\r\n");
                        triggered = 1; // 直接留样模式，每次都留样
                    }
                    else
                    {
                        triggered = retain_judge_commit(1, ts);
                    }

                    if (triggered)
                    {
                        printf("[任务4] B桶留样已触发，正在执行...\r\n");
                        // 使用实际送样完成时间，确保TSDB记录正确关联
                        uint32_t delivery_ts = (g_last_delivery_bucket == 1 && g_last_delivery_time > 0)
                                                   ? g_last_delivery_time
                                                   : ts;
                        uint8_t ret = retention_execute(1, delivery_ts);
                        if (ret == 0)
                        {
                            // 留样失败，手动排空
                            printf("[任务4] B桶留样失败，正在排空...\r\n");
                            drain_execute(1);
                        }
                    }
                    else
                    {
                        printf("[任务4] B桶留样未触发，正在排空...\r\n");
                        drain_execute(1);
                    }
                }

                // 通知调度器流量停止留样处理完成
                printf("[任务4] 流量停止留样完成，正在通知调度器...\r\n");
                flow_trigger_retention_complete_callback();
            }
            else
            {
                // 送样完成通知（bucket_id + 1）
                uint8_t bucket_id = bucket_notify - 1; // 还原桶号

                // ★ 优先检查是否留样开关（EnableSample）
                // 如果留样功能已禁用，立即排空桶，不设置窗口
                if (!g_RetainSampleConfig.EnableSample)
                {
                    printf("[任务4] 送样完成: 桶_%c, 留样已禁用（EnableSample=0），立即排空\r\n",
                           bucket_id ? 'B' : 'A');
                    drain_execute(bucket_id);
                    // 不设置窗口上下文，直接跳过后续窗口判定
                    continue; // 继续下一次循环
                }

                // ★ 检查是否为直接留样模式（Mode=1）
                if (g_RetainSampleConfig.Mode == RETAIN_MODE_DIRECT)
                {
                    printf("[任务4] 送样完成: 桶_%c, 模式=直接, 立即执行留样\r\n", bucket_id ? 'B' : 'A');

                    // 使用实际送样完成时间，确保TSDB记录正确关联
                    uint32_t delivery_ts = (g_last_delivery_bucket == bucket_id && g_last_delivery_time > 0)
                                               ? g_last_delivery_time
                                               : rtc_counter_get();
                    uint8_t ret = retention_execute(bucket_id, delivery_ts);
                    if (ret == 1)
                    {
                        // 留样成功（已在retention_execute中排空桶）
                        printf("[任务4] 直接留样完成并排空\r\n");
                    }
                    else
                    {
                        // 留样失败（未排空桶），手动排空
                        printf("[任务4] 直接留样失败，正在排空桶\r\n");
                        drain_execute(bucket_id);
                    }

                    // 不设置窗口上下文，直接跳过后续窗口判定
                }
                else
                {
                    // 检查是否为通讯触发留样模式（Mode=3）
                    if (g_RetainSampleConfig.Mode == 3) { // 通讯触发留样模式
                        printf("[任务4] 通讯触发留样模式：设置留样窗口\r\n");

                        // 初始化窗口上下文
                        CommRetainWindowContext *ctx = (bucket_id == 0) ? &g_comm_retain_ctx_a : &g_comm_retain_ctx_b;

                        ctx->bucket_id = bucket_id;
                        ctx->delivery_time = (g_last_delivery_bucket == bucket_id && g_last_delivery_time > 0)
                                               ? g_last_delivery_time
                                               : rtc_counter_get();

                        // 窗口时间：送样后20分钟开始，到分析时间结束（与其他模式一致）
                        ctx->window_start_sec = ctx->delivery_time + 20 * 60;  // 20分钟后
                        ctx->window_end_sec = ctx->delivery_time + g_SampleConfig.AnalysisTime * 60;  // 分析时间前

                        ctx->in_window = 0;
                        ctx->comm_trigger_received = 0;  // 重置触发标志
                        ctx->retain_executed = 0;
                        ctx->window_checked = 0;
                        ctx->cycle_completed = 0;

                        printf("[任务4] 通讯触发留样窗口已设置: 桶_%c, 窗口=%lu-%lu秒\r\n",
                               bucket_id ? 'B' : 'A', ctx->window_start_sec, ctx->window_end_sec);
                    } else {
                        // 其他模式：设置窗口上下文
                        RetainJudgeContext *ctx = (bucket_id == 0) ? &g_retain_ctx_a : &g_retain_ctx_b;

                        // 计算判定窗口
                        ctx->bucket_id = bucket_id;
                        // 使用实际送样完成时间，确保TSDB记录正确关联
                        ctx->delivery_time = (g_last_delivery_bucket == bucket_id && g_last_delivery_time > 0)
                                                 ? g_last_delivery_time
                                                 : rtc_counter_get();
                        uint16_t analysis_time = g_SampleConfig.AnalysisTime; // 分钟
                        ctx->window_end_sec = ctx->delivery_time + analysis_time * 60;
                        ctx->window_start_sec = ctx->delivery_time + 20 * 60; // 20分钟后开始窗口
                        ctx->in_window = 0;
                        ctx->retain_triggered = 0;
                        ctx->retain_executed = 0; // 重置执行标志
                        ctx->window_checked = 0;
                        ctx->gpio_checked = 0;    // 重置GPIO检测标志
                        ctx->signal_blocked = 0;  // 重置信号屏蔽标志
                        ctx->retain_success = 0;  // 重置留样成功标志
                        ctx->cycle_completed = 0; // 重置周期完成标志（允许新的留样触发）

                        if (bucket_id == 0)
                        {
                            g_post_drain_deadline_a = ctx->window_end_sec + 300;
                        }
                        else
                        {
                            g_post_drain_deadline_b = ctx->window_end_sec + 300;
                        }

                        printf("[任务4] 送样完成: 桶_%c, 送样时间=%lu, 判定窗口=%lu-%lu(20-55分钟)\r\n", bucket_id ? 'B' : 'A', ctx->delivery_time, ctx->window_start_sec, ctx->window_end_sec);
                    }
                }
            }
        }

        //==========================================================================
        // 第2步：检查A桶判定窗口
        //==========================================================================

        if (!g_retain_ctx_a.window_checked)
        {
            uint32_t now_sec = rtc_counter_get();

            // 检查是否进入窗口
            if (now_sec >= g_retain_ctx_a.window_start_sec &&
                now_sec < g_retain_ctx_a.window_end_sec)
            {

                if (!g_retain_ctx_a.in_window)
                {
                    g_retain_ctx_a.in_window = 1;
                    // 清除历史超标状态，让当前仍超标的通道能触发上升沿
                    retain_judge_reset_state();
                    printf("[任务4] 进入留样判定窗口: 桶_A\r\n");

                    // 调试：打印留样模式
                    printf("[调试] 留样模式: %d (0=超标,1=直接,3=通讯,6=开关量)\r\n", g_RetainSampleConfig.Mode);

                    // 调试：打印超标ADC设置项和测量值
                    {
                        int ch;
                        printf("[调试] 超标ADC通道设置:\r\n");
                        for (ch = 0; ch < 6; ch++)
                        {
                            printf("  通道%d: 启用=%d, 因子=%d, 下限=%.2f, 上限=%.2f, 测量值=%.2f\r\n",
                                   ch,
                                   g_RetainSampleConfig.channelLimits[ch].Enable,
                                   g_RetainSampleConfig.channelLimits[ch].FactorType,
                                   g_RetainSampleConfig.channelLimits[ch].LowerLimit,
                                   g_RetainSampleConfig.channelLimits[ch].UpperLimit,
                                   g_RetainSampleConfig.channelData[ch]);
                        }
                    }

                    // ★ 开关量留样模式：首次进入窗口时检测GPIO（低电平触发）
                    if (g_RetainSampleConfig.Mode == RETAIN_MODE_SWITCH && !g_retain_ctx_a.gpio_checked)
                    {
                        uint8_t gpio_level = read_trigger_retention_signal();
                        if (gpio_level == 0)
                        { // 检测到低电平
                            printf("[任务4] 检测到留样触发信号：低电平（PE2）\r\n");
                            analysis_report_switch(0, now_sec); // 通知开关量触发
                        }
                        g_retain_ctx_a.gpio_checked = 1; // 标记已检测，避免重复检测
                    }
                }

                // 检查信号屏蔽
                if (g_retain_ctx_a.signal_blocked)
                {
                    // 信号已屏蔽，跳过判定
                    continue;
                }

                // 检查是否已经完成周期（防止重复触发）
                if (g_retain_ctx_a.cycle_completed)
                {
                    printf("[任务4] A桶留样周期已完成，跳过检测\r\n");
                }
                else
                {
                    // 在窗口内每秒调用判定函数
                    uint8_t result = retain_judge_commit(g_retain_ctx_a.bucket_id, now_sec);
                    if (result && !g_retain_ctx_a.retain_executed && !g_retain_ctx_a.retain_triggered)
                    {
                        g_retain_ctx_a.retain_triggered = 1;
                        g_retain_ctx_a.retain_executed = 1;
                        g_retain_ctx_a.signal_blocked = 1; // 立即屏蔽后续信号
                        printf("[任务4] 留样已触发: 桶_A\r\n");

                        // 检查与采样/送样的冲突
                        uint8_t sampling_status = sampling_get_status();
                        uint8_t delivery_status = delivery_get_status();

                        if (sampling_status == 1 || delivery_status == 1)
                        {
                            // 有冲突，设置延迟执行
                            printf("[任务4] 检测到冲突（采样=%d，送样=%d），留样延迟执行\r\n",
                                   sampling_status, delivery_status);

                            s_pending_retain.pending = 1;
                            s_pending_retain.bucket_id = g_retain_ctx_a.bucket_id;
                            s_pending_retain.delivery_time = g_retain_ctx_a.delivery_time;
                            s_pending_retain.trigger_time = now_sec;
                        }
                        else
                        {
                            // 无冲突，立即执行留样
                            printf("[任务4] 立即执行留样\r\n");
                            uint8_t ret = retention_execute(g_retain_ctx_a.bucket_id, g_retain_ctx_a.delivery_time);

                            if (ret == 1)
                            {
                                // 留样成功
                                g_retain_ctx_a.retain_success = 1;  // 设置成功标志
                                g_retain_ctx_a.cycle_completed = 1; // 标记周期完成
                                printf("[任务4] 留样执行成功，周期完成\r\n");
                            }
                            else
                            {
                                // 留样失败，手动排空
                                g_retain_ctx_a.retain_success = 0;  // 设置失败标志
                                g_retain_ctx_a.cycle_completed = 1; // 仍然标记周期完成
                                printf("[任务4] 留样失败，正在排空桶\r\n");
                                drain_execute(g_retain_ctx_a.bucket_id);
                            }
                        }
                    }
                }
            }

            // 检查窗口是否结束
            if (g_retain_ctx_a.in_window && now_sec >= g_retain_ctx_a.window_end_sec)
            {
                g_retain_ctx_a.in_window = 0;
                g_retain_ctx_a.window_checked = 1;
                g_retain_ctx_a.cycle_completed = 1; // 标记周期完成

                printf("[任务4] 窗口已过期: 桶_A, 触发=%d, 已执行=%d, 成功=%d\r\n",
                       g_retain_ctx_a.retain_triggered, g_retain_ctx_a.retain_executed, g_retain_ctx_a.retain_success);

                // 只有在未触发留样时才需要排空（已触发的在触发时已执行）
                if (!g_retain_ctx_a.retain_triggered)
                {
                    // 无触发，不留样，直接排空
                    printf("[任务4] 无留样触发，正在排空桶\r\n");
                    drain_execute(g_retain_ctx_a.bucket_id);
                    g_retain_ctx_a.retain_success = 0; // 标记为无触发
                }

                g_post_drain_deadline_a = g_retain_ctx_a.window_end_sec + 300;
                // 注意：不清除整个上下文，保留 cycle_completed 和 retain_success 标志
                // memset(&g_retain_ctx_a, 0, sizeof(g_retain_ctx_a));
            }
        }

        //==========================================================================
        // 第3步：检查B桶判定窗口
        //==========================================================================

        if (!g_retain_ctx_b.window_checked)
        {
            uint32_t now_sec = rtc_counter_get();

            // 检查是否进入窗口
            if (now_sec >= g_retain_ctx_b.window_start_sec &&
                now_sec < g_retain_ctx_b.window_end_sec)
            {

                if (!g_retain_ctx_b.in_window)
                {
                    g_retain_ctx_b.in_window = 1;
                    // 清除历史超标状态，让当前仍超标的通道能触发上升沿
                    retain_judge_reset_state();
                    printf("[任务4] ========== 进入留样判定窗口 ==========\r\n");
                    printf("[任务4] 桶: B\r\n");
                    printf("[任务4] 窗口: %lu ~ %lu 秒\r\n", g_retain_ctx_b.window_start_sec, g_retain_ctx_b.window_end_sec);
                    printf("[任务4] ====================================================\r\n");

                    // 调试：打印留样模式
                    printf("[调试] 留样模式: %d (0=超标,1=直接,3=通讯,6=开关量)\r\n", g_RetainSampleConfig.Mode);

                    // 调试：打印超标ADC设置项和测量值
                    {
                        int ch;
                        printf("[调试] 超标ADC通道设置:\r\n");
                        for (ch = 0; ch < 6; ch++)
                        {
                            printf("  通道%d: 启用=%d, 因子=%d, 下限=%.2f, 上限=%.2f, 测量值=%.2f\r\n",
                                   ch,
                                   g_RetainSampleConfig.channelLimits[ch].Enable,
                                   g_RetainSampleConfig.channelLimits[ch].FactorType,
                                   g_RetainSampleConfig.channelLimits[ch].LowerLimit,
                                   g_RetainSampleConfig.channelLimits[ch].UpperLimit,
                                   g_RetainSampleConfig.channelData[ch]);
                        }
                    }

                    // ★ 开关量留样模式：首次进入窗口时检测GPIO（低电平触发）
                    if (g_RetainSampleConfig.Mode == RETAIN_MODE_SWITCH && !g_retain_ctx_b.gpio_checked)
                    {
                        uint8_t gpio_level = read_trigger_retention_signal();
                        if (gpio_level == 0)
                        { // 检测到低电平
                            printf("[任务4] 检测到留样触发信号：低电平（PE2）\r\n");
                            analysis_report_switch(0, now_sec); // 通知开关量触发
                        }
                        g_retain_ctx_b.gpio_checked = 1; // 标记已检测，避免重复检测
                    }
                }

                // 检查是否已经完成周期（防止重复触发）
                if (g_retain_ctx_b.cycle_completed)
                {
                    // 周期已完成，跳过检测
                }
                else
                {
                    // 检查信号屏蔽
                    if (g_retain_ctx_b.signal_blocked)
                    {
                        // 信号已屏蔽，跳过判定
                        continue;
                    }

                    // 在窗口内每秒调用判定函数
                    uint8_t result = retain_judge_commit(g_retain_ctx_b.bucket_id, now_sec);
                    if (result && !g_retain_ctx_b.retain_executed && !g_retain_ctx_b.retain_triggered)
                    {
                        g_retain_ctx_b.retain_triggered = 1;
                        g_retain_ctx_b.retain_executed = 1;
                        g_retain_ctx_b.signal_blocked = 1; // 立即屏蔽后续信号
                        printf("[任务4] 留样已触发: 桶_B\r\n");

                        // 检查与采样/送样的冲突
                        uint8_t sampling_status = sampling_get_status();
                        uint8_t delivery_status = delivery_get_status();

                        if (sampling_status == 1 || delivery_status == 1)
                        {
                            // 有冲突，设置延迟执行
                            printf("[任务4] 检测到冲突（采样=%d，送样=%d），留样延迟执行\r\n",
                                   sampling_status, delivery_status);

                            s_pending_retain.pending = 1;
                            s_pending_retain.bucket_id = g_retain_ctx_b.bucket_id;
                            s_pending_retain.delivery_time = g_retain_ctx_b.delivery_time;
                            s_pending_retain.trigger_time = now_sec;
                        }
                        else
                        {
                            // 无冲突，立即执行留样
                            printf("[任务4] 立即执行留样\r\n");
                            uint8_t ret = retention_execute(g_retain_ctx_b.bucket_id, g_retain_ctx_b.delivery_time);

                            if (ret == 1)
                            {
                                // 留样成功
                                g_retain_ctx_b.retain_success = 1;  // 设置成功标志
                                g_retain_ctx_b.cycle_completed = 1; // 标记周期完成
                                printf("[任务4] 留样执行成功，周期完成\r\n");
                            }
                            else
                            {
                                // 留样失败，手动排空
                                g_retain_ctx_b.retain_success = 0;  // 设置失败标志
                                g_retain_ctx_b.cycle_completed = 1; // 仍然标记周期完成
                                printf("[任务4] 留样失败，正在排空桶\r\n");
                                drain_execute(g_retain_ctx_b.bucket_id);
                            }
                        }
                    }
                }
            }

            // 检查窗口是否结束
            if (g_retain_ctx_b.in_window && now_sec >= g_retain_ctx_b.window_end_sec)
            {
                g_retain_ctx_b.in_window = 0;
                g_retain_ctx_b.window_checked = 1;
                g_retain_ctx_b.cycle_completed = 1; // 标记周期完成

                printf("[任务4] 窗口已过期: 桶_B, 触发=%d, 已执行=%d, 成功=%d\r\n",
                       g_retain_ctx_b.retain_triggered, g_retain_ctx_b.retain_executed, g_retain_ctx_b.retain_success);

                // 只有在未触发留样时才需要排空（已触发的在触发时已执行）
                if (!g_retain_ctx_b.retain_triggered)
                {
                    // 无触发，不留样，直接排空
                    printf("[任务4] 无留样触发，正在排空桶\r\n");
                    drain_execute(g_retain_ctx_b.bucket_id);
                    g_retain_ctx_b.retain_success = 0; // 标记为无触发
                }

                g_post_drain_deadline_b = g_retain_ctx_b.window_end_sec + 300;
                // 注意：不清除整个上下文，保留 cycle_completed 和 retain_success 标志
                // memset(&g_retain_ctx_b, 0, sizeof(g_retain_ctx_b));
            }
        }

        //==========================================================================
        // 第3.5步：检查通讯触发留样窗口（A桶）
        //==========================================================================
        if (g_RetainSampleConfig.Mode == 3 && !g_comm_retain_ctx_a.window_checked)
        {
            uint32_t now_sec = rtc_counter_get();
            CommRetainWindowContext *ctx = &g_comm_retain_ctx_a;

            // 检查是否进入窗口
            if (now_sec >= ctx->window_start_sec && now_sec < ctx->window_end_sec)
            {
                if (!ctx->in_window)
                {
                    ctx->in_window = 1;
                    printf("[任务4] 进入通讯触发留样窗口: 桶_A\r\n");
                }

                // 检查是否收到触发信号且未执行留样
                if (ctx->comm_trigger_received && !ctx->retain_executed)
                {
                    printf("[任务4] 通讯触发留样执行: 桶_A\r\n");

                    // 检查与采样/送样的冲突
                    uint8_t sampling_status = sampling_get_status();
                    uint8_t delivery_status = delivery_get_status();

                    if (sampling_status != 1 && delivery_status != 1)
                    {
                        // 无冲突，立即执行留样
                        uint8_t result = retention_execute(ctx->bucket_id, ctx->delivery_time);

                        if (result == 1)
                        {
                            ctx->retain_executed = 1;
                            ctx->cycle_completed = 1;
                            printf("[任务4] 通讯触发留样成功: 桶_A\r\n");

                            // 更新留样执行成功的日志
                            RetainLogRecord log = {0};
                            log.retain_mode = 3;              // 通讯触发模式
                            log.retain_reason = 9;            // 通讯触发
                            log.start_time = ctx->delivery_time;
            log.end_time = rtc_counter_get();
                            log.delivery_time = ctx->delivery_time;
                            log.retain_volume = g_RetainSampleConfig.SampleVolume;
                            log.bottle_number = g_RetainSampleConfig.bottleNumber + 1;
                            log.result = 1;                   // 执行成功
                            log.error_code = 0;

                            log_retain_record(&log);
                        }
                        else
                        {
                            printf("[任务4] 通讯触发留样失败，将排空\r\n");
                            drain_execute(ctx->bucket_id);
                            ctx->cycle_completed = 1;
                        }
                    }
                    else
                    {
                        printf("[任务4] 系统忙碌，通讯触发留样延迟\r\n");
                        // 保持触发标志，等待下次检查
                    }
                }
            }

            // 检查窗口是否结束
            if (ctx->in_window && now_sec >= ctx->window_end_sec)
            {
                ctx->in_window = 0;
                ctx->window_checked = 1;
                ctx->cycle_completed = 1;

                printf("[任务4] 通讯触发窗口结束: 桶_A, 触发=%d, 执行=%d\r\n",
                       ctx->comm_trigger_received, ctx->retain_executed);

                // 如果没有触发留样，需要排空
                if (!ctx->comm_trigger_received || !ctx->retain_executed)
                {
                    if (g_State.SaveWarterA > 0)
                    {
                        printf("[任务4] 无通讯触发留样，排空A桶\r\n");
                        drain_execute(0);
                    }
                }
            }
        }

        //==========================================================================
        // 第3.6步：检查通讯触发留样窗口（B桶）
        //==========================================================================
        if (g_RetainSampleConfig.Mode == 3 && !g_comm_retain_ctx_b.window_checked)
        {
            uint32_t now_sec = rtc_counter_get();
            CommRetainWindowContext *ctx = &g_comm_retain_ctx_b;

            // 检查是否进入窗口
            if (now_sec >= ctx->window_start_sec && now_sec < ctx->window_end_sec)
            {
                if (!ctx->in_window)
                {
                    ctx->in_window = 1;
                    printf("[任务4] 进入通讯触发留样窗口: 桶_B\r\n");
                }

                // 检查是否收到触发信号且未执行留样
                if (ctx->comm_trigger_received && !ctx->retain_executed)
                {
                    printf("[任务4] 通讯触发留样执行: 桶_B\r\n");

                    // 检查与采样/送样的冲突
                    uint8_t sampling_status = sampling_get_status();
                    uint8_t delivery_status = delivery_get_status();

                    if (sampling_status != 1 && delivery_status != 1)
                    {
                        // 无冲突，立即执行留样
                        uint8_t result = retention_execute(ctx->bucket_id, ctx->delivery_time);

                        if (result == 1)
                        {
                            ctx->retain_executed = 1;
                            ctx->cycle_completed = 1;
                            printf("[任务4] 通讯触发留样成功: 桶_B\r\n");

                            // 更新留样执行成功的日志
                            RetainLogRecord log = {0};
                            log.retain_mode = 3;              // 通讯触发模式
                            log.retain_reason = 9;            // 通讯触发
                            log.start_time = ctx->delivery_time;
            log.end_time = rtc_counter_get();
                            log.delivery_time = ctx->delivery_time;
                            log.retain_volume = g_RetainSampleConfig.SampleVolume;
                            log.bottle_number = g_RetainSampleConfig.bottleNumber + 1;
                            log.result = 1;                   // 执行成功
                            log.error_code = 0;

                            log_retain_record(&log);
                        }
                        else
                        {
                            printf("[任务4] 通讯触发留样失败，将排空\r\n");
                            drain_execute(ctx->bucket_id);
                            ctx->cycle_completed = 1;
                        }
                    }
                    else
                    {
                        printf("[任务4] 系统忙碌，通讯触发留样延迟\r\n");
                        // 保持触发标志，等待下次检查
                    }
                }
            }

            // 检查窗口是否结束
            if (ctx->in_window && now_sec >= ctx->window_end_sec)
            {
                ctx->in_window = 0;
                ctx->window_checked = 1;
                ctx->cycle_completed = 1;

                printf("[任务4] 通讯触发窗口结束: 桶_B, 触发=%d, 执行=%d\r\n",
                       ctx->comm_trigger_received, ctx->retain_executed);

                // 如果没有触发留样，需要排空
                if (!ctx->comm_trigger_received || !ctx->retain_executed)
                {
                    if (g_State.SaveWarterB > 0)
                    {
                        printf("[任务4] 无通讯触发留样，排空B桶\r\n");
                        drain_execute(1);
                    }
                }
            }
        }

        //		rtc_time_get();
        //		printf("%d年%d月%d日%d时%d分%d秒\r\n",calendar.year,calendar.month,calendar.date,calendar.hour,calendar.min,calendar.sec);
        {
            uint32_t now2 = rtc_counter_get();
            if (g_post_drain_deadline_a && now2 >= g_post_drain_deadline_a && g_State.DrainAComplete == 0)
            {
                printf("[任务4] 排空超时兜底：A桶未完成，强制再次排空\r\n");
                drain_execute(0);
                g_post_drain_deadline_a = 0;
            }
            if (g_post_drain_deadline_b && now2 >= g_post_drain_deadline_b && g_State.DrainBComplete == 0)
            {
                printf("[任务4] 排空超时兜底：B桶未完成，强制再次排空\r\n");
                drain_execute(1);
                g_post_drain_deadline_b = 0;
            }
        }

        //==========================================================================
        // 第4步：检查延迟执行的留样（处理与采样/送样的冲突）
        //==========================================================================

        if (s_pending_retain.pending)
        {
            // 检查采样和送样状态
            uint8_t sampling_status = sampling_get_status();
            uint8_t delivery_status = delivery_get_status();

            if (sampling_status != 1 && delivery_status != 1)
            {
                // 冲突已解决，执行延迟的留样
                printf("[任务4] 冲突已解决，执行延迟的留样: 桶_%d\r\n", s_pending_retain.bucket_id);

                uint8_t ret = retention_execute(s_pending_retain.bucket_id, s_pending_retain.delivery_time);

                if (ret == 1)
                {
                    printf("[任务4] 延迟留样执行成功\r\n");
                    // 设置对应的留样上下文标志
                    if (s_pending_retain.bucket_id == 0)
                    {
                        g_retain_ctx_a.retain_success = 1;
                        g_retain_ctx_a.cycle_completed = 1;
                    }
                    else
                    {
                        g_retain_ctx_b.retain_success = 1;
                        g_retain_ctx_b.cycle_completed = 1;
                    }
                }
                else
                {
                    printf("[任务4] 延迟留样失败，正在排空桶\r\n");
                    drain_execute(s_pending_retain.bucket_id);
                    // 设置对应的留样上下文标志
                    if (s_pending_retain.bucket_id == 0)
                    {
                        g_retain_ctx_a.retain_success = 0;
                        g_retain_ctx_a.cycle_completed = 1;
                    }
                    else
                    {
                        g_retain_ctx_b.retain_success = 0;
                        g_retain_ctx_b.cycle_completed = 1;
                    }
                }

                // 清除待执行标志
                memset(&s_pending_retain, 0, sizeof(s_pending_retain));
            }
            else
            {
                // 仍在冲突中，打印等待信息
                static uint32_t last_wait_print = 0;
                uint32_t current_time = rtc_counter_get();
                if (current_time - last_wait_print >= 10)
                { // 每10秒打印一次
                    printf("[任务4] 等待冲突解决执行延迟留样: 采样=%d, 送样=%d\r\n",
                           sampling_status, delivery_status);
                    last_wait_print = current_time;
                }
            }
        }

        vTaskDelay(1000);

        /* add user code end task04_func 1 */
    }
}

// 任务5：串口屏显示与指令分发
/**
 * @brief task5 function.
 * @param  none
 * @retval none
 */
// 任务5：串口屏显示与指令分发
void task05_func(void *pvParameters)
{
    /* add user code begin task05_func 0 */

    // ★ 等待串口屏完全启动（迪文屏需要较长时间启动）
    vTaskDelay(pdMS_TO_TICKS(10000));

    // 初始化串口屏
    Screen_init();

    // ★ 新增：设置屏幕就绪标志
    xEventGroupSetBits(event_handle, SCREEN_READY_BIT);

    /* add user code end task05_func 0 */

    /* add user code begin task05_func 2 */

    // 主页刷新控制变量
    TickType_t last_home_refresh_tick = 0;
    const TickType_t home_refresh_interval = pdMS_TO_TICKS(1000); // 1秒刷新间隔
    uint8_t screen_begin_done = 0;  // 排水完成后再跳转首页
    
    // 独立时间更新控制变量（不受页面状态影响）
    TickType_t last_time_update_tick = 0;
    const TickType_t time_update_interval = pdMS_TO_TICKS(1000); // 1秒更新间隔

    // 通道设置页ADC数据发送控制变量（5秒周期）
    TickType_t last_channel_send_tick = 0;
    const TickType_t channel_send_interval = pdMS_TO_TICKS(5000); // 5秒发送间隔

    /* add user code end task05_func 2 */

    /* Infinite loop */
    while (1)
    {
        /* add user code begin task05_func 1 */

        // 喂狗
        xEventGroupSetBits(event_handle, TASK5_EVENT_BIT);

        // 排水完成后执行一次首页展示，避免在排水前发送跳转命令
        if (!screen_begin_done && (xEventGroupGetBits(event_handle) & DRAIN_INIT_DONE_BIT))
        {
            Screen_begin();
            Screen_init(); // 排水完成后再做一次初始化，确保采样设置等刷新
            screen_begin_done = 1;
        }

        // 通道设置页ADC数据发送（5秒周期，仅在通道设置页时发送）
        TickType_t now = xTaskGetTickCount();
        if (screen_is_on_channel_settings_page()) {
            if ((now - last_channel_send_tick) >= channel_send_interval) {
                retain_send_current_values_to_screen();
                last_channel_send_tick = now;
            }
        }
        
        // 高级设置页流量数据发送（5秒周期，仅在高级设置页且流量触发模式时发送）
        if (screen_is_on_advanced_settings_page()) {
            if ((now - last_channel_send_tick) >= channel_send_interval) {
                printf("[调试] 高级设置页检测到，调用流量发送函数\r\n");
                retain_send_flow_values_to_screen();
                last_channel_send_tick = now;
            }
        }

        // 独立时间更新（每秒发送，不受页面状态影响）
        if ((now - last_time_update_tick) >= time_update_interval)
        {
            screen_send_time_update();  // 发送时间到地址5220
            last_time_update_tick = now;
        }

        // 主页刷新逻辑（1秒一次）
        if (screen_is_on_home_page())
        {
            if ((now - last_home_refresh_tick) >= home_refresh_interval)
            {
                // 注意：不要清零瓶号，retain_judge.c中的update_bottle_display()会正确更新
                write_begin_page();

                // ★ 发送门状态到串口屏
                if (door_is_locked()) {
                    // 关门：5A A5 05 82 52 0C 00 00
                    uint8_t door_close_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x0C, 0x00, 0x00};
                    screen_send_notify(USART_SCREEN, door_close_buf, 8, 3);
                } else {
                    // 开门：5A A5 05 82 52 0C 00 01
                    uint8_t door_open_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x0C, 0x00, 0x01};
                    screen_send_notify(USART_SCREEN, door_open_buf, 8, 3);
                }

                // ★ 检测留样瓶满：24号瓶且EnableVacuum=0
                if (g_RetainSampleConfig.bottleNumber == 24 && g_RetainSampleConfig.EnableVacuum == 0) {
                    // 留样瓶满：5A A5 05 82 52 43 00 01
                    uint8_t bottle_full_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x43, 0x00, 0x01};
                    screen_send_notify(USART_SCREEN, bottle_full_buf, 8, 3);
                } else {
                    // 留样瓶未满：5A A5 05 82 52 43 00 00
                    uint8_t bottle_not_full_buf[8] = {0x5A, 0xA5, 0x05, 0x82, 0x52, 0x43, 0x00, 0x00};
                    screen_send_notify(USART_SCREEN, bottle_not_full_buf, 8, 3);
                }

                last_home_refresh_tick = now;
            }
        }

        // 串口屏消息分发（每10ms检查）
        screen_message_dispatcher();

        vTaskDelay(pdMS_TO_TICKS(10));

        /* add user code end task05_func 1 */
    }
}

// 任务6：ADC采集与留样判定处理
/**
 * @brief task6 function - ADC采集与双级滤波
 * @param  none
 * @retval none
 */
// 任务6：ADC采集与留样判定处理
void task06_func(void *pvParameters)
{
    /* add user code begin task06_func 0 */

    /* add user code end task06_func 0 */

    /* add user code begin task06_func 2 */
    /* add user code end task06_func 2 */

    /* Infinite loop */
    while (1)
    {
        /* add user code begin task06_func 1 */

        // 喂狗
        xEventGroupSetBits(event_handle, TASK6_EVENT_BIT);
        if (!retain_judge_uart7_should_run())
        {
            retain_judge_process();
        }
        
        // 废水排放浮子开关检测（PE4边沿检测，PB1阀门控制）
        waste_water_drain_process();

        vTaskDelay(pdMS_TO_TICKS(25)); // 25ms周期，等待DMA缓冲区完全更新（200组×9通道≈23ms）

        /* add user code end task06_func 1 */
    }
}

static void apply_comm_protocol_callbacks(MBInstance_t *inst, uint8_t proto)
{
    switch (proto)
    {
    case COMM_PROTOCOL_DAYUE:
        dayue_register_callbacks(inst);
        printf("[任务7] 使用大岳协议处理\r\n");
        break;
    case COMM_PROTOCOL_DAHU:
        dahu_register_callbacks(inst);
        printf("[任务7] 使用大湖协议处理\r\n");
        break;
    case COMM_PROTOCOL_SICHUAN:
        sichuan_register_callbacks(inst);
        printf("[任务7] 使用四川管控协议处理\r\n");
        break;
    default:
        printf("[任务7] 协议选择无效(%d)，回退大岳协议\r\n", proto);
        dayue_register_callbacks(inst);
        break;
    }
}

// 任务7：485协议通讯
void task07_func(void *pvParameters) // 485协议
{
    UartMessage message;
    static uint8_t mb_initialized = 0;
    static uint8_t active_comm_mode = 0xFF;
    static uint8_t active_comm_proto = 0xFF;

    // 任务初始化

    // 初始化FreeModbus实例（根据配置选择485/232，协议由串口屏选择）
    if (!mb_initialized) {
        active_comm_mode = g_CommSettingConfig.Protocol;
        active_comm_proto = g_CommSettingConfig.AutoCalibration;
        if (active_comm_mode == 0) {
            M485
        } else if (active_comm_mode == 1) {
            M232
        }

        if (eMBInit_Inst(&g_mb_dayue, g_CommSettingConfig.DeviceAddr,
                          USART2, MB_MODE_NORMAL)) {
            apply_comm_protocol_callbacks(&g_mb_dayue, active_comm_proto);
            eMBEnable_Inst(&g_mb_dayue);
            mb_initialized = 1;
        }
    }

    /* Infinite loop */
    while (1)
    {
        /* 运行期检测接口切换请求（仅切换485/232引脚） */
        if (active_comm_mode != g_CommSettingConfig.Protocol) {
            active_comm_mode = g_CommSettingConfig.Protocol;
            if (active_comm_mode == 0) {
                M485
            } else if (active_comm_mode == 1) {
                M232
            }
        }

        if (active_comm_proto != g_CommSettingConfig.AutoCalibration) {
            active_comm_proto = g_CommSettingConfig.AutoCalibration;
            apply_comm_protocol_callbacks(&g_mb_dayue, active_comm_proto);
        }

        // 1. 从消息队列接收数据（1秒超时）
        if (xQueueReceive(queue_analyser_handle, &message, pdMS_TO_TICKS(1000)) == pdPASS)
        {
            g_State.ExternalConnection = 2;

            // 2. 设置接收帧并处理
            eMBSetRcvFrame_Inst(&g_mb_dayue, message.data, message.len);
            eMBPoll_Inst(&g_mb_dayue);

            // 注意：eMBPoll_Inst内部会自动发送响应
        }

        // 设置事件标志（用于系统监控）- 无论是否收到消息都喂狗
        xEventGroupSetBits(event_handle, TASK7_EVENT_BIT);
    }
}

// 任务8：心跳更新与刷卡开门
void task08_func(void *pvParameters)
{
    /* add user code begin task08_func 0 */

    /* add user code end task08_func 0 */

    /* add user code begin task08_func 2 */
    uint32_t CardNum;

    // 先等待系统完全启动
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 注册韦根任务句柄，用于接收卡片通知
    wiegand_register_task(xTaskGetCurrentTaskHandle());
    while (1)
    {
        /* add user code begin task08_func 1 */

        // 喂狗 - 放在循环开始处
        xEventGroupSetBits(event_handle, TASK8_EVENT_BIT);

        // 检查任务通知（区分心跳通知和刷卡通知）
        uint32_t notify_value = 0;
        xTaskNotifyWait(0, 0xFFFFFFFF, &notify_value, pdMS_TO_TICKS(10));  // 减少超时到10ms，提高响应速度
        if (notify_value > 0)
        {
            if (notify_value & 0x01)
            {
                // 心跳通知（bit0）

                extern void power_update_heartbeat(void);
                power_update_heartbeat();
            }
            if (notify_value & 0x02)
            {
                // 刷卡通知（bit1）
                // 收到刷卡通知，读取卡号
                CardNum = wiegand_get_card_id();

                if (CardNum != 0)
                {
                    printf("\r\n[DOOR] Card detected: %u\r\n", CardNum);

                    // 检查卡号是否授权
                    bool authorized = false;
                    for (int i = 0; i < 10; i++)
                    { // 假设最多支持10张卡
                        if (g_SystemSettingConfig.CardId[i] == CardNum)
                        {
                            authorized = true;
                            break;
                        }
                    }

                    if (authorized)
                    {
                        printf("[DOOR] Authorized! Opening door...\r\n");

                        // 保存门禁操作记录（参照断电记录的滑动窗口方式）
                        rtc_time_get();
                        uint8_t idx;
                        
                        if (g_DoorAccessRecordCount < DOOR_ACCESS_RECORD_MAX)
                        {
                            // 未满：顺序追加
                            idx = g_DoorAccessRecordCount;
                            g_DoorAccessRecordCount++;
                        }
                        else
                        {
                            // 已满：滑动窗口，移除最旧记录
                            memmove(&g_DoorAccessRecords[0],
                                    &g_DoorAccessRecords[1],
                                    (DOOR_ACCESS_RECORD_MAX - 1) * sizeof(DoorAccessRecord_t));
                            idx = DOOR_ACCESS_RECORD_MAX - 1;
                        }
                        
                        g_DoorAccessRecords[idx].year = calendar.year - 2000;
                        g_DoorAccessRecords[idx].month = calendar.month;
                        g_DoorAccessRecords[idx].day = calendar.date;
                        g_DoorAccessRecords[idx].hour = calendar.hour;
                        g_DoorAccessRecords[idx].minute = calendar.min;
                        g_DoorAccessRecords[idx].second = calendar.sec;
                        g_DoorAccessRecords[idx].cardId = CardNum;
                        
                        printf("[DOOR] Record saved, idx=%d, count=%d\r\n", idx, g_DoorAccessRecordCount);

                        // 记录开门时间
                        uint32_t door_open_time = g_tmr3_milliseconds;

                        DoorRun; // 开门
                        vTaskDelay(pdMS_TO_TICKS(1000));
                        DoorStop; // 关门

                        // 计算本次开锁持续时间
                        uint32_t door_duration = g_tmr3_milliseconds - door_open_time;
                        printf("[DOOR] Door closed, duration: %u ms (%.2f sec)\r\n", door_duration, door_duration / 1000.0f);

                        // 查询门锁统计信息
                        DoorLockStats_t stats;
                        door_get_stats(&stats);
                        printf("[DOOR] Total unlock count: %u\r\n", stats.unlock_count);
                    }
                }
            }
        }

        // 短延时，避免占用过多CPU
        vTaskDelay(pdMS_TO_TICKS(10));

        /* add user code end task08_func 1 */
    }
}

#if USE_UART7_AI
// 任务9：UART7 485采集（超标/流量）
void task09_func(void *pvParameters)
{
    UartMessage message;
    static const uint8_t req_frame[8] = {0x01, 0x03, 0x00, 0x08, 0x00, 0x06, 0x44, 0x0A};
    static const TickType_t poll_interval = pdMS_TO_TICKS(5000);
    static const TickType_t resp_timeout = pdMS_TO_TICKS(1000);
    static uint8_t was_active = 0;
    static uint32_t last_warn_ts = 0;
    TickType_t last_wake = xTaskGetTickCount();

    while (1)
    {
        uint8_t need_uart7 = retain_judge_uart7_should_run();
        if (!need_uart7)
        {
            if (was_active)
            {
                retain_judge_uart7_mark_invalid();
                was_active = 0;
            }
            xEventGroupSetBits(event_handle, TASK9_EVENT_BIT);
            vTaskDelay(pdMS_TO_TICKS(500));
            last_wake = xTaskGetTickCount();
            continue;
        }

        if (!was_active)
        {
            while (xQueueReceive(queue_moduleAI_handle, &message, 0) == pdPASS)
            {
            }
            was_active = 1;
        }

        while (xQueueReceive(queue_moduleAI_handle, &message, 0) == pdPASS)
        {
        }

        vSendData(UART7, req_frame, sizeof(req_frame));

        if (xQueueReceive(queue_moduleAI_handle, &message, resp_timeout) == pdPASS)
        {
            if (message.len == 17 && message.data[0] == 0x01 && message.data[1] == 0x03 && message.data[2] == 0x0C)
            {
                uint16_t regs[6];
                for (int i = 0; i < 6; ++i)
                {
                    regs[i] = ((uint16_t)message.data[3 + i * 2] << 8) | message.data[4 + i * 2];
                }
                retain_judge_uart7_update(regs, rtc_counter_get());
            }
            else
            {
                uint32_t now = rtc_counter_get();
                if (now - last_warn_ts >= 30)
                {
                    printf("[UART7] 警告: 无效帧(len=%u)\r\n", (unsigned)message.len);
                    last_warn_ts = now;
                }
                retain_judge_uart7_mark_invalid();
            }
        }
        else
        {
            uint32_t now = rtc_counter_get();
            if (now - last_warn_ts >= 30)
            {
                printf("[UART7] 警告: 485应答超时\r\n");
                last_warn_ts = now;
            }
            retain_judge_uart7_mark_invalid();
        }

        xEventGroupSetBits(event_handle, TASK9_EVENT_BIT);
        vTaskDelayUntil(&last_wake, poll_interval);
    }
}
#endif /* USE_UART7_AI */

// 任务10：屏幕命令处理与KVDB/TSDB刷写
void task10_func(void *pvParameters)
{
    ScreenCommand cmd;
    TickType_t last_flush_time = xTaskGetTickCount();
    const TickType_t FLUSH_INTERVAL = pdMS_TO_TICKS(2000); /* 每2秒刷写一次缓存 */
    const TickType_t RESET_CONFIRM_TIMEOUT = pdMS_TO_TICKS(20000); /* 复位二次确认超时 */

    /* 瓶盘复位状态机变量 */
    uint8_t bottle_reset_active = 0;      /* 0=空闲, 1=复位中 */
    uint8_t bottle_complete = 0;          /* 瓶盘复位完成标志 */
    uint8_t bottle_result = 0;            /* 瓶盘复位结果: 0=进行中, 1=成功, 2=失败 */

    /* 复位二次确认状态 */
    uint8_t reset_confirm_pending = 0;
    TickType_t reset_pending_start = 0;

    while (1)
    {
        /* 非阻塞检查命令队列 */
        if (xQueueReceive(queue_screen_cmd, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            
            switch (cmd.type)
            {
                case SCMD_BOTTLE_RESET:
                    /* 瓶盘复位 - 仅将留样瓶复位到1号瓶，更新KVDB瓶号，不做其他操作 */
                    if (bottle_reset_active == 0)
                    {
                        printf("[Task10] 启动留样瓶复位到1号瓶\r\n");

                        /* 启动瓶盘复位到1号瓶 */
                        bottle_home_to_1_start(100, 120000);  /* 120秒超时 */
                        bottle_complete = 0;
                        bottle_result = 0;
                        bottle_reset_active = 1;
                    }
                    else
                    {
                        printf("[Task10] 警告：复位正在进行中，忽略重复命令\r\n");
                    }
                    break;
                    
                case SCMD_TSDB_FORMAT:
                    /* TSDB格式化 - 长耗时操作 */
                    printf("[Task10] 开始TSDB格式化...\r\n");
                    tsdb_format_full();
                    printf("[Task10] TSDB格式化完成\r\n");
                    break;
                    
                case SCMD_MANUAL_SAMPLING:
                    /* 手动采样 - 使用全局变量g_SingleSampleTest中已设置的参数 */
                    printf("[Task10] 执行手动采样\r\n");
                    test_sampling_execute();
                    break;
                    
                case SCMD_MANUAL_DELIVERY:
                    /* 手动送样 - 使用全局变量中已设置的参数 */
                    printf("[Task10] 执行手动送样\r\n");
                    test_delivery_execute();
                    break;
                    
                case SCMD_MANUAL_INSTANT_DELIVERY:
                    /* 瞬时送样 */
                    printf("[Task10] 执行瞬时送样\r\n");
                    test_instant_delivery_execute();
                    break;
                    
                case SCMD_MANUAL_RETENTION:
                    /* 手动留样 - 使用全局变量中已设置的参数 */
                    printf("[Task10] 执行手动留样\r\n");
                    test_retention_execute();
                    break;
                    
                case SCMD_MANUAL_INSTANT_RETENTION:
                    /* 瞬时留样 */
                    printf("[Task10] 执行瞬时留样\r\n");
                    test_instant_retention_execute();
                    break;
                    
                case SCMD_SYSTEM_RESET:
                    /* 系统复位 - 直接执行，不需要二次确认 */
                    if (bottle_reset_active == 0)
                    {
                        printf("[Task10] 收到系统复位请求，直接执行复位流程\r\n");
                        /* 刷写缓存，避免数据丢失 */
                        kvdb_cache_flush_all();
                        tsdb_cache_flush_all();
                        vTaskDelay(pdMS_TO_TICKS(100));
                        /* 启动系统复位状态机 */
                        extern void system_reset_start(void);
                        system_reset_start();
                    }
                    else
                    {
                        printf("[Task10] 警告：瓶盘复位进行中，忽略系统复位请求\r\n");
                    }
                    break;

                case SCMD_SYSTEM_RESET_FSM:
                    /* 系统复位状态机 - 调用sampling.c中的复位状态机 */
                    if (bottle_reset_active == 0)
                    {
                        printf("[Task10] 启动系统复位状态机\r\n");
                        extern void system_reset_start(void);
                        system_reset_start();
                        /* 注意：状态机更新在其他任务中执行 */
                        /* 这里只是触发启动，避免屏幕分发器直接调用导致的问题 */
                    }
                    else
                    {
                        printf("[Task10] 警告：瓶盘复位正在进行中，忽略系统复位命令\r\n");
                    }
                    break;

                default:
                    break;
            }
        }

        /* 复位确认超时处理 */
        if (reset_confirm_pending && (TickType_t)(xTaskGetTickCount() - reset_pending_start) >= RESET_CONFIRM_TIMEOUT)
        {
            reset_confirm_pending = 0;
            printf("[Task10] 复位确认超时，已取消复位请求\r\n");
        }

        /* 检查瓶盘复位状态 */
        if (bottle_reset_active != 0)
        {
            /* 检查瓶盘复位状态 */
            if (!bottle_complete)
            {
                uint8_t status = bottle_home_to_1_check();
                if (status == 1)  /* 成功 */
                {
                    bottle_result = 1;
                    bottle_complete = 1;
                    bottle_clear_fault();  /* ★ 清除故障标志 */
                    printf("[Task10] 瓶盘复位成功\r\n");
                }
                else if (status == 2)  /* 失败/超时 - 修复bug：原来错误地写成 status == 0 */
                {
                    bottle_result = 2;
                    bottle_complete = 1;
                    bottle_set_fault(BOTTLE_FAULT_RESET_TIMEOUT);  /* ★ 设置故障标志 */
                    printf("[Task10] 瓶盘复位失败或超时\r\n");
                }
                /* status == 0 表示进行中，继续等待 */
            }

            /* 瓶盘复位完成后，执行KVDB更新 */
            if (bottle_complete)
            {
                if (bottle_result == 1)
                {
                    printf("[Task10] 瓶盘复位成功，更新KVDB瓶号...\r\n");

                    /* 仅更新瓶号相关变量 */
                    g_current_bottle_number = 1;
                    g_RetainBottleState.currentBottle = 1;
                    g_RetainSampleConfig.bottleNumber = 1;

                    /* 保存到KVDB */
                    if (cfg_save_retain(&g_RetainSampleConfig)) {
                        printf("[Task10] 留样瓶号已保存到KVDB: 1号瓶\r\n");
                    } else {
                        printf("[Task10] 警告：cfg_save_retain失败，标记缓存\r\n");
                        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN);
                    }
                    if (cfg_save_retain_state(&g_RetainBottleState)) {
                        printf("[Task10] 瓶位状态已保存到KVDB\r\n");
                    } else {
                        printf("[Task10] 警告：cfg_save_retain_state失败，标记缓存\r\n");
                        kvdb_cache_mark_dirty(KVDB_CACHE_RETAIN_STATE);
                    }

                    /* 复位完成，跳转主页 */
                    printf("[Task10] 瓶盘复位完成，跳转主页\r\n");
                    Screen_begin();
                }
                else
                {
                    printf("[Task10] 瓶盘复位失败，保持原有瓶号不变\r\n");
                }

                /* 重置状态机 */
                bottle_reset_active = 0;
                bottle_complete = 0;
                bottle_result = 0;
            }
        }
        
        /* 定期刷写缓存 */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_flush_time) >= FLUSH_INTERVAL)
        {
            /* 刷写KVDB缓存 */
            if (kvdb_cache_has_dirty())
            {
                kvdb_cache_flush_all();
            }
            
            /* 刷写TSDB缓存 */
            if (tsdb_cache_count() > 0)
            {
                tsdb_cache_flush_all();
            }
            
            last_flush_time = now;
        }
        
        /* 喂狗 */
        xEventGroupSetBits(event_handle, TASK10_EVENT_BIT);
    }
}

/* add user code begin 2 */

/* add user code end 2 */

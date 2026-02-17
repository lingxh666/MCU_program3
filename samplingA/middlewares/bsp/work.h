#ifndef __WORK_H__
#define __WORK_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "at32f403a_407.h"
#include "rtc.h"
#include "freertos_app.h"
#include "app_flashdb.h"

/* ===== 主协议选择（USART2）===== */
/* 已改为运行期由串口屏选择（g_CommSettingConfig.AutoCalibration） */

/* ===== 西安协议（UART7，独立）===== */
#define USE_PROTOCOL_XIAN     0  // 西安协议（0=禁用）
#define USE_UART7_AI          1  // UART7 485采集（超标/流量）

// 协议编号定义（运行时标识，保留兼容）
#define PROTOCOL_DAYUE      0  // 大岳协议（数采仪01）
#define PROTOCOL_GUOBIAO    1  // 国标协议
#define PROTOCOL_XIAN       2  // 西安协议
#define PROTOCOL_NANJING    3  // 南京协议
#define PROTOCOL_DAHU       4  // 大湖协议（新增）

// 西安协议寄存器地址定义
#define XIAN_INPUT_REG_BASE     30001   // 输入寄存器基址
#define XIAN_HOLDING_REG_BASE   40001   // 保持寄存器基址

// 大岳协议命令状态反馈定义
typedef struct {
    uint16_t a_bucket_status;    // A桶命令执行状态（40535）
    uint16_t b_bucket_status;    // B桶命令执行状态（40536）
    uint16_t ab_bucket_status;   // AB桶命令执行状态（40537）
    uint16_t retention_status;   // 留样命令执行状态（40540）
    uint32_t a_cmd_timestamp;    // A桶命令时间戳
    uint32_t b_cmd_timestamp;    // B桶命令时间戳
    uint32_t ab_cmd_timestamp;   // AB桶命令时间戳
    uint32_t retention_cmd_timestamp; // 留样命令时间戳
} DayueCommandStatus_t;

// 命令状态值定义
#define CMD_STATUS_IDLE       0x0000  // 空闲
#define CMD_STATUS_RECEIVED   0x8000  // 已接收
#define CMD_STATUS_EXECUTING  0xC000  // 执行中
#define CMD_STATUS_COMPLETED  0x4000  // 完成
#define CMD_STATUS_FAILED     0x2000  // 失败

// 声明全局状态变量
extern DayueCommandStatus_t g_dayue_cmd_status;

// 运行状态寄存器（30001-30002）
#define XIAN_REG_A_BUCKET_STATE     30001
#define XIAN_REG_B_BUCKET_STATE     30002

// 采样日志寄存器（30010-30019）
#define XIAN_REG_SAMPLING_MODE      30010
#define XIAN_REG_SAMPLING_BUCKET    30011
#define XIAN_REG_SAMPLING_YEAR      30012
#define XIAN_REG_SAMPLING_MONTH     30013
#define XIAN_REG_SAMPLING_DAY       30014
#define XIAN_REG_SAMPLING_HOUR      30015
#define XIAN_REG_SAMPLING_MINUTE    30016
#define XIAN_REG_SAMPLING_SEQUENCE  30017
#define XIAN_REG_SAMPLING_VOLUME    30018
#define XIAN_REG_SAMPLING_RESULT    30019

// 供样日志寄存器（30030-30038）
#define XIAN_REG_DELIVERY_MODE      30030
#define XIAN_REG_DELIVERY_BUCKET    30031
#define XIAN_REG_DELIVERY_YEAR      30032
#define XIAN_REG_DELIVERY_MONTH     30033
#define XIAN_REG_DELIVERY_DAY       30034
#define XIAN_REG_DELIVERY_HOUR      30035
#define XIAN_REG_DELIVERY_MINUTE    30036
#define XIAN_REG_DELIVERY_VOLUME    30037
#define XIAN_REG_DELIVERY_RESULT    30038

// 留样日志寄存器（30050-30059）
#define XIAN_REG_RETAIN_MODE        30050
#define XIAN_REG_RETAIN_REASON      30051
#define XIAN_REG_RETAIN_YEAR        30052
#define XIAN_REG_RETAIN_MONTH       30053
#define XIAN_REG_RETAIN_DAY         30054
#define XIAN_REG_RETAIN_HOUR        30055
#define XIAN_REG_RETAIN_MINUTE      30056
#define XIAN_REG_RETAIN_BOTTLE      30057
#define XIAN_REG_RETAIN_VOLUME      30058
#define XIAN_REG_RETAIN_RESULT      30059

// 校时命令寄存器（40001-40006）
#define XIAN_REG_TIME_YEAR          40001
#define XIAN_REG_TIME_MONTH         40002
#define XIAN_REG_TIME_DAY           40003
#define XIAN_REG_TIME_HOUR          40004
#define XIAN_REG_TIME_MINUTE        40005
#define XIAN_REG_TIME_SECOND        40006

// 西安协议功能码
#define XIAN_FUNC_READ_INPUT        0x04
#define XIAN_FUNC_WRITE_SINGLE      0x06
#define XIAN_FUNC_WRITE_MULTIPLE    0x10

#define MotorReverse gpio_bits_reset(GPIOE,  GPIO_PINS_8 )//转盘电机反转
#define MotorForward gpio_bits_set(GPIOE,  GPIO_PINS_8 )//转盘电机正转


void vSendData( usart_type *usart_x, const uint8_t *buf ,uint8_t len);
uint8_t Modbus(usart_type *usart_x,QueueHandle_t queue, uint8_t *buf, uint8_t Sendlen, uint8_t retryNum);
uint16_t CRC16_MODBUS( uint8_t *data, uint16_t length);
uint8_t vCrc16Check( uint8_t *data, uint16_t length);
uint32_t FindNum(char *pcBuf,char *left,char *right);
void FindString(char *pcBuf,char *left,char *right,char *text);
void SendData(usart_type *usart_x, char *buf, uint8_t len);
void extract_numbers_irq(const char* str, uint32_t* crc1, uint32_t* crc2);
float convertToFloatH(uint8_t data3, uint8_t data2, uint8_t data1, uint8_t data0);
void convertFloatToBytes(float value, uint8_t *bytes);


void delay_init(void);
void delay_us(uint32_t nus);
void delay_ms(uint16_t nms);
void delay_sec(uint16_t sec);
//uint16_t get_adcval(adc_type *adc_x,adc_channel_select_type adc_channe);
//uint16_t get_adcval_average(adc_type *adc_x, adc_channel_select_type adc_channe, uint8_t times);

uint8_t motor_send(usart_type *usart_x,QueueHandle_t queue, uint8_t const *buf, uint8_t len,uint8_t retryNum);
void set_motor_rpm(uint8_t dir);
void OutletThreeWayValveA(void);
void OutletThreeWayValveB(void);
void OutletThreeWayValveClose(void);

//void SysInit(void);
void MotorRun(uint8_t Snum,uint8_t Dir,uint16_t Speed);
void MotorStop(uint8_t Snum);

// 计算采样时间（秒）: 基于 g_CalibrationParams.samplingCalib 三个校准点，
// 通过离散组合(可重复)得到最接近 target_ml 的体积方案；若并列，优先时间短/段数少。
uint16_t calc_sampling_time_by_volume(uint16_t target_ml);

// 计算送样时间（秒）：使用 retainSampleCalib 校准（送样和留样使用同一个蠕动泵）
uint16_t calc_delivery_time_by_volume(uint16_t target_ml);

// 计算留样时间（秒）：使用 retainSampleCalib 校准
uint16_t calc_retain_time_by_volume(uint16_t target_ml);

// 根据留样时间反向计算体积（用于送样流程中的体积估算）
uint16_t calc_volume_by_retain_time(uint16_t time_seconds);

/* ===== 瓶位系统接口（基于PD0/PD1传感器） ===== */
#define BOTTLE_COUNT 24

/* 留样瓶位状态全局变量（extern声明） */
extern RetainBottleState g_RetainBottleState;
extern uint8_t g_current_bottle_number;

/* ===== 位图管理工具函数 ===== */

/* 检查指定瓶位是否已使用 */
uint8_t is_bottle_used(uint32_t usedMask, uint8_t bottle);

/* 标记指定瓶位为已使用 */
void mark_bottle_used(uint32_t *usedMask, uint8_t bottle);

/* 清除指定瓶位的已使用标记 */
void clear_bottle_used(uint32_t *usedMask, uint8_t bottle);

/* 从指定位置开始环形查找下一个空瓶 */
uint8_t find_next_empty_bottle(uint8_t start_from, uint32_t usedMask, uint8_t *out);

/* ===== 瓶位移动控制函数 ===== */

/* 查询当前零点位置的瓶号（1..24） */
uint8_t bottle_query_current_at_origin(void);

/* 自动寻找1号瓶并初始化系统 */
uint8_t bottle_auto_find_origin(uint16_t rpm, uint32_t timeout_ms);

/* 回零：将1号瓶移动到零点位置 */
uint8_t bottle_home_to_1(uint16_t rpm, uint32_t timeout_ms);

/* 非阻塞瓶位复位函数 */
uint8_t bottle_home_to_1_start(uint16_t rpm, uint32_t timeout_ms);
uint8_t bottle_home_to_1_check(void);
void bottle_home_to_1_stop(void);

/* 传感器状态检测函数 */
uint8_t bottle_sensor_is_working(void);
void bottle_sensor_reset_detection(void);

/* 移动到指定瓶号：movebott(n) 功能 */
uint8_t bottle_move_to(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);

/* 非阻塞瓶位移动函数 */
uint8_t bottle_move_to_start(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);
uint8_t bottle_move_to_check(void);
void bottle_move_to_stop(void);
uint8_t bottle_move_to_get_target(void);
uint8_t bottle_move_to_is_active(void);

/* ===== 留样瓶系统故障管理 ===== */

/* 故障类型定义 */
#define BOTTLE_FAULT_NONE           0x00  // 无故障
#define BOTTLE_FAULT_SENSOR_MISSING 0x01  // 传感器缺失
#define BOTTLE_FAULT_INIT_TIMEOUT   0x02  // 初始化超时
#define BOTTLE_FAULT_RESET_TIMEOUT  0x03  // 复位超时
#define BOTTLE_FAULT_MOVE_TIMEOUT   0x04  // 移动超时

/* 故障标志全局变量 */
extern volatile uint8_t g_bottle_system_fault;
extern volatile uint8_t g_bottle_fault_retry_count;

/* ★ 留样瓶首次初始化标志（开机后第一次留样时初始化） */
extern volatile uint8_t g_bottle_first_init_done;

/* 故障管理函数 */
void bottle_set_fault(uint8_t fault_code);
void bottle_clear_fault(void);
uint8_t bottle_get_fault(void);
uint8_t bottle_is_fault_active(void);

/* ★ 留样瓶初始化确保函数（首次留样时调用） */
uint8_t bottle_ensure_initialized(void);

// 瞬时留样执行函数
// start_bottle: 起始瓶号（0=自主查找空瓶）
// bottle_count: 瓶数量
// trigger_source: 触发来源（0=手动, 1=通讯）
uint8_t instant_retention_execute(uint8_t start_bottle, uint8_t bottle_count, uint8_t trigger_source);

uint8_t emptybottle(uint8_t target_bottle, uint16_t rpm, uint32_t timeout_ms);
uint8_t emptybottleall(uint16_t rpm, uint32_t timeout_ms);
void BottleEmpty(void);

// 瞬时留样函数
// start_bottle: 起始瓶号（0=自主查找空瓶）
// bottle_count: 瓶数量
// trigger_source: 触发来源（0=手动, 1=通讯）
uint8_t instant_retention_execute(uint8_t start_bottle, uint8_t bottle_count, uint8_t trigger_source);

// 通讯触发留样窗口上下文结构体
typedef struct {
    uint8_t bucket_id;             // 桶ID（0=A桶, 1=B桶）
    uint32_t delivery_time;        // 送样时间
    uint32_t window_start_sec;     // 窗口开始时间（秒）
    uint32_t window_end_sec;       // 窗口结束时间（秒）
    uint8_t in_window;             // 是否在窗口内（0/1）
    uint8_t comm_trigger_received; // 是否收到通讯触发信号
    uint8_t retain_executed;       // 是否已执行留样（防重复）
    uint8_t window_checked;        // 窗口是否已检查
    uint8_t cycle_completed;       // 周期是否完成
} CommRetainWindowContext;

// 声明外部通讯触发留样窗口上下文变量
extern CommRetainWindowContext g_comm_retain_ctx_a;
extern CommRetainWindowContext g_comm_retain_ctx_b;
extern uint8_t g_comm_retain_bottle_count;

#ifdef __cplusplus
}
#endif

#endif

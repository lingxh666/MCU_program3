/**
 * @file mb_reg_xian.c
 * @brief 西安协议Modbus寄存器回调实现
 *
 * 西安协议特点：
 * - 站号不固定，仅做CRC校验
 * - 应答使用问询帧的站号
 * - 使用UART7通讯
 *
 * 寄存器映射：
 * - 输入寄存器(0x04):
 *   30001-30002: A/B桶状态
 *   30010-30019: 采样日志(10个寄存器)
 *   30030-30038: 送样日志(9个寄存器)
 *   30050-30059: 留样日志(10个寄存器)
 *
 * - 保持寄存器(0x06/0x10):
 *   40001-40006: 校时功能(6个寄存器)
 */

#include "mb.h"
#include "mb_instance.h"
#include "work.h"
#include "freertos_app.h"
#include "rtc.h"
#include <stdio.h>
#include <string.h>

/* ============ 外部变量声明 ============ */

/* 系统状态 */
extern State g_State;

/* 西安协议日志结构体 */
extern XianSamplingLog_t g_XianSamplingLog;
extern XianDeliveryLog_t g_XianDeliveryLog;
extern XianRetainLog_t g_XianRetainLog;

/* RTC函数已在rtc.h 中声明 */

/* ============ 西安协议地址定义 ============ */

/* 输入寄存器基址 */
#define XIAN_INPUT_BASE 30001

/* 输入寄存器地址 */
#define XIAN_REG_A_STATE 30001  /* A桶状态 */
#define XIAN_REG_B_STATE 30002  /* B桶状态 */
#define XIAN_REG_SAMPLING 30010 /* 采样日志起始 */
#define XIAN_REG_DELIVERY 30030 /* 送样日志起始 */
#define XIAN_REG_RETAIN 30050   /* 留样日志起始 */

/* 保持寄存器基址 */
#define XIAN_HOLDING_BASE 40001

/* 保持寄存器地址 */
#define XIAN_REG_TIME 40001 /* 校时起始 */

/* ===== Xian read block helpers ===== */
#define XIAN_MAX_INPUT_REGS 10

static uint8_t xian_try_read_input_block(uint8_t *dst, uint16_t modbus_addr, uint16_t nregs,
                                         uint16_t block_start, uint16_t block_regs,
                                         uint16_t (*build)(uint8_t *buf, uint16_t buf_size))
{
    uint32_t req_end;
    uint32_t block_end;
    uint16_t actual_regs;
    uint8_t block_buf[XIAN_MAX_INPUT_REGS * 2];

    if (nregs == 0) {
        return 0;
    }

    req_end = (uint32_t)modbus_addr + nregs - 1;
    block_end = (uint32_t)block_start + block_regs - 1;
    if (modbus_addr < block_start || req_end > block_end) {
        return 0;
    }

    actual_regs = build(block_buf, sizeof(block_buf));
    if (actual_regs == 0) {
        return 0;
    }
    if (req_end > (uint32_t)block_start + actual_regs - 1) {
        return 0;
    }

    memcpy(dst, &block_buf[(modbus_addr - block_start) * 2], nregs * 2);
    return 1;
}

static uint16_t xian_build_ab_state_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    printf("[西安] 读取桶状态\r\n");

    /* A桶状态 */
    buf[idx++] = 0;
    buf[idx++] = g_State.ABucketState;

    /* B桶状态 */
    buf[idx++] = 0;
    buf[idx++] = g_State.BBucketState;

    return idx / 2;
}

static uint16_t xian_build_sampling_log_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    printf("[西安] 读取采样日志\r\n");

    /* mode */
    buf[idx++] = (g_XianSamplingLog.mode >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.mode & 0xFF;

    /* bucketId */
    buf[idx++] = (g_XianSamplingLog.bucketId >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.bucketId & 0xFF;

    /* year */
    buf[idx++] = (g_XianSamplingLog.year >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.year & 0xFF;

    /* month */
    buf[idx++] = (g_XianSamplingLog.month >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.month & 0xFF;

    /* day */
    buf[idx++] = (g_XianSamplingLog.day >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.day & 0xFF;

    /* hour */
    buf[idx++] = (g_XianSamplingLog.hour >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.hour & 0xFF;

    /* minute */
    buf[idx++] = (g_XianSamplingLog.minute >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.minute & 0xFF;

    /* sequence */
    buf[idx++] = (g_XianSamplingLog.sequence >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.sequence & 0xFF;

    /* volume */
    buf[idx++] = (g_XianSamplingLog.volume >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.volume & 0xFF;

    /* result */
    buf[idx++] = (g_XianSamplingLog.result >> 8) & 0xFF;
    buf[idx++] = g_XianSamplingLog.result & 0xFF;

    return idx / 2;
}

static uint16_t xian_build_delivery_log_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    printf("[西安] 读取送样日志\r\n");

    /* mode */
    buf[idx++] = (g_XianDeliveryLog.mode >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.mode & 0xFF;

    /* bucketId */
    buf[idx++] = (g_XianDeliveryLog.bucketId >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.bucketId & 0xFF;

    /* year */
    buf[idx++] = (g_XianDeliveryLog.year >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.year & 0xFF;

    /* month */
    buf[idx++] = (g_XianDeliveryLog.month >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.month & 0xFF;

    /* day */
    buf[idx++] = (g_XianDeliveryLog.day >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.day & 0xFF;

    /* hour */
    buf[idx++] = (g_XianDeliveryLog.hour >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.hour & 0xFF;

    /* minute */
    buf[idx++] = (g_XianDeliveryLog.minute >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.minute & 0xFF;

    /* volume */
    buf[idx++] = (g_XianDeliveryLog.volume >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.volume & 0xFF;

    /* result */
    buf[idx++] = (g_XianDeliveryLog.result >> 8) & 0xFF;
    buf[idx++] = g_XianDeliveryLog.result & 0xFF;

    return idx / 2;
}

static uint16_t xian_build_retain_log_block(uint8_t *buf, uint16_t buf_size)
{
    uint16_t idx = 0;
    (void)buf_size;

    printf("[西安] 读取留样日志\r\n");

    /* mode */
    buf[idx++] = (g_XianRetainLog.mode >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.mode & 0xFF;

    /* reason */
    buf[idx++] = (g_XianRetainLog.reason >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.reason & 0xFF;

    /* year */
    buf[idx++] = (g_XianRetainLog.year >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.year & 0xFF;

    /* month */
    buf[idx++] = (g_XianRetainLog.month >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.month & 0xFF;

    /* day */
    buf[idx++] = (g_XianRetainLog.day >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.day & 0xFF;

    /* hour */
    buf[idx++] = (g_XianRetainLog.hour >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.hour & 0xFF;

    /* minute */
    buf[idx++] = (g_XianRetainLog.minute >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.minute & 0xFF;

    /* bottleId */
    buf[idx++] = (g_XianRetainLog.bottleId >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.bottleId & 0xFF;

    /* volume */
    buf[idx++] = (g_XianRetainLog.volume >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.volume & 0xFF;

    /* result */
    buf[idx++] = (g_XianRetainLog.result >> 8) & 0xFF;
    buf[idx++] = g_XianRetainLog.result & 0xFF;

    return idx / 2;
}

/* ============ 输入寄存器回调 (0x04) ============ */

/**
 * @brief 西安协议读输入寄存器回调
 * @param inst Modbus实例
 * @param buf 数据缓冲区(用于填充读取的数据)
 * @param addr 起始地址(通讯地址，需加基址30001)
 * @param nregs 寄存器数量
 * @return 0=成功, 其他=异常码
 */
static uint8_t xian_read_input(MBInstance_t *inst, uint8_t *buf,
                               uint16_t addr, uint16_t nregs)
{
    (void)inst;

    /* ????????? -> Modbus?? */
    uint16_t modbus_addr = addr + XIAN_INPUT_BASE;

    printf("[??] ??????: ??=%d, ??=%d\r\n", modbus_addr, nregs);

    if (xian_try_read_input_block(buf, modbus_addr, nregs, XIAN_REG_A_STATE, 2,
                                  xian_build_ab_state_block)) {
        return 0;
    }

    if (xian_try_read_input_block(buf, modbus_addr, nregs, XIAN_REG_SAMPLING, 10,
                                  xian_build_sampling_log_block)) {
        return 0;
    }

    if (xian_try_read_input_block(buf, modbus_addr, nregs, XIAN_REG_DELIVERY, 9,
                                  xian_build_delivery_log_block)) {
        return 0;
    }

    if (xian_try_read_input_block(buf, modbus_addr, nregs, XIAN_REG_RETAIN, 10,
                                  xian_build_retain_log_block)) {
        return 0;
    }

    printf("[??] ?????????: %d\r\n", modbus_addr);
    return MB_EX_ILLEGAL_ADDRESS;
}

/* ============ 保持寄存器回调 (0x03/0x06/0x10) ============ */

/**
 * @brief 西安协议读/写保持寄存器回调
 * @param inst Modbus实例
 * @param buf 数据缓冲区
 *            - 读取时：用于填充读取的数据
 *            - 写入时：包含要写入的数据
 * @param addr 起始地址(通讯地址，需加基址40001)
 * @param nregs 寄存器数量
 * @param mode 操作模式（MB_REG_READ=读，MB_REG_WRITE=写）
 * @return 0=成功, 其他=异常码
 */
static uint8_t xian_rw_holding(MBInstance_t *inst, uint8_t *buf,
                               uint16_t addr, uint16_t nregs, uint8_t mode)
{
    (void)inst;

    /* 地址转换：通讯地址 -> Modbus地址 */
    uint16_t modbus_addr = addr + XIAN_HOLDING_BASE;

    printf("[西安] 保持寄存器操作: 地址=%d, 数量=%d, 模式=%s\r\n",
           modbus_addr, nregs, mode == MB_REG_READ ? "读" : "写");

    /* ===== 读保持寄存器(0x03) ===== */
    if (mode == MB_REG_READ)
    {
        /* 西安协议目前没有定义读保持寄存器功能 */
        printf("[西安] 不支持读保持寄存器\r\n");
        return MB_EX_ILLEGAL_FUNCTION;
    }

    /* ===== 写单个寄存器(0x06) ===== */
    if (nregs == 1)
    {
        uint16_t regValue = (buf[0] << 8) | buf[1];

        printf("[西安] 写单个寄存器: 地址=%d, 值=0x%04X\r\n", modbus_addr, regValue);

        /* 西安协议写单个寄存器目前只返回成功，不做实际处理 */
        /* 如有需要可在此添加具体功能 */

        return 0;
    }

    /* ===== 写多个寄存器(0x10) ===== */

    /* 40001-40006: 校时功能(6个寄存器) */
    if (modbus_addr == XIAN_REG_TIME && nregs == 6)
    {
        uint16_t year = (buf[0] << 8) | buf[1];
        uint16_t month = (buf[2] << 8) | buf[3];
        uint16_t day = (buf[4] << 8) | buf[5];
        uint16_t hour = (buf[6] << 8) | buf[7];
        uint16_t minute = (buf[8] << 8) | buf[9];
        uint16_t second = (buf[10] << 8) | buf[11];

        printf("[西安] 设置时间: %04d-%02d-%02d %02d:%02d:%02d\r\n",
               year, month, day, hour, minute, second);

        /* 构造时间结构 */
        calendar_type time_struct;
        time_struct.year = year;
        time_struct.month = (uint8_t)month;
        time_struct.date = (uint8_t)day;
        time_struct.week = 0; /* 西安协议不设置星期 */
        time_struct.hour = (uint8_t)hour;
        time_struct.min = (uint8_t)minute;
        time_struct.sec = (uint8_t)second;

        /* 设置RTC时间 */
        rtc_time_set(&time_struct);

        printf("[西安] RTC时钟已更新\r\n");
        return 0;
    }

    /* 未知地址 */
    printf("[西安] 未实现的保持寄存器地址: %d (nregs=%d)\r\n", modbus_addr, nregs);
    return MB_EX_ILLEGAL_ADDRESS;
}

/* ============ 西安协议实例初始化 ============ */

/**
 * @brief 注册西安协议回调函数
 * @param inst Modbus实例指针
 *
 * @note 西安协议使用MB_MODE_BROADCAST模式，忽略站号检查
 */
void xian_register_callbacks(MBInstance_t *inst)
{
    /* 注册输入寄存器回调(0x04) */
    eMBRegisterInputCB_Inst(inst, xian_read_input);

    /* 注册保持寄存器回调(0x03/0x06/0x10) */
    eMBRegisterHoldingCB_Inst(inst, xian_rw_holding);
}

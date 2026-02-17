#ifndef BSP_RTC_H
#define BSP_RTC_H

#include "at32f435_437.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ERTC BPR魔数，用于判断是否已初始化 */
#define ERTC_BPR_MAGIC    0x1234

/* 北京时间 UTC+8 偏移（秒） */
#define BEIJING_UTC_OFFSET  (8 * 3600)

/* 日期时间结构体 */
typedef struct {
  uint8_t year;   /* 0~99, 对应2000~2099 */
  uint8_t month;  /* 1~12 */
  uint8_t day;    /* 1~31 */
  uint8_t week;   /* 1~7, 1=周一 */
  uint8_t hour;   /* 0~23 */
  uint8_t min;    /* 0~59 */
  uint8_t sec;    /* 0~59 */
} rtc_datetime_t;

/* 初始化ERTC（检查BPR判断是否首次，首次才设置默认时间） */
void bsp_rtc_init(void);

/* 设置日期时间（北京时间） */
void rtc_set_time(uint8_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t min, uint8_t sec);

/* 获取日期时间（北京时间） */
void rtc_get_time(rtc_datetime_t *dt);

/* 获取Unix时间戳（UTC，基于2000-01-01 00:00:00 UTC） */
uint32_t rtc_get_timestamp(void);

/* 从Unix时间戳设置时间（输入为UTC时间戳） */
void rtc_set_timestamp(uint32_t timestamp);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RTC_H */

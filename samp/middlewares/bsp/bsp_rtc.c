#include "bsp_rtc.h"

/* 每月天数表（非闰年） */
static const uint8_t days_in_month[] = {
  0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* 判断闰年（2000+year） */
static uint8_t is_leap_year(uint16_t y)
{
  return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

/* 计算某年某月的天数 */
static uint8_t month_days(uint16_t y, uint8_t m)
{
  if(m == 2 && is_leap_year(y)) return 29;
  return days_in_month[m];
}

/* 计算星期几（基姆拉尔森公式），返回1~7(周一~周日) */
static uint8_t calc_weekday(uint16_t y, uint8_t m, uint8_t d)
{
  int w;
  if(m <= 2) { m += 12; y--; }
  w = (d + 2*m + 3*(m+1)/5 + y + y/4 - y/100 + y/400 + 1) % 7;
  return (uint8_t)(w == 0 ? 7 : w);
}

void bsp_rtc_init(void)
{
  /* 允许访问电池供电域 */
  pwc_battery_powered_domain_access(TRUE);

  /* 检查BPR魔数，判断ERTC是否已初始化（电池保持） */
  if(ertc_bpr_data_read(ERTC_DT1) == ERTC_BPR_MAGIC)
  {
    /* 已初始化，仅配置时钟源并等待同步 */
    crm_ertc_clock_select(CRM_ERTC_CLOCK_LEXT);
    crm_ertc_clock_enable(TRUE);
    ertc_wait_update();
    return;
  }

  /* 首次初始化：配置时钟源、分频器、默认时间 */
  crm_ertc_clock_select(CRM_ERTC_CLOCK_LEXT);
  crm_ertc_clock_enable(TRUE);
  ertc_reset();
  ertc_wait_update();
  ertc_divider_set(127, 255);
  ertc_hour_mode_set(ERTC_HOUR_MODE_24);

  /* 默认时间：2025-01-01 00:00:00 北京时间 */
  ertc_date_set(25, 1, 1, 3);  /* 2025-01-01 周三 */
  ertc_time_set(0, 0, 0, ERTC_24H);

  /* 写入魔数，下次上电不再重置时间 */
  ertc_bpr_data_write(ERTC_DT1, ERTC_BPR_MAGIC);
}

void rtc_set_time(uint8_t year, uint8_t month, uint8_t day,
                  uint8_t hour, uint8_t min, uint8_t sec)
{
  uint8_t week = calc_weekday(2000 + year, month, day);
  ertc_date_set(year, month, day, week);
  ertc_time_set(hour, min, sec, ERTC_24H);
}

void rtc_get_time(rtc_datetime_t *dt)
{
  ertc_time_type time;
  ertc_calendar_get(&time);
  dt->year  = time.year;
  dt->month = time.month;
  dt->day   = time.day;
  dt->week  = time.week;
  dt->hour  = time.hour;
  dt->min   = time.min;
  dt->sec   = time.sec;
}

/* 日期时间转Unix时间戳（基于2000-01-01 00:00:00 UTC） */
static uint32_t datetime_to_stamp(uint8_t y, uint8_t mo, uint8_t d,
                                  uint8_t h, uint8_t mi, uint8_t s)
{
  uint32_t days = 0;
  uint16_t i;
  uint16_t full_year = 2000 + y;

  /* 累加整年天数 */
  for(i = 2000; i < full_year; i++)
    days += is_leap_year(i) ? 366 : 365;

  /* 累加整月天数 */
  for(i = 1; i < mo; i++)
    days += month_days(full_year, (uint8_t)i);

  days += (d - 1);

  return days * 86400UL + h * 3600UL + mi * 60UL + s;
}

/* Unix时间戳转日期时间（基于2000-01-01 00:00:00 UTC） */
static void stamp_to_datetime(uint32_t stamp, uint8_t *y, uint8_t *mo,
                              uint8_t *d, uint8_t *h, uint8_t *mi, uint8_t *s)
{
  uint32_t rem = stamp;
  uint16_t year = 2000;
  uint16_t yd;
  uint8_t md;

  *s  = (uint8_t)(rem % 60); rem /= 60;
  *mi = (uint8_t)(rem % 60); rem /= 60;
  *h  = (uint8_t)(rem % 24); rem /= 24;

  /* rem = 总天数 */
  while(1)
  {
    yd = is_leap_year(year) ? 366 : 365;
    if(rem < yd) break;
    rem -= yd;
    year++;
  }
  *y = (uint8_t)(year - 2000);

  *mo = 1;
  while(1)
  {
    md = month_days(year, *mo);
    if(rem < md) break;
    rem -= md;
    (*mo)++;
  }
  *d = (uint8_t)(rem + 1);
}

uint32_t rtc_get_timestamp(void)
{
  rtc_datetime_t dt;
  uint32_t beijing_stamp;

  rtc_get_time(&dt);
  /* ERTC存储的是北京时间，转为UTC需减去8小时 */
  beijing_stamp = datetime_to_stamp(dt.year, dt.month, dt.day,
                                    dt.hour, dt.min, dt.sec);
  if(beijing_stamp >= BEIJING_UTC_OFFSET)
    return beijing_stamp - BEIJING_UTC_OFFSET;
  return 0;
}

void rtc_set_timestamp(uint32_t timestamp)
{
  uint8_t y, mo, d, h, mi, s;
  /* 输入UTC时间戳，加8小时转北京时间后存入ERTC */
  stamp_to_datetime(timestamp + BEIJING_UTC_OFFSET, &y, &mo, &d, &h, &mi, &s);
  rtc_set_time(y, mo, d, h, mi, s);
}

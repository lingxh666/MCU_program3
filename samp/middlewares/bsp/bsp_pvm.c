#include "bsp_pvm.h"

/* PVM数据存储在bank2最后2KB扇区 */
#define PVM_FLASH_ADDR   0x080FF800u
#define PVM_SECTOR_SIZE  2048u
#define PVM_RECORD_SIZE  16u
#define PVM_MAX_RECORDS  (PVM_SECTOR_SIZE / PVM_RECORD_SIZE)

static uint32_t g_count;
static uint32_t g_date;
static uint32_t g_time;
static uint32_t g_write_idx;

void bsp_pvm_init(void)
{
  uint32_t *base = (uint32_t *)PVM_FLASH_ADDR;

  g_count = 0;
  g_date = 0;
  g_time = 0;
  g_write_idx = 0;

  /* 每条记录4个word: [count][date][time][reserved] */
  for(uint32_t i = 0; i < PVM_MAX_RECORDS; i++) {
    uint32_t off = i * 4;
    if(base[off] != 0xFFFFFFFFu) {
      g_count = base[off];
      g_date  = base[off + 1];
      g_time  = base[off + 2];
      g_write_idx = i + 1;
    } else {
      break;
    }
  }

  if(g_write_idx >= PVM_MAX_RECORDS) {
    flash_unlock();
    flash_sector_erase(PVM_FLASH_ADDR);
    flash_lock();
    g_write_idx = 0;
  }

  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  pwc_pvm_level_select(PWC_PVM_VOLTAGE_2V9);
  pwc_power_voltage_monitor_enable(TRUE);

  exint_init_type exint_cfg;
  exint_cfg.line_select   = EXINT_LINE_16;
  exint_cfg.line_enable   = TRUE;
  exint_cfg.line_mode     = EXINT_LINE_INTERRUPT;
  exint_cfg.line_polarity = EXINT_TRIGGER_RISING_EDGE;
  exint_init(&exint_cfg);

  nvic_irq_enable(PVM_IRQn, 0, 0);
}

uint32_t bsp_pvm_get_count(void) { return g_count; }
uint32_t bsp_pvm_get_date(void)  { return g_date; }
uint32_t bsp_pvm_get_time(void)  { return g_time; }

void bsp_pvm_irq(void)
{
  if(g_write_idx >= PVM_MAX_RECORDS) return;

  ertc_time_type t;
  ertc_calendar_get(&t);
  uint32_t date = (uint32_t)t.year * 10000u + (uint32_t)t.month * 100u + (uint32_t)t.day;
  uint32_t time = (uint32_t)t.hour * 10000u + (uint32_t)t.min * 100u + (uint32_t)t.sec;

  g_count++;
  uint32_t addr = PVM_FLASH_ADDR + g_write_idx * PVM_RECORD_SIZE;

  flash_unlock();
  flash_word_program(addr,      g_count);
  flash_word_program(addr + 4,  date);
  flash_word_program(addr + 8,  time);
  flash_word_program(addr + 12, 0);
  flash_lock();

  g_write_idx++;
}

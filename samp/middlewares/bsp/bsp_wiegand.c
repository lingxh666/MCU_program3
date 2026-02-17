#include "bsp_wiegand.h"

/* Wiegand接收状态 */
static volatile uint64_t wg_raw_data;
static volatile uint8_t  wg_bit_count;
static volatile uint32_t wg_last_bit_ms;
static volatile uint32_t wg_ms_counter;

/* 已解析的卡号 */
static volatile uint32_t wg_card_id;
static volatile uint8_t  wg_card_valid;

/* Wiegand 26位解析：[P0] [FC:8bit] [CN:16bit] [P1] */
static uint8_t wiegand_parse_26(uint32_t raw, uint32_t *card_id)
{
  uint8_t i, ones;

  /* 偶校验：高13位(bit25~bit13)，1的个数应为偶数 */
  ones = 0;
  for(i = 13; i < 26; i++)
    if(raw & ((uint32_t)1 << i)) ones++;
  if(ones & 1) return 0;

  /* 奇校验：低13位(bit12~bit0)，1的个数应为奇数 */
  ones = 0;
  for(i = 0; i < 13; i++)
    if(raw & ((uint32_t)1 << i)) ones++;
  if(!(ones & 1)) return 0;

  /* FC(8bit) << 16 | CN(16bit) */
  *card_id = ((raw >> 17) & 0xFF) * 65536u + ((raw >> 1) & 0xFFFF);
  return 1;
}

/* 接收单个bit，供D0/D1中断调用 */
static void receive_bit(uint8_t bit_val)
{
  uint32_t now = wg_ms_counter;

  /* 超时重置 */
  if(wg_bit_count > 0 && (now - wg_last_bit_ms) > WIEGAND_TIMEOUT_MS)
  {
    wg_raw_data  = 0;
    wg_bit_count = 0;
  }

  if(wg_bit_count >= WIEGAND_MAX_BITS) return;

  wg_raw_data = (wg_raw_data << 1) | bit_val;
  wg_bit_count++;
  wg_last_bit_ms = now;
}

void wiegand_init(void)
{
  wg_raw_data    = 0;
  wg_bit_count   = 0;
  wg_last_bit_ms = 0;
  wg_ms_counter  = 0;
  wg_card_id     = 0;
  wg_card_valid  = 0;

  /* EXINT4/EXINT7的GPIO和中断已在wk_config中配置，此处启用NVIC */
  nvic_irq_enable(EXINT4_IRQn, 5, 0);
  nvic_irq_enable(EXINT9_5_IRQn, 5, 0);
}

uint8_t wiegand_get_card_id(uint32_t *card_id)
{
  if(!wg_card_valid) return 0;
  *card_id = wg_card_id;
  wg_card_valid = 0;
  return 1;
}

/* D0中断：接收0位 */
void wiegand_d0_irq(void)
{
  receive_bit(0);
}

/* D1中断：接收1位 */
void wiegand_d1_irq(void)
{
  receive_bit(1);
}

/* TMR7 1ms中断中调用，检测帧结束超时 */
void wiegand_timeout_check(void)
{
  wg_ms_counter++;

  if(wg_bit_count == 0) return;
  if((wg_ms_counter - wg_last_bit_ms) < WIEGAND_TIMEOUT_MS) return;

  /* 超时，帧接收完成 */
  if(wg_bit_count == 26)
  {
    uint32_t id;
    if(wiegand_parse_26((uint32_t)(wg_raw_data & 0x3FFFFFF), &id))
    {
      wg_card_id = id;
      wg_card_valid = 1;
    }
  }
  else if(wg_bit_count == 34)
  {
    /* Wiegand 34: 取中间32位作为卡号 */
    wg_card_id = (uint32_t)((wg_raw_data >> 1) & 0xFFFFFFFF);
    wg_card_valid = 1;
  }

  wg_raw_data  = 0;
  wg_bit_count = 0;
}

#include "bsp_qspi_flash.h"
#include <string.h>

/* 内部扇区缓冲（用于写入时的读-改-写） */
static uint8_t sector_buf[QFLASH_SECTOR_SIZE];

/* ======================== QSPI Command Helpers ======================== */
static void qspi_cmd_send(qspi_cmd_type *cmd)
{
  qspi_cmd_operation_kick(QSPI2, cmd);
  while(qspi_flag_get(QSPI2, QSPI_CMDSTS_FLAG) == RESET);
  qspi_flag_clear(QSPI2, QSPI_CMDSTS_FLAG);
}

static void qspi_write_enable(void)
{
  qspi_cmd_type cmd;
  cmd.pe_mode_enable = FALSE;
  cmd.pe_mode_operate_code = 0;
  cmd.instruction_code = QFLASH_CMD_WRITE_ENABLE;
  cmd.instruction_length = QSPI_CMD_INSLEN_1_BYTE;
  cmd.address_code = 0;
  cmd.address_length = QSPI_CMD_ADRLEN_0_BYTE;
  cmd.data_counter = 0;
  cmd.second_dummy_cycle_num = 0;
  cmd.operation_mode = QSPI_OPERATE_MODE_111;
  cmd.read_status_config = QSPI_RSTSC_HW_AUTO;
  cmd.read_status_enable = FALSE;
  cmd.write_data_enable = TRUE;
  qspi_cmd_send(&cmd);
}

static void qspi_wait_busy(void)
{
  qspi_cmd_type cmd;
  cmd.pe_mode_enable = FALSE;
  cmd.pe_mode_operate_code = 0;
  cmd.instruction_code = QFLASH_CMD_READ_SR1;
  cmd.instruction_length = QSPI_CMD_INSLEN_1_BYTE;
  cmd.address_code = 0;
  cmd.address_length = QSPI_CMD_ADRLEN_0_BYTE;
  cmd.data_counter = 0;
  cmd.second_dummy_cycle_num = 0;
  cmd.operation_mode = QSPI_OPERATE_MODE_111;
  cmd.read_status_config = QSPI_RSTSC_HW_AUTO;
  cmd.read_status_enable = TRUE;
  cmd.write_data_enable = FALSE;
  qspi_cmd_send(&cmd);
}

/* ======================== Init & Read ID ======================== */
uint8_t qspi_flash_init(void)
{
  /* QSPI2 GPIO和外设已在 wk_qspi2_init() 中初始化 */
  /* 切换到命令端口模式 */
  qspi_xip_enable(QSPI2, FALSE);
  uint16_t id = qspi_flash_read_id();
  if(id == ZD25Q64_ID || id == W25Q64_ID ||
     id == ZD25Q128_ID || id == W25Q128_ID)
    return 1;
  return 0;
}

uint16_t qspi_flash_read_id(void)
{
  qspi_cmd_type cmd;
  cmd.pe_mode_enable = FALSE;
  cmd.pe_mode_operate_code = 0;
  cmd.instruction_code = QFLASH_CMD_READ_ID;
  cmd.instruction_length = QSPI_CMD_INSLEN_1_BYTE;
  cmd.address_code = 0;
  cmd.address_length = QSPI_CMD_ADRLEN_3_BYTE;
  cmd.data_counter = 2;
  cmd.second_dummy_cycle_num = 0;
  cmd.operation_mode = QSPI_OPERATE_MODE_111;
  cmd.read_status_config = QSPI_RSTSC_HW_AUTO;
  cmd.read_status_enable = FALSE;
  cmd.write_data_enable = FALSE;
  qspi_cmd_operation_kick(QSPI2, &cmd);

  while(qspi_flag_get(QSPI2, QSPI_RXFIFORDY_FLAG) == RESET);
  uint8_t mf = qspi_byte_read(QSPI2);
  uint8_t id = qspi_byte_read(QSPI2);

  while(qspi_flag_get(QSPI2, QSPI_CMDSTS_FLAG) == RESET);
  qspi_flag_clear(QSPI2, QSPI_CMDSTS_FLAG);
  return ((uint16_t)mf << 8) | id;
}

/* ======================== Read ======================== */
void qspi_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
  qspi_xip_enable(QSPI2, TRUE);
  memcpy(buf, (const void *)(QSPI2_MEM_BASE + addr), len);
  qspi_xip_enable(QSPI2, FALSE);
}

/* ======================== Page Program (max 256B) ======================== */
static void qspi_flash_page_write(uint32_t addr, uint8_t *buf, uint32_t len)
{
  if(len == 0 || len > QFLASH_PAGE_SIZE) return;

  qspi_write_enable();

  qspi_cmd_type cmd;
  cmd.pe_mode_enable = FALSE;
  cmd.pe_mode_operate_code = 0;
  cmd.instruction_code = QFLASH_CMD_PAGE_PROGRAM;
  cmd.instruction_length = QSPI_CMD_INSLEN_1_BYTE;
  cmd.address_code = addr;
  cmd.address_length = QSPI_CMD_ADRLEN_3_BYTE;
  cmd.data_counter = len;
  cmd.second_dummy_cycle_num = 0;
  cmd.operation_mode = QSPI_OPERATE_MODE_111;
  cmd.read_status_config = QSPI_RSTSC_HW_AUTO;
  cmd.read_status_enable = FALSE;
  cmd.write_data_enable = TRUE;

  qspi_cmd_operation_kick(QSPI2, &cmd);

  uint32_t i;
  for(i = 0; i < len; i++) {
    while(qspi_flag_get(QSPI2, QSPI_TXFIFORDY_FLAG) == RESET);
    qspi_byte_write(QSPI2, buf[i]);
  }

  while(qspi_flag_get(QSPI2, QSPI_CMDSTS_FLAG) == RESET);
  qspi_flag_clear(QSPI2, QSPI_CMDSTS_FLAG);

  qspi_wait_busy();
}

/* ======================== Write No Check (跨页自动拆分) ======================== */
static void qspi_flash_write_nocheck(uint32_t addr, uint8_t *buf, uint32_t len)
{
  uint32_t page_remain = QFLASH_PAGE_SIZE - (addr % QFLASH_PAGE_SIZE);
  if(len <= page_remain) page_remain = len;

  while(1)
  {
    qspi_flash_page_write(addr, buf, page_remain);
    if(len == page_remain) break;
    buf += page_remain;
    addr += page_remain;
    len -= page_remain;
    page_remain = (len > QFLASH_PAGE_SIZE) ? QFLASH_PAGE_SIZE : len;
  }
}

/* ======================== Write (带读改写) ======================== */
void qspi_flash_write(uint32_t addr, uint8_t *buf, uint32_t len)
{
  uint32_t sec_pos, sec_off, sec_remain;
  uint32_t i;

  sec_pos = addr / QFLASH_SECTOR_SIZE;
  sec_off = addr % QFLASH_SECTOR_SIZE;
  sec_remain = QFLASH_SECTOR_SIZE - sec_off;
  if(len <= sec_remain) sec_remain = len;

  while(1)
  {
    qspi_flash_read(sec_pos * QFLASH_SECTOR_SIZE, sector_buf, QFLASH_SECTOR_SIZE);

    /* 检查是否需要擦除 */
    for(i = 0; i < sec_remain; i++)
    {
      if(sector_buf[sec_off + i] != 0xFF) break;
    }

    if(i < sec_remain)
    {
      /* 需要擦除 */
      qspi_flash_erase_sector(sec_pos * QFLASH_SECTOR_SIZE);
      for(i = 0; i < sec_remain; i++)
        sector_buf[sec_off + i] = buf[i];
      qspi_flash_write_nocheck(sec_pos * QFLASH_SECTOR_SIZE, sector_buf, QFLASH_SECTOR_SIZE);
    }
    else
    {
      qspi_flash_write_nocheck(addr, buf, sec_remain);
    }

    if(len == sec_remain) break;

    sec_pos++;
    sec_off = 0;
    buf += sec_remain;
    addr += sec_remain;
    len -= sec_remain;
    sec_remain = (len > QFLASH_SECTOR_SIZE) ? QFLASH_SECTOR_SIZE : len;
  }
}

/* ======================== Erase ======================== */
static void qspi_erase_cmd(uint8_t instruction, uint32_t addr, qspi_cmd_adrlen_type addr_len)
{
  qspi_write_enable();

  qspi_cmd_type cmd;
  cmd.pe_mode_enable = FALSE;
  cmd.pe_mode_operate_code = 0;
  cmd.instruction_code = instruction;
  cmd.instruction_length = QSPI_CMD_INSLEN_1_BYTE;
  cmd.address_code = addr;
  cmd.address_length = addr_len;
  cmd.data_counter = 0;
  cmd.second_dummy_cycle_num = 0;
  cmd.operation_mode = QSPI_OPERATE_MODE_111;
  cmd.read_status_config = QSPI_RSTSC_HW_AUTO;
  cmd.read_status_enable = FALSE;
  cmd.write_data_enable = TRUE;
  qspi_cmd_send(&cmd);

  qspi_wait_busy();
}

void qspi_flash_erase_sector(uint32_t addr)
{
  qspi_erase_cmd(QFLASH_CMD_SECTOR_ERASE, addr, QSPI_CMD_ADRLEN_3_BYTE);
}

void qspi_flash_erase_block(uint32_t addr)
{
  qspi_erase_cmd(QFLASH_CMD_BLOCK_ERASE, addr, QSPI_CMD_ADRLEN_3_BYTE);
}

void qspi_flash_erase_chip(void)
{
  qspi_erase_cmd(QFLASH_CMD_CHIP_ERASE, 0, QSPI_CMD_ADRLEN_0_BYTE);
}

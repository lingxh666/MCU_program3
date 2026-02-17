#include "spi_flash.h"
#include "freertos_app.h"



TaskHandle_t g_spi2_wait_task = NULL;


uint8_t spiflash_sector_buf[SPIF_SECTOR_SIZE];

static void spiflash_read_nolock(uint8_t *pbuffer, uint32_t read_addr, uint32_t length);
static void spiflash_sector_erase_nolock(uint32_t erase_addr);

void spiflash_write(uint8_t *pbuffer, uint32_t write_addr, uint32_t length)
{
  /* mutex protect whole write sequence */
  if (mutex_flash_handle)
  {
    xSemaphoreTake(mutex_flash_handle, portMAX_DELAY);
  }
  uint32_t sector_pos;
  uint16_t sector_offset;
  uint16_t sector_remain;
  uint16_t index;
  uint8_t *spiflash_buf;
  spiflash_buf = spiflash_sector_buf;

  /* sector address */
  sector_pos = write_addr / SPIF_SECTOR_SIZE;

  /* address offset in a sector */
  sector_offset = write_addr % SPIF_SECTOR_SIZE;

  /* the remain in a sector */
  sector_remain = SPIF_SECTOR_SIZE - sector_offset;
  if(length <= sector_remain)
  {
    /* smaller than a sector size */
    sector_remain = length;
  }
  while(1)
  {
    /* read a sector (nolock, we hold mutex here) */
    spiflash_read_nolock(spiflash_buf, sector_pos * SPIF_SECTOR_SIZE, SPIF_SECTOR_SIZE);

    /* validate the read erea */
    for(index = 0; index < sector_remain; index++)
    {
      if(spiflash_buf[sector_offset + index] != 0xFF)
      {
        /* there are some data not equal 0xff, so this secotr needs erased */
        break;
      }
    }
    if(index < sector_remain)
    {
      /* erase the sector (nolock, we hold mutex here) */
      spiflash_sector_erase_nolock(sector_pos);

      /* copy the write data */
      for(index = 0; index < sector_remain; index++)
      {
        spiflash_buf[index + sector_offset] = pbuffer[index];
      }
      spiflash_write_nocheck(spiflash_buf, sector_pos * SPIF_SECTOR_SIZE, SPIF_SECTOR_SIZE); /* program the sector */
    }
    else
    {
      /* write directly in the erased area */
      spiflash_write_nocheck(pbuffer, write_addr, sector_remain);
    }
    if(length == sector_remain)
    {
      /* write end */
      break;
    }
    else
    {
      /* go on writing */
      sector_pos++;
      sector_offset = 0;

      pbuffer += sector_remain;
      write_addr += sector_remain;
      length -= sector_remain;
      if(length > SPIF_SECTOR_SIZE)
      {
        /* could not write the remain data in the next sector */
        sector_remain = SPIF_SECTOR_SIZE;
      }
      else
      {
        /* could write the remain data in the next sector */
        sector_remain = length;
      }
    }
  }
  /* release mutex */
  if (mutex_flash_handle)
  {
    xSemaphoreGive(mutex_flash_handle);
  }
}

/**
  * @brief  read data from flash
  * @param  pbuffer: the pointer for data buffer
  * @param  read_addr: the address where the data is read
  * @param  length: buffer length
  * @retval none
  */
void spiflash_read(uint8_t *pbuffer, uint32_t read_addr, uint32_t length)
{
  /* mutex protect read */
  if (mutex_flash_handle)
  {
    xSemaphoreTake(mutex_flash_handle, portMAX_DELAY);
  }
  spiflash_read_nolock(pbuffer, read_addr, length);
  if (mutex_flash_handle)
  {
    xSemaphoreGive(mutex_flash_handle);
  }
}

static void spiflash_read_nolock(uint8_t *pbuffer, uint32_t read_addr, uint32_t length)
{
  FLASH_CS_LOW();
  spi_byte_write(SPIF_READDATA); /* send instruction */
  spi_byte_write((uint8_t)((read_addr) >> 16)); /* send 24-bit address */
  spi_byte_write((uint8_t)((read_addr) >> 8));
  spi_byte_write((uint8_t)read_addr);
  spi_bytes_read(pbuffer, length);
  FLASH_CS_HIGH();
}

/**
  * @brief  erase a sector data
  * @param  erase_addr: sector address to erase
  * @retval none
  */
void spiflash_sector_erase(uint32_t erase_addr)
{
  /* mutex protect erase */
  if (mutex_flash_handle)
  {
    xSemaphoreTake(mutex_flash_handle, portMAX_DELAY);
  }
  spiflash_sector_erase_nolock(erase_addr);
  if (mutex_flash_handle)
  {
    xSemaphoreGive(mutex_flash_handle);
  }
}

static void spiflash_sector_erase_nolock(uint32_t erase_addr)
{
  erase_addr *= SPIF_SECTOR_SIZE; /* translate sector address to byte address */
  spiflash_write_enable();
  spiflash_wait_busy();
  FLASH_CS_LOW();
  spi_byte_write(SPIF_SECTORERASE);
  spi_byte_write((uint8_t)((erase_addr) >> 16));
  spi_byte_write((uint8_t)((erase_addr) >> 8));
  spi_byte_write((uint8_t)erase_addr);
  FLASH_CS_HIGH();
  spiflash_wait_busy();
}

/**
  * @brief  write data without check
  * @param  pbuffer: the pointer for data buffer
  * @param  write_addr: the address where the data is written
  * @param  length: buffer length
  * @retval none
  */
void spiflash_write_nocheck(uint8_t *pbuffer, uint32_t write_addr, uint32_t length)
{
  uint16_t page_remain;

  /* remain bytes in a page */
  page_remain = SPIF_PAGE_SIZE - write_addr % SPIF_PAGE_SIZE;
  if(length <= page_remain)
  {
    /* smaller than a page size */
    page_remain = length;
  }
  while(1)
  {
    spiflash_page_write(pbuffer, write_addr, page_remain);
    if(length == page_remain)
    {
      /* all data are programmed */
      break;
    }
    else
    {
      /* length > page_remain */
      pbuffer += page_remain;
      write_addr += page_remain;

      /* the remain bytes to be prorammed */
      length -= page_remain;
      if(length > SPIF_PAGE_SIZE)
      {
        /* can be progrmmed a page at a time */
        page_remain = SPIF_PAGE_SIZE;
      }
      else
      {
        /* smaller than a page size */
        page_remain = length;
      }
    }
  }
}

/**
  * @brief  write a page data
  * @param  pbuffer: the pointer for data buffer
  * @param  write_addr: the address where the data is written
  * @param  length: buffer length
  * @retval none
  */
void spiflash_page_write(uint8_t *pbuffer, uint32_t write_addr, uint32_t length)
{
  if((0 < length) && (length <= SPIF_PAGE_SIZE))
  {
    /* set write enable */
    spiflash_write_enable();

    FLASH_CS_LOW();

    /* send instruction */
    spi_byte_write(SPIF_PAGEPROGRAM);

    /* send 24-bit address */
    spi_byte_write((uint8_t)((write_addr) >> 16));
    spi_byte_write((uint8_t)((write_addr) >> 8));
    spi_byte_write((uint8_t)write_addr);
    spi_bytes_write(pbuffer,length);

    FLASH_CS_HIGH();

    /* wait for program end */
    spiflash_wait_busy();
  }
}

/**
  * @brief  write data continuously
  * @param  pbuffer: the pointer for data buffer
  * @param  length: buffer length
  * @retval none
  */
void spi_bytes_write(uint8_t *pbuffer, uint32_t length)
{
  volatile uint8_t dummy_data;

  dma_init_type dma_init_struct;
  dma_reset(DMA2_CHANNEL2);
  dma_reset(DMA2_CHANNEL3);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.buffer_size = length;
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_base_addr = (uint32_t)&dummy_data;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = FALSE;
  dma_init_struct.peripheral_base_addr = (uint32_t)(&SPI2->dt);
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA2_CHANNEL2, &dma_init_struct);
	dma_flexible_config(DMA2, FLEX_CHANNEL2, DMA_FLEXIBLE_SPI2_RX);
 
  dma_init_struct.buffer_size = length;
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_base_addr = (uint32_t)pbuffer;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_base_addr = (uint32_t)(&SPI2->dt);
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA2_CHANNEL3, &dma_init_struct);
dma_flexible_config(DMA2, FLEX_CHANNEL3, DMA_FLEXIBLE_SPI2_TX);
  /* Enable RX-done interrupt only (DMA1 Channel1 FDT). TX FDT not required */
  dma_interrupt_enable(DMA2_CHANNEL2, DMA_FDT_INT, TRUE);

  /* remember waiting task (expect to be called from task03) */
  g_spi2_wait_task = xTaskGetCurrentTaskHandle();
  spi_i2s_dma_transmitter_enable(SPI2, TRUE);
  spi_i2s_dma_receiver_enable(SPI2, TRUE);

  dma_channel_enable(DMA2_CHANNEL2, TRUE);
  dma_channel_enable(DMA2_CHANNEL3, TRUE);
  /* wait for DMA done via task notification (no busy wait) */
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) == 0)
  {
    /* timeout: cleanup locally (ISR may not run) */
    dma_channel_enable(DMA2_CHANNEL2, FALSE);
    dma_channel_enable(DMA2_CHANNEL3, FALSE);
    spi_i2s_dma_transmitter_enable(SPI2, FALSE);
    spi_i2s_dma_receiver_enable(SPI2, FALSE);
    g_spi2_wait_task = NULL;
    printf("SPI2 DMA write timeout\r\n");
  }
}

/**
  * @brief  read data continuously
  * @param  pbuffer: buffer to save data
  * @param  length: buffer length
  * @retval none
  */
void spi_bytes_read(uint8_t *pbuffer, uint32_t length)
{
  uint8_t write_value = FLASH_SPI_DUMMY_BYTE;

  dma_init_type dma_init_struct;
  dma_reset(DMA2_CHANNEL2);
  dma_reset(DMA2_CHANNEL3);
  dma_default_para_init(&dma_init_struct);
  dma_init_struct.buffer_size = length;
  dma_init_struct.direction = DMA_DIR_MEMORY_TO_PERIPHERAL;
  dma_init_struct.memory_base_addr = (uint32_t)&write_value;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = FALSE;
  dma_init_struct.peripheral_base_addr = (uint32_t)(&SPI2->dt);
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA2_CHANNEL3, &dma_init_struct);
	 dma_flexible_config(DMA2, FLEX_CHANNEL2, DMA_FLEXIBLE_SPI2_RX);
  dma_init_struct.buffer_size = length;
  dma_init_struct.direction = DMA_DIR_PERIPHERAL_TO_MEMORY;
  dma_init_struct.memory_base_addr = (uint32_t)pbuffer;
  dma_init_struct.memory_data_width = DMA_MEMORY_DATA_WIDTH_BYTE;
  dma_init_struct.memory_inc_enable = TRUE;
  dma_init_struct.peripheral_base_addr = (uint32_t)(&SPI2->dt);
  dma_init_struct.peripheral_data_width = DMA_PERIPHERAL_DATA_WIDTH_BYTE;
  dma_init_struct.peripheral_inc_enable = FALSE;
  dma_init_struct.priority = DMA_PRIORITY_VERY_HIGH;
  dma_init_struct.loop_mode_enable = FALSE;
  dma_init(DMA2_CHANNEL2, &dma_init_struct);
  dma_flexible_config(DMA2, FLEX_CHANNEL3, DMA_FLEXIBLE_SPI2_TX);

  /* Enable RX-done interrupt only (DMA1 Channel1 FDT). TX FDT not required */
  dma_interrupt_enable(DMA2_CHANNEL2, DMA_FDT_INT, TRUE);

  /* remember waiting task */
  g_spi2_wait_task = xTaskGetCurrentTaskHandle();

  spi_i2s_dma_transmitter_enable(SPI2, TRUE);
  spi_i2s_dma_receiver_enable(SPI2, TRUE);
  dma_channel_enable(DMA2_CHANNEL2, TRUE);
  dma_channel_enable(DMA2_CHANNEL3, TRUE);

  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) == 0)
  {
    dma_channel_enable(DMA2_CHANNEL2, FALSE);
    dma_channel_enable(DMA2_CHANNEL3, FALSE);
    spi_i2s_dma_transmitter_enable(SPI2, FALSE);
    spi_i2s_dma_receiver_enable(SPI2, FALSE);
    g_spi2_wait_task = NULL;
    printf("SPI2 DMA read timeout\r\n");
  }
}


/**
  * @brief  wait WIP bit cleared with configurable timeout
  * @param  timeout_ms: 超时时间(毫秒); 0 表示无限等待（每秒打印一次状态）
  * @retval 1: ready; 0: timeout
  */
static uint8_t spiflash_wait_busy_timeout(uint32_t timeout_ms)
{
  TickType_t start = xTaskGetTickCount();
  TickType_t last_log = start;
  for (;;) {
    uint8_t sr = spiflash_read_sr1();
    if ((sr & 0x01) == 0) {
      return 1; /* not busy */
    }
    TickType_t now = xTaskGetTickCount();
    if (timeout_ms > 0 && (now - start) > pdMS_TO_TICKS(timeout_ms)) {
      printf("SPI Flash busy timeout(%lu ms), SR1=0x%02X\r\n", (unsigned long)timeout_ms, sr);
      return 0; /* timeout */
    }
    /* 无限等待模式下，每1秒打印一次便于观察长时间擦写 */
    if (timeout_ms == 0 && (now - last_log) >= pdMS_TO_TICKS(1000)) {
      printf("SPI Flash busy... SR1=0x%02X\r\n", sr);
      last_log = now;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

/**
  * @brief  wait program/erase done with a generous default timeout
  * @note   默认等待 60s；若需更长，请改用 spiflash_wait_busy_timeout(0) 无限等待
  */
void spiflash_wait_busy(void)
{
  /* 大规模格式化/擦写可能较久，给出 60s 足够空间，避免误判 */
  (void)spiflash_wait_busy_timeout(60000);
}

/**
  * @brief  read sr1 register
  * @param  none
  * @retval none
  */
uint8_t spiflash_read_sr1(void)
{
  uint8_t breadbyte = 0;
  FLASH_CS_LOW();
  spi_byte_write(SPIF_READSTATUSREG1);
  breadbyte = (uint8_t)spi_byte_read();
  FLASH_CS_HIGH();
  return (breadbyte);
}

/**
  * @brief  enable write operation
  * @param  none
  * @retval none
  */
void spiflash_write_enable(void)
{
  FLASH_CS_LOW();
  spi_byte_write(SPIF_WRITEENABLE);
  FLASH_CS_HIGH();
}

/**
  * @brief  read device id
  * @param  none
  * @retval device id
  */
uint16_t spiflash_read_id(void)
{
  uint16_t wreceivedata = 0;
  FLASH_CS_LOW();
  spi_byte_write(SPIF_MANUFACTDEVICEID);
  spi_byte_write(0x00);
  spi_byte_write(0x00);
  spi_byte_write(0x00);
  wreceivedata |= spi_byte_read() << 8;
  wreceivedata |= spi_byte_read();
  FLASH_CS_HIGH();
  return wreceivedata;
}

/**
  * @brief  write a byte to flash
  * @param  data: data to write
  * @retval flash return data
  */
uint8_t spi_byte_write(uint8_t data)
{
  uint8_t brxbuff;
  spi_i2s_dma_transmitter_enable(SPI2, FALSE);
  spi_i2s_dma_receiver_enable(SPI2, FALSE);
  spi_i2s_data_transmit(SPI2, data);
  while(spi_i2s_flag_get(SPI2, SPI_I2S_RDBF_FLAG) == RESET);
  brxbuff = spi_i2s_data_receive(SPI2);
  
  /* wait spi idle when communication end */
  while(spi_i2s_flag_get(SPI2, SPI_I2S_BF_FLAG) != RESET);
  
  return brxbuff;
}

/**
  * @brief  read a byte to flash
  * @param  none
  * @retval flash return data
  */
uint8_t spi_byte_read(void)
{
  return (spi_byte_write(FLASH_SPI_DUMMY_BYTE));
}

/*
 * Test: write 128 bytes of 0x05 at address 0x010000, then read back and verify.
 * return 0 on success, negative on failure.
 */
int spiflash_test_010000_write_read(void)
{
  const uint32_t addr = 0x010000U;
  uint8_t tx[128];
  uint8_t rx[128];

  /* prepare pattern */
  for (size_t i = 0; i < sizeof(tx); ++i) {
    tx[i] = 0x05;
  }

  /* write then read back */
  spiflash_write(tx, addr, (uint32_t)sizeof(tx));
  memset(rx, 0, sizeof(rx));
  spiflash_read(rx, addr, (uint32_t)sizeof(rx));

  /* verify */
  for (size_t i = 0; i < sizeof(rx); ++i) {
    if (rx[i] != 0x05) {
      printf("SPI Flash test FAIL at 0x%06lX + %lu: read 0x%02X, expect 0x05\r\n",
             (unsigned long)addr, (unsigned long)i, rx[i]);
      return -1;
    }
  }

  printf("SPI Flash test PASS: 128 bytes of 0x05 at 0x%06lX verified\r\n", (unsigned long)addr);
  return 0;
}



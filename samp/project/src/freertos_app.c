/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.c
  * Description        : Code for freertos applications
  */
/* add user code end Header */

/* Includes ------------------------------------------------------------------*/
#include "freertos_app.h"
#include "usb_app.h"

/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "bsp_io.h"
#include "bsp_adc.h"
#include "bsp_uart.h"
#include "bsp_screen.h"
#include "bsp_can_motor.h"
#include "bsp_qspi_flash.h"
#include "fal_cfg.h"
#include "fal.h"
#include "app_flashdb.h"
#include "bsp_wiegand.h"
#include "bsp_wdt.h"
#include "bsp_pvm.h"
#include "usb_core.h"
#include "cdc_class.h"
#include "app_config.h"
#include "app_sampling.h"
#include "app_screen.h"
#include "bsp_timer.h"
#include "app_modbus.h"
#include "app_adc_module.h"
#include "app_scheduler.h"
#include "app_4g_modem.h"
#include "app_mqtt.h"
#include "app_ota.h"
#include "app_retain_judge.h"
#include <string.h>
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

/* add user code end private typedef */

/* private define ------------------------------------------------------------*/
/* add user code begin private define */

/* 任务优先级定义（FreeRTOS: 数值越大优先级越高） */
#define PRIO_BELOW_NORMAL   2
#define PRIO_NORMAL         3
#define PRIO_ABOVE_NORMAL   4

/* 任务心跳事件位定义 */
#define TASK02_HB_BIT  (1 << 0)
#define TASK03_HB_BIT  (1 << 1)
#define TASK04_HB_BIT  (1 << 2)
#define TASK05_HB_BIT  (1 << 3)
#define TASK06_HB_BIT  (1 << 4)
#define TASK07_HB_BIT  (1 << 5)
#define TASK08_HB_BIT  (1 << 6)
#define ALL_HB_BITS    (0x7F)

/* add user code end private define */

/* private macro -------------------------------------------------------------*/
/* add user code begin private macro */

/* add user code end private macro */

/* private variables ---------------------------------------------------------*/
/* add user code begin private variables */

/* 硬件定时器全局计数器（TMR2 ISR 每秒++，TMR4 ISR 每毫秒++） */
volatile uint32_t g_tmr2_seconds = 0;
volatile uint32_t g_tmr4_milliseconds = 0;

/* add user code end private variables */

/* private function prototypes --------------------------------------------*/
/* add user code begin function prototypes */
static void handle_retain_and_drain(uint8_t bucket);
/* add user code end function prototypes */

/* private user code ---------------------------------------------------------*/
/* add user code begin 0 */

/* add user code end 0 */

/* task handler */
TaskHandle_t my_task01_handle;
TaskHandle_t my_task02_handle;
TaskHandle_t my_task03_handle;
TaskHandle_t my_task04_handle;
TaskHandle_t my_task05_handle;
TaskHandle_t my_task06_handle;
TaskHandle_t my_task07_handle;
TaskHandle_t my_task08_handle;

/* queue handler */
QueueHandle_t my_queue01_handle;
QueueHandle_t my_queue02_handle;
QueueHandle_t my_queue03_handle;
QueueHandle_t my_queue04_handle;
QueueHandle_t my_queue05_handle;
QueueHandle_t my_queue06_handle;
QueueHandle_t my_queue07_handle;
QueueHandle_t my_queue08_handle;
QueueHandle_t my_queue09_handle;

/* binary semaphore handler */
SemaphoreHandle_t my_binary_sem01_handle;
SemaphoreHandle_t my_binary_sem02_handle;
SemaphoreHandle_t my_binary_sem03_handle;
SemaphoreHandle_t my_binary_sem04_handle;
SemaphoreHandle_t my_binary_sem05_handle;
SemaphoreHandle_t my_binary_sem06_handle;

/* mutex handler */
SemaphoreHandle_t my_mutex01_handle;
SemaphoreHandle_t my_mutex02_handle;
SemaphoreHandle_t my_mutex03_handle;
SemaphoreHandle_t my_mutex04_handle;
SemaphoreHandle_t my_mutex05_handle;
SemaphoreHandle_t my_mutex06_handle;
SemaphoreHandle_t my_mutex07_handle;
SemaphoreHandle_t my_mutex08_handle;

/* event handler */
EventGroupHandle_t my_event01_handle;

/* Idle task control block and stack */
static StackType_t idle_task_stack[configMINIMAL_STACK_SIZE];

static StaticTask_t idle_task_tcb;

/* External Idle and Timer task static memory allocation functions */
extern void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize );

/*
  vApplicationGetIdleTaskMemory gets called when configSUPPORT_STATIC_ALLOCATION
  equals to 1 and is required for static memory allocation support.
*/
void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxIdleTaskTCBBuffer, StackType_t ** ppxIdleTaskStackBuffer, uint32_t *pulIdleTaskStackSize )
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
  /* create my_task01 task — USB (CDC Device + MSC Host OTA) */
  xTaskCreate(my_task01_func,
              "task01_usb",
              512,
              NULL,
              PRIO_NORMAL,
              &my_task01_handle);

  /* create my_task02 task — 采样/送样/留样主控状态机 */
  xTaskCreate(my_task02_func,
              "task02_samp",
              1024,
              NULL,
              PRIO_ABOVE_NORMAL,
              &my_task02_handle);

  /* create my_task03 task — 串口屏通信与命令处理 */
  xTaskCreate(my_task03_func,
              "task03_scr",
              512,
              NULL,
              PRIO_ABOVE_NORMAL,
              &my_task03_handle);

  /* create my_task04 task — 数采仪/Modbus通信 */
  xTaskCreate(my_task04_func,
              "task04_mbus",
              512,
              NULL,
              PRIO_NORMAL,
              &my_task04_handle);

  /* create my_task05 task — 4G模块通信 */
  xTaskCreate(my_task05_func,
              "task05_4g",
              512,
              NULL,
              PRIO_NORMAL,
              &my_task05_handle);

  /* create my_task06 task — CAN电机控制 + ADC监控 */
  xTaskCreate(my_task06_func,
              "task06_can",
              256,
              NULL,
              PRIO_ABOVE_NORMAL,
              &my_task06_handle);

  /* create my_task07 task — 系统管理 (WDT/KVDB刷写) */
  xTaskCreate(my_task07_func,
              "task07_sys",
              256,
              NULL,
              PRIO_BELOW_NORMAL,
              &my_task07_handle);

  /* create my_task08 task — 心跳/刷卡/备用 */
  xTaskCreate(my_task08_func,
              "task08_aux",
              256,
              NULL,
              PRIO_BELOW_NORMAL,
              &my_task08_handle);
}

/**
  * @brief  initializes all queue.
  * @param  none
  * @retval none
  */
void freertos_queue_create(void)
{
  /* Create the my_queue01, storing the returned handle in the xQueue variable. */
  my_queue01_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue02, storing the returned handle in the xQueue variable. */
  my_queue02_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue03, storing the returned handle in the xQueue variable. */
  my_queue03_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue04, storing the returned handle in the xQueue variable. */
  my_queue04_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue05, storing the returned handle in the xQueue variable. */
  my_queue05_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue06, storing the returned handle in the xQueue variable. */
  my_queue06_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue07, storing the returned handle in the xQueue variable. */
  my_queue07_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue08, storing the returned handle in the xQueue variable. */
  my_queue08_handle = xQueueCreate(16, sizeof(uint16_t));

  /* Create the my_queue09, storing the returned handle in the xQueue variable. */
  my_queue09_handle = xQueueCreate(16, sizeof(uint16_t));
}

/**
  * @brief  initializes all semaphore.
  * @param  none
  * @retval none
  */
void freertos_semaphore_create(void)
{
  /* Create the my_binary_sem01 */
  my_binary_sem01_handle = xSemaphoreCreateBinary();

  /* Create the my_binary_sem02 */
  my_binary_sem02_handle = xSemaphoreCreateBinary();

  /* Create the my_binary_sem03 */
  my_binary_sem03_handle = xSemaphoreCreateBinary();

  /* Create the my_binary_sem04 */
  my_binary_sem04_handle = xSemaphoreCreateBinary();

  /* Create the my_binary_sem05 */
  my_binary_sem05_handle = xSemaphoreCreateBinary();

  /* Create the my_binary_sem06 */
  my_binary_sem06_handle = xSemaphoreCreateBinary();

  /* Create the my_mutex01 */
  my_mutex01_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex02 */
  my_mutex02_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex03 */
  my_mutex03_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex04 */
  my_mutex04_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex05 */
  my_mutex05_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex06 */
  my_mutex06_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex07 */
  my_mutex07_handle = xSemaphoreCreateMutex();

  /* Create the my_mutex08 */
  my_mutex08_handle = xSemaphoreCreateMutex();
}

/**
  * @brief  initializes all event.
  * @param  none
  * @retval none
  */
void freertos_event_create(void)
{
  /* Create the my_event01 */
  my_event01_handle = xEventGroupCreate();
}

/**
  * @brief  freertos init and begin run.
  * @param  none
  * @retval none
  */
void wk_freertos_init(void)
{
  /* add user code begin freertos_init 0 */

  /* add user code end freertos_init 0 */

  /* enter critical */
  taskENTER_CRITICAL();

  freertos_semaphore_create();
  freertos_queue_create();
  freertos_event_create();
  freertos_task_create();
	
  /* add user code begin freertos_init 1 */

  /* add user code end freertos_init 1 */

  /* exit critical */
  taskEXIT_CRITICAL();

  /* start scheduler */
  vTaskStartScheduler();
}

/**
  * @brief my_task01 function.
  * @param  none
  * @retval none
  */
void my_task01_func(void *pvParameters)
{
  /* USB初始化 */
  wk_usb_app_init();

  /* 检查是否看门狗复位 */
  if (bsp_wdt_is_reset()) {
    printf("[Task01] 检测到看门狗复位\r\n");
  }

  for (;;)
  {
    /* USB CDC + MSC Host 轮询 */
    wk_usb_app_task();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}


/**
  * @brief my_task02 function.
  * @param  none
  * @retval none
  */
void my_task02_func(void *pvParameters)
{
  /* 上电初始化：加载配置 */
  cfg_init_load();
  scheduler_init((sched_mode_t)g_sampling_cfg.mode);
  vTaskDelay(pdMS_TO_TICKS(1000));  /* 等待外设就绪 */
  printf("[Task02] 采样主控任务启动\r\n");

  for (;;)
  {
    /* 1. 推进采样状态机 */
    sampling_step();

    /* 2. 推进送样状态机 */
    delivery_step();

    /* 3. 推进留样状态机 */
    retain_step();

    /* 4. 推进排水状态机 */
    drain_step();

    /* 5. 周期调度逻辑 */
    if (g_state.running) {
        scheduler_run();
    }

    /* 6. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK02_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms轮询周期 */
  }
}


/**
  * @brief my_task03 function.
  * @param  none
  * @retval none
  */
void my_task03_func(void *pvParameters)
{
  screen_task_init();
  vTaskDelay(pdMS_TO_TICKS(500));
  printf("[Task03] 串口屏通信任务启动\r\n");

  static uint32_t last_update_sec = 0;

  for (;;)
  {
    /* 1. 处理屏幕接收命令（从ISR环形缓冲区取出） */
    screen_poll_commands();

    /* 2. 周期刷新状态显示（每1秒） */
    if ((g_tmr2_seconds - last_update_sec) >= 1) {
      screen_update_status();
      last_update_sec = g_tmr2_seconds;
    }

    /* 3. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK03_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(20));  /* 20ms轮询 */
  }
}


/**
  * @brief my_task04 function.
  * @param  none
  * @retval none
  */
void my_task04_func(void *pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(1000));

  /* 初始化Modbus从站（默认大岳协议，地址1） */
  modbus_init(PROTO_DAYUE, 1);
  printf("[Task04] 数采仪/Modbus通信任务启动\r\n");

  static uint8_t rx_buf[UART_DMA_BUF_SIZE];
  static uint8_t tx_buf[UART_DMA_BUF_SIZE];

  for (;;)
  {
    /* 1. 同步系统状态到Modbus输入寄存器 */
    modbus_sync_status();

    /* 2. 轮询数采仪UART接收数据 */
    if (bsp_uart_rx_available(UART_PORT_COLLECTOR)) {
      uint16_t len = bsp_uart_get_rxdata(UART_PORT_COLLECTOR,
                                          rx_buf, sizeof(rx_buf));
      if (len > 0) {
        uint16_t resp_len = modbus_poll(rx_buf, len,
                                         tx_buf, sizeof(tx_buf));
        if (resp_len > 0) {
          bsp_uart_send(UART_PORT_COLLECTOR, tx_buf, resp_len);
        }
      }
    }

    /* 3. 留样判定通知处理 */
    {
        uint32_t notify_value = 0;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notify_value, 0) == pdTRUE) {
            if (notify_value == 0xFF) {
                drain_execute_blocking(0);
                drain_execute_blocking(1);
            } else {
                uint8_t bucket = (uint8_t)(notify_value - 1);
                /* 写入送样记录 */
                {
                    DeliveryLogData dlog;
                    dlog.trigger_source = 0;
                    dlog.water_source = (uint8_t)(bucket + 1);
                    dlog.delivery_volume = 0;
                    dlog.result = 1;
                    tsdb_event_append(EVT_DELIVERY_DONE, &dlog, sizeof(dlog));
                }
                handle_retain_and_drain(bucket);
            }
        }
    }

    /* 4. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK04_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}


/**
  * @brief my_task05 function.
  * @param  none
  * @retval none
  */
void my_task05_func(void *pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(3000));  /* 等待4G模块上电 */

  /* AD模块初始化 */
  adc_module_init();

  /* 4G模组初始化 */
  modem_init();

  /* MQTT初始化 */
  mqtt_init();

  /* OTA初始化 */
  ota_init();

  printf("[Task05] 4G/MQTT/OTA通信任务启动\r\n");

  for (;;)
  {
    /* 1. 4G模组状态机轮询 */
    modem_poll();

    /* 2. MQTT状态机轮询 */
    mqtt_poll();

    /* 3. AD模块数据接收(UART8) */
    adc_module_poll();

    /* 4. OTA状态机轮询 */
    ota_poll();

    /* 5. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK05_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}


/**
  * @brief my_task06 function.
  * @param  none
  * @retval none
  */
void my_task06_func(void *pvParameters)
{
  vTaskDelay(pdMS_TO_TICKS(500));
  printf("[Task06] CAN电机控制+ADC监控任务启动\r\n");

  for (;;)
  {
    /* 1. 处理CAN接收帧（电机状态回报） */
    {
      can_rx_frame_t frame;
      while (can_rx_get(&frame)) {
        /* CAN帧已在中断中入环形缓冲，这里取出处理 */
      }
    }

    /* 2. ADC电流监控：阀门电流异常检测 */
    /* 开阀后电流为0=断线，电流过大=堵转 */

    /* 3. 冰箱温度监控 NTC */

    /* 4. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK06_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}


/**
  * @brief my_task07 function.
  * @param  none
  * @retval none
  */
void my_task07_func(void *pvParameters)
{
  bsp_wdt_enable();
  bsp_pvm_init();
  vTaskDelay(pdMS_TO_TICKS(2000));

  uint32_t pvm_count = bsp_pvm_get_count();
  printf("[Task07] 系统管理任务启动 (WDT已启用, 历史掉电%u次)\r\n",
         (unsigned int)pvm_count);

  for (;;)
  {
    /* 1. 喂狗（检查所有任务心跳） */
    EventBits_t bits = xEventGroupWaitBits(
        my_event01_handle, ALL_HB_BITS, pdTRUE, pdTRUE,
        pdMS_TO_TICKS(5000));
    if ((bits & ALL_HB_BITS) == ALL_HB_BITS) {
      bsp_wdt_feed();
    } else {
      printf("[Task07] 心跳超时! bits=0x%02X\r\n", (unsigned int)bits);
    }

    /* 2. KVDB脏数据定时刷写（每30秒） */
    {
      static uint32_t last_flush_sec = 0;
      if ((g_tmr2_seconds - last_flush_sec) >= 30) {
        cfg_save_all();
        last_flush_sec = g_tmr2_seconds;
      }
    }

    /* 3. 掉电次数变化检测 */
    {
      uint32_t cur = bsp_pvm_get_count();
      if (cur != pvm_count) {
        printf("[Task07] 检测到掉电! 累计%u次\r\n", (unsigned int)cur);
        pvm_count = cur;
      }
    }

    /* 4. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK07_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}


/**
  * @brief my_task08 function.
  * @param  none
  * @retval none
  */
void my_task08_func(void *pvParameters)
{
  /* 门锁自动关闭时长(ms) */
  #define LOCK_OPEN_DURATION_MS  5000u
  /* 门锁状态 */
  static uint8_t lock_open = 0;
  static uint32_t lock_open_tick = 0;
  static uint32_t door_open_count = 0;
  static uint32_t door_deny_count = 0;

  wiegand_init();
  vTaskDelay(pdMS_TO_TICKS(500));
  printf("[Task08] 门锁/刷卡任务启动\r\n");

  for (;;)
  {
    /* 1. Wiegand刷卡检测 + 授权 + 开门 */
    {
      uint32_t card_id;
      if (wiegand_get_card_id(&card_id)) {
        uint8_t authorized = 0;
        uint8_t i;

        /* 白名单匹配 */
        for (i = 0; i < 10; i++) {
          if (g_system_setting_cfg.card_id[i] != 0 &&
              g_system_setting_cfg.card_id[i] == card_id)
          {
            authorized = 1;
            break;
          }
        }

        if (authorized) {
          /* 开门 */
          LOCK_ON();
          lock_open = 1;
          lock_open_tick = g_tmr4_milliseconds;
          door_open_count++;

          /* 记录开门事件 */
          tsdb_event_append(EVT_DOOR_OPEN, &card_id, sizeof(card_id));
          printf("[门锁] 授权开门 卡号=%08X 累计=%lu\r\n",
                 (unsigned)card_id, (unsigned long)door_open_count);
        } else {
          door_deny_count++;
          printf("[门锁] 拒绝 卡号=%08X 累计拒绝=%lu\r\n",
                 (unsigned)card_id, (unsigned long)door_deny_count);
        }
      }
    }

    /* 2. 门锁自动关闭 */
    if (lock_open &&
        (g_tmr4_milliseconds - lock_open_tick) >= LOCK_OPEN_DURATION_MS)
    {
      LOCK_OFF();
      lock_open = 0;
      tsdb_event_append(EVT_DOOR_CLOSE, NULL, 0);
      printf("[门锁] 自动关闭\r\n");
    }

    /* 3. 心跳上报 */
    xEventGroupSetBits(my_event01_handle, TASK08_HB_BIT);

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}


/* add user code begin 2 */

/* 送样完成通知Task04（由调度器调用） */
void notify_task4_delivery_complete(uint8_t bucket_id)
{
    uint32_t value = (bucket_id == 0xFF) ? 0xFF : (uint32_t)(bucket_id + 1);
    xTaskNotify(my_task04_handle, value, eSetValueWithOverwrite);
    printf("[调度器] 通知Task04: value=%lu\r\n", (unsigned long)value);
}

static void handle_retain_and_drain(uint8_t bucket)
{
    uint32_t window_start = g_tmr2_seconds;
    uint32_t delay_sec = 20 * 60;
    uint32_t window_sec = g_sampling_cfg.analysis_time_min * 60;
    uint8_t should_retain = 0;

    while (g_tmr2_seconds - window_start < window_sec) {
        if (g_tmr2_seconds - window_start >= delay_sec) {
            if (retain_judge_commit(bucket, g_tmr2_seconds)) {
                should_retain = 1;
                break;
            }
        }
        xEventGroupSetBits(my_event01_handle, TASK04_HB_BIT);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (should_retain) {
        retention_execute(bucket, g_tmr2_seconds);
    } else {
        drain_execute_blocking(bucket);
    }
    retain_judge_reset_state();
}

/* add user code end 2 */


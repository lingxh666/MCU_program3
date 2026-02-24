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
#include <string.h>
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */

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
  /* create my_task01 task */
  xTaskCreate(my_task01_func,
              "my_task01",
              128,
              NULL,
              0,
              &my_task01_handle);

  /* create my_task02 task */
  xTaskCreate(my_task02_func,
              "my_task02",
              512,
              NULL,
              0,
              &my_task02_handle);

  /* create my_task03 task */
  xTaskCreate(my_task03_func,
              "my_task03",
              128,
              NULL,
              0,
              &my_task03_handle);

  /* create my_task04 task */
  xTaskCreate(my_task04_func,
              "my_task04",
              128,
              NULL,
              0,
              &my_task04_handle);

  /* create my_task05 task */
  xTaskCreate(my_task05_func,
              "my_task05",
              128,
              NULL,
              0,
              &my_task05_handle);

  /* create my_task06 task */
  xTaskCreate(my_task06_func,
              "my_task06",
              128,
              NULL,
              0,
              &my_task06_handle);

  /* create my_task07 task */
  xTaskCreate(my_task07_func,
              "my_task07",
              128,
              NULL,
              0,
              &my_task07_handle);

  /* create my_task08 task */
  xTaskCreate(my_task08_func,
              "my_task08",
              128,
              NULL,
              0,
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
  /* add user code begin my_task01_func 0 */

  /* add user code end my_task01_func 0 */

  /* init usb app function. */
  wk_usb_app_init();

  /* add user code begin my_task01_func 2 */

  /* add user code end my_task01_func 2 */

  /* Infinite loop */
  while(1)
  {
    /* when use usb,the function wk_usb_app_task() will be generated,
       which is the usb application layer code that users can improve themselves */
    wk_usb_app_task();

  /* add user code begin my_task01_func 1 */

     vTaskDelay(1);

  /* add user code end my_task01_func 1 */
  }
}


/**
  * @brief my_task02 function.
  * @param  none
  * @retval none
  */
void my_task02_func(void *pvParameters)
{
  /* S11 电流通道测试 - 逐路开阀验证ADC电流与阀对应关系 */
  static const struct {
    adc1_ch_index_t adc_ch;
    relay_id_t      relay_id;
    const char     *name;
    uint8_t         is_motor;
  } map[] = {
    { ADC_CH_BOTTLE_MOTOR,   (relay_id_t)0,        "瓶排空电机", 1 },
    { ADC_CH_OUTLET_VALVE_A, RELAY_OUTLET_VALVE_A, "出水阀A",    0 },
    { ADC_CH_STIR_B,         RELAY_STIR_B,         "B搅拌",      0 },
    { ADC_CH_STIR_A,         RELAY_STIR_A,         "A搅拌",      0 },
    { ADC_CH_DRAIN_B,        RELAY_DRAIN_B,        "B排水",      0 },
    { ADC_CH_DRAIN_A,        RELAY_DRAIN_A,        "A排水",      0 },
    { ADC_CH_INLET_VALVE,    RELAY_INLET_VALVE,    "进水阀",     0 },
    { ADC_CH_INSTANT_VALVE,  RELAY_INSTANT_VALVE,  "瞬时阀",     0 },
    { ADC_CH_DELIVER_VALVE,  RELAY_DELIVER_VALVE,  "送留样阀",   0 },
  };
  static const adc1_ch_index_t all_ch[] = {
    ADC_CH_BOTTLE_MOTOR, ADC_CH_OUTLET_VALVE_A, ADC_CH_STIR_B,
    ADC_CH_STIR_A, ADC_CH_DRAIN_B, ADC_CH_DRAIN_A,
    ADC_CH_INLET_VALVE, ADC_CH_INSTANT_VALVE, ADC_CH_DELIVER_VALVE,
  };
  static const char *ch_tag[] = {
    "瓶电机", "出水A", "搅拌B", "搅拌A", "排水B", "排水A", "进水", "瞬时", "送留样"
  };
  const int ch_count = 9;
  const int map_count = sizeof(map) / sizeof(map[0]);

  static int round = 0;
  vTaskDelay(pdMS_TO_TICKS(3000));

  while(1) {
    printf("\r\n===== S11 电流通道测试 第%d轮 =====\r\n", ++round);

    /* 基线：全部关闭 */
    relay_all_off();
    bottle_motor_set(MOTOR_STOP);
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("[基线] 全部关闭:\r\n");
    for(int i = 0; i < ch_count; i++)
      printf("  %s: raw=%u  %.1f mA\r\n", ch_tag[i], adc1_get_raw(all_ch[i]), adc1_get_current_ma(all_ch[i]));

    /* 逐路测试 */
    for(int t = 0; t < map_count; t++) {
      printf("\r\n--- [%d/%d] %s ---\r\n", t + 1, map_count, map[t].name);

      if(map[t].is_motor)
        bottle_motor_set(MOTOR_EMPTY);
      else
        relay_set(map[t].relay_id, 1);

      vTaskDelay(pdMS_TO_TICKS(10000));

      for(int i = 0; i < ch_count; i++) {
        float ma = adc1_get_current_ma(all_ch[i]);
        const char *mark = (all_ch[i] == map[t].adc_ch) ? " <--目标" : "";
        printf("  %s: raw=%u  %.1f mA%s\r\n", ch_tag[i], adc1_get_raw(all_ch[i]), ma, mark);
      }

      if(map[t].is_motor)
        bottle_motor_set(MOTOR_STOP);
      else
        relay_set(map[t].relay_id, 0);

      vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("\r\n===== 第%d轮完成 =====\r\n", round);
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}


/**
  * @brief my_task03 function.
  * @param  none
  * @retval none
  */
void my_task03_func(void *pvParameters)
{
  /* add user code begin my_task03_func 0 */

  /* add user code end my_task03_func 0 */

  /* add user code begin my_task03_func 2 */

  /* add user code end my_task03_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task03_func 1 */

     vTaskDelay(1);

  /* add user code end my_task03_func 1 */
  }
}


/**
  * @brief my_task04 function.
  * @param  none
  * @retval none
  */
void my_task04_func(void *pvParameters)
{
  /* add user code begin my_task04_func 0 */

  /* add user code end my_task04_func 0 */

  /* add user code begin my_task04_func 2 */

  /* add user code end my_task04_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task04_func 1 */

     vTaskDelay(1);

  /* add user code end my_task04_func 1 */
  }
}


/**
  * @brief my_task05 function.
  * @param  none
  * @retval none
  */
void my_task05_func(void *pvParameters)
{
  /* add user code begin my_task05_func 0 */

  /* add user code end my_task05_func 0 */

  /* add user code begin my_task05_func 2 */

  /* add user code end my_task05_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task05_func 1 */

     vTaskDelay(1);

  /* add user code end my_task05_func 1 */
  }
}


/**
  * @brief my_task06 function.
  * @param  none
  * @retval none
  */
void my_task06_func(void *pvParameters)
{
  /* add user code begin my_task06_func 0 */

  /* add user code end my_task06_func 0 */

  /* add user code begin my_task06_func 2 */

  /* add user code end my_task06_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task06_func 1 */

     vTaskDelay(1);

  /* add user code end my_task06_func 1 */
  }
}


/**
  * @brief my_task07 function.
  * @param  none
  * @retval none
  */
void my_task07_func(void *pvParameters)
{
  /* add user code begin my_task07_func 0 */

  /* add user code end my_task07_func 0 */

  /* add user code begin my_task07_func 2 */

  /* add user code end my_task07_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task07_func 1 */

     vTaskDelay(1);

  /* add user code end my_task07_func 1 */
  }
}


/**
  * @brief my_task08 function.
  * @param  none
  * @retval none
  */
void my_task08_func(void *pvParameters)
{
  /* add user code begin my_task08_func 0 */

  /* add user code end my_task08_func 0 */

  /* add user code begin my_task08_func 2 */

  /* add user code end my_task08_func 2 */

  /* Infinite loop */
  while(1)
  {
  /* add user code begin my_task08_func 1 */

     vTaskDelay(1);

  /* add user code end my_task08_func 1 */
  }
}


/* add user code begin 2 */

/* add user code end 2 */


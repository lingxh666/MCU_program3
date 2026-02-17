/* add user code begin Header */
/**
  ******************************************************************************
  * File Name          : freertos_app.h
  * Description        : Code for freertos applications
  */
/* add user code end Header */
  
#ifndef FREERTOS_APP_H
#define FREERTOS_APP_H

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"
#include "event_groups.h"
#include "wk_system.h"

/* private includes -------------------------------------------------------------*/
/* add user code begin private includes */

/* add user code end private includes */

/* exported types -------------------------------------------------------------*/
/* add user code begin exported types */

/* add user code end exported types */

/* exported constants --------------------------------------------------------*/
/* add user code begin exported constants */

/* add user code end exported constants */

/* exported macro ------------------------------------------------------------*/
/* add user code begin exported macro */

/* add user code end exported macro */

/* task handler */
extern TaskHandle_t my_task01_handle;
extern TaskHandle_t my_task02_handle;
extern TaskHandle_t my_task03_handle;
extern TaskHandle_t my_task04_handle;
extern TaskHandle_t my_task05_handle;
extern TaskHandle_t my_task06_handle;
extern TaskHandle_t my_task07_handle;
extern TaskHandle_t my_task08_handle;
/* declaration for task function */
void my_task01_func(void *pvParameters);
void my_task02_func(void *pvParameters);
void my_task03_func(void *pvParameters);
void my_task04_func(void *pvParameters);
void my_task05_func(void *pvParameters);
void my_task06_func(void *pvParameters);
void my_task07_func(void *pvParameters);
void my_task08_func(void *pvParameters);

/* queue handler */
extern QueueHandle_t my_queue01_handle;
extern QueueHandle_t my_queue02_handle;
extern QueueHandle_t my_queue03_handle;
extern QueueHandle_t my_queue04_handle;
extern QueueHandle_t my_queue05_handle;
extern QueueHandle_t my_queue06_handle;
extern QueueHandle_t my_queue07_handle;
extern QueueHandle_t my_queue08_handle;
extern QueueHandle_t my_queue09_handle;

/* binary semaphore handler */
extern SemaphoreHandle_t my_binary_sem01_handle;
extern SemaphoreHandle_t my_binary_sem02_handle;
extern SemaphoreHandle_t my_binary_sem03_handle;
extern SemaphoreHandle_t my_binary_sem04_handle;
extern SemaphoreHandle_t my_binary_sem05_handle;
extern SemaphoreHandle_t my_binary_sem06_handle;

/* mutex handler */
extern SemaphoreHandle_t my_mutex01_handle;
extern SemaphoreHandle_t my_mutex02_handle;
extern SemaphoreHandle_t my_mutex03_handle;
extern SemaphoreHandle_t my_mutex04_handle;
extern SemaphoreHandle_t my_mutex05_handle;
extern SemaphoreHandle_t my_mutex06_handle;
extern SemaphoreHandle_t my_mutex07_handle;
extern SemaphoreHandle_t my_mutex08_handle;

/* event handler */
extern EventGroupHandle_t my_event01_handle;

/* add user code begin 0 */

/* add user code end 0 */

void freertos_task_create(void);
void freertos_queue_create(void);
void freertos_semaphore_create(void);
void freertos_event_create(void);
void wk_freertos_init(void);

/* add user code begin 1 */

/* add user code end 1 */

#endif /* FREERTOS_APP_H */

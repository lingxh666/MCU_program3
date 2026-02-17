/* add user code begin Header */
/**
  **************************************************************************
  * @file     at32f403a_407_int.c
  * @brief    main interrupt service routines.
  **************************************************************************
  *                       Copyright notice & Disclaimer
  *
  * The software Board Support Package (BSP) that is made available to
  * download from Artery official website is the copyrighted work of Artery.
  * Artery authorizes customers to use, copy, and distribute the BSP
  * software and its related documentation for the purpose of design and
  * development in conjunction with Artery microcontrollers. Use of the
  * software is governed by this copyright notice and the following disclaimer.
  *
  * THIS SOFTWARE IS PROVIDED ON "AS IS" BASIS WITHOUT WARRANTIES,
  * GUARANTEES OR REPRESENTATIONS OF ANY KIND. ARTERY EXPRESSLY DISCLAIMS,
  * TO THE FULLEST EXTENT PERMITTED BY LAW, ALL EXPRESS, IMPLIED OR
  * STATUTORY OR OTHER WARRANTIES, GUARANTEES OR REPRESENTATIONS,
  * INCLUDING BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY,
  * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT.
  *
  **************************************************************************
  */
/* add user code end Header */

/* includes ------------------------------------------------------------------*/
#include "at32f403a_407_int.h"
#include "wk_system.h"
#include "retain_judge.h"
/* private includes ----------------------------------------------------------*/
/* add user code begin private includes */
#include "freertos_app.h"
#include "screen.h"
#include "multi_button.h"
#include "wiegand.h"
#include "work.h"
/* add user code end private includes */

/* private typedef -----------------------------------------------------------*/
/* add user code begin private typedef */
//uint32_t OTAFileCRC1, OTAFileCRC2,testcount;  // OTA1.bin和OTA2.bin的CRC校验值
volatile uint32_t MqttCount;
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

/* external variables ---------------------------------------------------------*/
/* add user code begin external variables */

/* add user code end external variables */

/**
  * @brief  this function handles nmi exception.
  * @param  none
  * @retval none
  */
void NMI_Handler(void)
{
    /* add user code begin NonMaskableInt_IRQ 0 */

    /* add user code end NonMaskableInt_IRQ 0 */

    /* add user code begin NonMaskableInt_IRQ 1 */

    /* add user code end NonMaskableInt_IRQ 1 */
}

/**
  * @brief  this function handles hard fault exception.
  * @param  none
  * @retval none
  */
void HardFault_Handler(void)
{
    /* add user code begin HardFault_IRQ 0 */

    /* add user code end HardFault_IRQ 0 */
    /* go to infinite loop when hard fault exception occurs */
    while (1)
    {
        /* add user code begin W1_HardFault_IRQ 0 */

        /* add user code end W1_HardFault_IRQ 0 */
    }
}

/**
  * @brief  this function handles memory manage exception.
  * @param  none
  * @retval none
  */
void MemManage_Handler(void)
{
    /* add user code begin MemoryManagement_IRQ 0 */

    /* add user code end MemoryManagement_IRQ 0 */
    /* go to infinite loop when memory manage exception occurs */
    while (1)
    {
        /* add user code begin W1_MemoryManagement_IRQ 0 */

        /* add user code end W1_MemoryManagement_IRQ 0 */
    }
}

/**
  * @brief  this function handles bus fault exception.
  * @param  none
  * @retval none
  */
void BusFault_Handler(void)
{
    /* add user code begin BusFault_IRQ 0 */

    /* add user code end BusFault_IRQ 0 */
    /* go to infinite loop when bus fault exception occurs */
    while (1)
    {
        /* add user code begin W1_BusFault_IRQ 0 */

        /* add user code end W1_BusFault_IRQ 0 */
    }
}

/**
  * @brief  this function handles usage fault exception.
  * @param  none
  * @retval none
  */
void UsageFault_Handler(void)
{
    /* add user code begin UsageFault_IRQ 0 */

    /* add user code end UsageFault_IRQ 0 */
    /* go to infinite loop when usage fault exception occurs */
    while (1)
    {
        /* add user code begin W1_UsageFault_IRQ 0 */

        /* add user code end W1_UsageFault_IRQ 0 */
    }
}


/**
  * @brief  this function handles debug monitor exception.
  * @param  none
  * @retval none
  */
void DebugMon_Handler(void)
{
    /* add user code begin DebugMonitor_IRQ 0 */

    /* add user code end DebugMonitor_IRQ 0 */
    /* add user code begin DebugMonitor_IRQ 1 */

    /* add user code end DebugMonitor_IRQ 1 */
}

extern void xPortSysTickHandler(void);

/**
  * @brief  this function handles systick handler.
  * @param  none
  * @retval none
  */
void SysTick_Handler(void)
{
    /* add user code begin SysTick_IRQ 0 */

    /* add user code end SysTick_IRQ 0 */


#if (INCLUDE_xTaskGetSchedulerState == 1 )
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    {
#endif /* INCLUDE_xTaskGetSchedulerState */
        xPortSysTickHandler();
#if (INCLUDE_xTaskGetSchedulerState == 1 )
    }
#endif /* INCLUDE_xTaskGetSchedulerState */

    /* add user code begin SysTick_IRQ 1 */

    /* add user code end SysTick_IRQ 1 */
}

void EXINT2_IRQHandler(void)
{
    if (exint_flag_get(EXINT_LINE_2) != RESET) {
        exint_flag_clear(EXINT_LINE_2);
        // PD2 - 韦根D0线中断处理
        wiegand_d0_interrupt();
    }
}

void EXINT3_IRQHandler(void)
{
    if (exint_flag_get(EXINT_LINE_3) != RESET) {
        exint_flag_clear(EXINT_LINE_3);
        // PD3 - 韦根D1线中断处理
        wiegand_d1_interrupt();
    }
}


// ★ 开机排水超时检测标志（在freertos_app.c中定义）
extern volatile uint8_t g_drain_ab_executed;

// ★ 开机超时计数器（硬件中断级别，不受任务卡死影响）
static volatile uint32_t s_boot_timeout_counter = 0;
#define BOOT_DRAIN_TIMEOUT_SEC  60  // 开机60秒内必须执行AB桶排水

void TMR2_GLOBAL_IRQHandler(void)  //1s
{
//	BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if( tmr_flag_get( TMR2, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR2, TMR_OVF_FLAG );
        g_tmr2_seconds++; // 每秒递增
        Screenflag = 1;
        MqttCount++;
        if(MqttCount==120) {
            MqttCount=0;
            SendMqttFlag =1;
        }

        // 新增: 10分钟状态发送(600秒) —— 调试临时改为60秒
        MqttStatusCount++;
        if(MqttStatusCount >= 60) {
            MqttStatusCount = 0;
            SendMqttStatusFlag = 1;
        }

        // 新增: 1小时设置发送(3600秒)
        // 需求变更：暂停每小时设置上报及自动校时
        MqttSettingsCount++;
        if(MqttSettingsCount >= 3600) {
            MqttSettingsCount = 0;
            SendMqttSettingsFlag = 1;
        }

        // ★ 开机排水超时检测（硬件中断级别，不受任务卡死影响）
        if (!g_drain_ab_executed) {
            s_boot_timeout_counter++;
            if (s_boot_timeout_counter >= BOOT_DRAIN_TIMEOUT_SEC) {
                // 超时未执行AB桶排水，强制重启
                NVIC_SystemReset();
            }
        }

        // 已移除：电流值发送改为5秒周期+页面检测方式，不再使用定时器标志
        // g_retain_send_current_flag = 1;
    }
}

void TMR3_GLOBAL_IRQHandler(void) //1ms
{
    if( tmr_flag_get( TMR3, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR3, TMR_OVF_FLAG );
        g_tmr3_milliseconds++; // 每毫秒递增
        // 韦根超时检查(每毫秒检查一次)
        wiegand_timeout_check();
    }
}

void TMR4_GLOBAL_IRQHandler(void) //5s
{
    if( tmr_flag_get( TMR4, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR4, TMR_OVF_FLAG );
        wdt_counter_reload();
//		   	testcount++;

        // 30秒心跳计数（5s * 6 = 30s）
        static uint8_t heartbeat_count = 0;
        heartbeat_count++;
        if (heartbeat_count >= 6) {
            heartbeat_count = 0;
            // 通知task08更新心跳时间戳（通知值0x01表示心跳）
            extern TaskHandle_t task8_handle;
            if (task8_handle != NULL) {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
//                xTaskNotifyFromISR(task8_handle, 0x01, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);
                xTaskNotifyFromISR(task8_handle, 0x01, eSetBits, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
    }
}

void TMR5_GLOBAL_IRQHandler(void)
{
    if( tmr_flag_get( TMR5, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR5, TMR_OVF_FLAG );
        g_tmr4_seconds++;
        button_ticks();
    }
}


void TMR6_GLOBAL_IRQHandler(void)
{
    if( tmr_flag_get( TMR6, TMR_OVF_FLAG ) != RESET ) {
        tmr_flag_clear( TMR6, TMR_OVF_FLAG );
//				printf("tim6  中断\r\n");
    }
}

void TMR8_TRG_HALL_TMR14_IRQHandler(void)
{
    wk_timebase_handler();
}

void USART2_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(USART2, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(USART2, USART_IDLEF_FLAG);
        usart_data_receive(USART2);

        dma_channel_enable(DMA1_CHANNEL2, FALSE);
        message.len = sizeof(UART2_Buf) - dma_data_number_get(DMA1_CHANNEL2);
        // 限制复制长度，防止缓冲区溢出
        if (message.len > sizeof(message.data)) {
            message.len = sizeof(message.data);
        }
        memcpy(message.data, UART2_Buf, message.len);
//		    printf("usart2_rec_len=%d",message.len);
        xQueueSendFromISR(queue_analyser_handle, &message, &xHigherPriorityTaskWoken);
        dma_data_number_set(DMA1_CHANNEL2, sizeof(UART2_Buf));
        dma_channel_enable(DMA1_CHANNEL2, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void USART3_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(USART3, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(USART3, USART_IDLEF_FLAG);
        usart_data_receive(USART3);

        dma_channel_enable(DMA1_CHANNEL3, FALSE);
        message.len = sizeof(UART3_Buf) - dma_data_number_get(DMA1_CHANNEL3);
//        printf("接收UART3=%s", UART3_Buf);
        memcpy(message.data, UART3_Buf, message.len);
        xQueueSendFromISR(queue_usb_handle, &message, &xHigherPriorityTaskWoken);
        dma_data_number_set(DMA1_CHANNEL3, sizeof(UART3_Buf));
        dma_channel_enable(DMA1_CHANNEL3, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void UART4_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(UART4, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(UART4, USART_IDLEF_FLAG);
        usart_data_receive(UART4);

        dma_channel_enable(DMA1_CHANNEL4, FALSE);
        message.len = sizeof(UART4_Buf) - dma_data_number_get(DMA1_CHANNEL4);
//        printf("uart4_rec_len=%d\r\n",message.len);
        if(UART4_Buf[4] == 0x4F && UART4_Buf[5] == 0x4B) {
            /* 发送任务通知：与 screen_send_notify 协同 */
//			printf("%x %x %x %x %x %x\r\n",UART4_Buf[0],UART4_Buf[1],UART4_Buf[2],UART4_Buf[3],UART4_Buf[4],UART4_Buf[5]);
            screen_ack_notify_from_isr(&xHigherPriorityTaskWoken);
        }
        else {
            for(int i = 0; i < message.len; i++) {
//                printf(" 0x%02x   ", UART4_Buf[i]);
            }
            memcpy(message.data, UART4_Buf, message.len);
            xQueueSendFromISR(queue_screen_handle, &message, &xHigherPriorityTaskWoken);
//			printf(" screen send OK\r\n  ");
        }
        dma_data_number_set(DMA1_CHANNEL4, sizeof(UART4_Buf));
        dma_channel_enable(DMA1_CHANNEL4, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void UART5_IRQHandler(void)
{   BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(UART5, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(UART5, USART_IDLEF_FLAG);
        usart_data_receive(UART5);

        dma_channel_enable(DMA1_CHANNEL5, FALSE);
        message.len = sizeof(UART5_Buf) - dma_data_number_get(DMA1_CHANNEL5);
//        printf("uart5_rec_len=%d",message.len);
        memcpy(message.data, UART5_Buf, message.len);
        for(int i = 0; i < message.len; i++) {
//            printf("UART5接收数据[%d]=0x%02x\r\n", i, message.data[i]);

        }
        xQueueSendFromISR(queue_motor_handle, &message, &xHigherPriorityTaskWoken);
        dma_data_number_set(DMA1_CHANNEL5, sizeof(UART5_Buf));
        dma_channel_enable(DMA1_CHANNEL5, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void USART6_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t len;
    char patterm[64];
    char ota_pattern[64];
//    snprintf(pattern, sizeof(pattern), "ML307OTA_%s", g_CommSettingConfig.IDSET);//ML307OTA_CYJ_2512121H1001B
    snprintf(ota_pattern, sizeof(ota_pattern), "ML307OTA_%s", g_CommSettingConfig.IDSET);
    snprintf(patterm, sizeof(patterm), "SET_%s", g_CommSettingConfig.IDSET);


    if(usart_flag_get(USART6, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(USART6, USART_IDLEF_FLAG);
        usart_data_receive(USART6);

        dma_channel_enable(DMA1_CHANNEL6, FALSE);

        len = sizeof(UART6_Buf) - dma_data_number_get(DMA1_CHANNEL6);
        // 确保字符串终止，防止strstr越界读取
        if (len < sizeof(UART6_Buf)) {
            UART6_Buf[len] = '\0';
        } else {
            UART6_Buf[sizeof(UART6_Buf) - 1] = '\0';
        }
//        printf("接收UART6=%s", UART6_Buf);

        // 统计MQTT消息接收成功
        static uint32_t mqtt_msg_count = 0;
        if (strstr((char *)UART6_Buf, "+MQTTURC:") != NULL) {
            mqtt_msg_count++;
//            printf("[统计] MQTT消息计数: %lu\r\n", mqtt_msg_count);
        }

        // 检查是否包含特殊命令
        uint32_t notify_value = len; // 默认发送数据长度
        if (strstr((char *)UART6_Buf, ota_pattern) != NULL) {
            notify_value = 0x66;
        }
        // 锁机命令
        else if (strstr((char *)UART6_Buf, "_LOCK") != NULL) {
            notify_value = 0xAA;
        }
        // 解锁命令
        else if (strstr((char *)UART6_Buf, "_UNLOCK") != NULL) {
            notify_value = 0xAB;
        }
        // 转速调整命令
        else if (strstr((char *)UART6_Buf, "_SPD") != NULL) {
            notify_value = 0xAC;
        }
        // IP地址更改命令
        else if (strstr((char *)UART6_Buf, "_IP") != NULL) {
            notify_value = 0x99;
        }

        // 只发送一次通知，避免覆盖
        xTaskNotifyFromISR(task2_handle, notify_value, eSetValueWithOverwrite, &xHigherPriorityTaskWoken);

        dma_data_number_set(DMA1_CHANNEL6, sizeof(UART6_Buf));
        dma_channel_enable(DMA1_CHANNEL6, TRUE);

    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void UART7_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(UART7, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(UART7, USART_IDLEF_FLAG);
        usart_data_receive(UART7);
				printf("接收UART7=%x  %x", UART7_Buf[13],UART7_Buf[14]);
        dma_channel_enable(DMA1_CHANNEL7, FALSE);
        message.len = sizeof(UART7_Buf) - dma_data_number_get(DMA1_CHANNEL7);
        memcpy(message.data, UART7_Buf, message.len);
        xQueueSendFromISR(queue_moduleAI_handle, &message, &xHigherPriorityTaskWoken);
        dma_data_number_set(DMA1_CHANNEL7, sizeof(UART7_Buf));
        dma_channel_enable(DMA1_CHANNEL7, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void UART8_IRQHandler(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    UartMessage message;
    if(usart_flag_get(UART8, USART_IDLEF_FLAG) == SET) {
        usart_flag_clear(UART8, USART_IDLEF_FLAG);
        usart_data_receive(UART8);

        dma_channel_enable(DMA2_CHANNEL1, FALSE);
        message.len = sizeof(UART8_Buf) - dma_data_number_get(DMA2_CHANNEL1);
        memcpy(message.data, UART8_Buf, message.len);
//        printf("接收UART8=%s", UART8_Buf);
        xQueueSendFromISR(queue_UART8_handle, &message, &xHigherPriorityTaskWoken);
        dma_data_number_set(DMA2_CHANNEL1, sizeof(UART8_Buf));
        dma_channel_enable(DMA2_CHANNEL1, TRUE);
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void DMA2_Channel2_IRQHandler(void)
{
    extern TaskHandle_t g_spi2_wait_task; /* from spi_flash.c */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (dma_flag_get(DMA2_FDT2_FLAG) != RESET)
    {
        dma_flag_clear(DMA2_FDT2_FLAG);
        if (dma_flag_get(DMA2_FDT3_FLAG) != RESET)
        {
            dma_flag_clear(DMA2_FDT3_FLAG);
        }
        while(spi_i2s_flag_get(SPI2, SPI_I2S_BF_FLAG) != RESET);

        dma_channel_enable(DMA2_CHANNEL2, FALSE);
        dma_channel_enable(DMA2_CHANNEL3, FALSE);
        spi_i2s_dma_transmitter_enable(SPI2, FALSE);
        spi_i2s_dma_receiver_enable(SPI2, FALSE);

        if (g_spi2_wait_task)
        {
            vTaskNotifyGiveFromISR(g_spi2_wait_task, &xHigherPriorityTaskWoken);
            g_spi2_wait_task = NULL;
        }
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void screen_ack_notify_from_isr(BaseType_t *pxHigherPriorityTaskWoken);


#include "app_sample_id.h"
#include "bsp_rtc.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>

static struct {
    uint16_t           last_seq;
    uint32_t           last_timestamp;
    SemaphoreHandle_t  mutex;
} s_id_gen;

uint8_t sample_id_init(void)
{
    s_id_gen.mutex = xSemaphoreCreateMutex();
    if (s_id_gen.mutex == NULL)
        return 0;

    s_id_gen.last_seq       = 0;
    s_id_gen.last_timestamp = 0;
    return 1;
}

uint8_t sample_id_generate(char *out_id, size_t buf_size)
{
    if (out_id == NULL || buf_size < SAMPLE_ID_BUF_SIZE)
        return 0;

    if (xSemaphoreTake(s_id_gen.mutex, pdMS_TO_TICKS(10)) != pdTRUE)
        return 0;

    uint32_t now = rtc_get_timestamp();
    uint16_t seq;

    if (now == s_id_gen.last_timestamp)
    {
        s_id_gen.last_seq++;
        seq = s_id_gen.last_seq;
        if (seq >= 1000)
        {
            xSemaphoreGive(s_id_gen.mutex);
            return 0;
        }
    }
    else
    {
        s_id_gen.last_timestamp = now;
        s_id_gen.last_seq = 0;
        seq = 0;
    }

    rtc_datetime_t dt;
    rtc_get_time(&dt);

    snprintf(out_id, buf_size, "%04u%02u%02u%02u%02u%02u-%03u",
             dt.year, dt.month, dt.day,
             dt.hour, dt.min, dt.sec, seq);

    xSemaphoreGive(s_id_gen.mutex);
    return 1;
}

uint16_t sample_id_get_current_seq(void)
{
    return s_id_gen.last_seq;
}

void sample_id_reset(void)
{
    if (xSemaphoreTake(s_id_gen.mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        s_id_gen.last_seq       = 0;
        s_id_gen.last_timestamp = 0;
        xSemaphoreGive(s_id_gen.mutex);
    }
}

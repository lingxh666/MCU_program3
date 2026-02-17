#ifndef __SWITCHTRIGGER_H__
#define __SWITCHTRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ST_EVT_SWITCH_TRIGGER     0x01
#define ST_EVT_INIT               0x02
#define ST_EVT_START              0x03
#define ST_EVT_FIRST_TRIGGER      0x04
#define ST_EVT_WINDOW_START       0x05
#define ST_EVT_WINDOW_TRIGGERED   0x06
#define ST_EVT_WINDOW_NOT_TRIGGERED 0x07
#define ST_EVT_SAMPLE_EXECUTE     0x08
#define ST_EVT_SAMPLE_SKIP        0x09
#define ST_EVT_INSTANT_DELIVERY_START  0x0A
#define ST_EVT_INSTANT_DELIVERY_COMPLETE 0x0B
#define ST_EVT_FULL_SAMPLING_START  0x0C
#define ST_EVT_FULL_SAMPLING_COMPLETE 0x0D
#define ST_EVT_SAMPLING_STOP      0x0E
#define ST_EVT_SWITCH_RESUME      0x0F

typedef struct {
    uint8_t is_initialized;
    uint8_t is_running;

    uint8_t switch_signal_received;
    uint32_t switch_signal_time;
    uint8_t waiting_first_trigger;
    uint32_t last_notify_time;
    uint8_t last_signal_processed;
    uint32_t switch_trigger_count;

    uint8_t window_checking[24];
    uint32_t window_start_time[24];
    uint32_t window_end_time[24];
    uint8_t window_triggered[24];

    uint8_t first_trigger_done;
    uint32_t first_trigger_time;
    uint8_t first_delivery_mode;
    uint8_t first_delivery_hour;
    uint8_t first_delivery_done;

    uint8_t sample_count;
    uint16_t sample_offsets[24];
    uint32_t sample_done_mask;

    uint8_t cycle_started;
    uint8_t cycle_start_hour;
    uint32_t cycle_idx;
    uint8_t active_bucket;
    uint8_t configured_delivery_min;

    uint8_t sampling_stopped;
    uint8_t waiting_switch_resume;
    uint8_t delivery_done;

    uint32_t next_startup_sample_time;
    uint32_t startup_sample_interval_sec;

    uint32_t total_cycles;
    uint32_t total_samples;
    uint32_t total_deliveries;
} SwitchTriggerSchedulerState;

void st_scheduler_init(uint8_t is_power_recovery);
void st_scheduler_start(void);
void st_scheduler_stop(void);
void scheduler_switch_trigger(void);
void switch_trigger_notify_signal(uint32_t timestamp);
uint8_t st_scheduler_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __SWITCHTRIGGER_H__ */

#ifndef __TIMETRIGGER_H__
#define __TIMETRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FIXED_TIME_EVT_INIT               0x01
#define FIXED_TIME_EVT_START              0x02
#define FIXED_TIME_EVT_DELIVERY_LIST      0x03
#define FIXED_TIME_EVT_CYCLE_START        0x04
#define FIXED_TIME_EVT_SAMPLE_TRIGGER     0x05
#define FIXED_TIME_EVT_SAMPLE_COMPLETE    0x06
#define FIXED_TIME_EVT_DELIVERY_TRIGGER   0x07
#define FIXED_TIME_EVT_DELIVERY_COMPLETE  0x08
#define FIXED_TIME_EVT_CYCLE_SWITCH       0x09
#define FIXED_TIME_EVT_SAMPLE_SKIP        0x0A
#define FIXED_TIME_EVT_DELIVERY_SKIP      0x0B

typedef struct {
    uint8_t is_initialized;
    uint8_t is_running;

    uint8_t delivery_hours[24];
    uint8_t delivery_indices[24];
    uint8_t delivery_count;
    uint8_t delivery_min;

    uint16_t cycle_time;
    uint16_t sample_interval;
    uint8_t sample_count;
    uint16_t sample_offsets[24];

    uint8_t current_delivery_idx;
    uint8_t current_delivery_hour;
    uint8_t current_delivery_done;
    uint8_t active_bucket;

    uint32_t sample_done_mask;
    uint32_t total_cycles;
    uint32_t total_samples;
    uint32_t total_deliveries;

    uint8_t cycle_start_hour;
    uint8_t last_check_day;      // 日期检测：上次检查的日期（1-31）
} FixedTimeSchedulerState;

void fixed_time_scheduler_init(void);
void fixed_time_scheduler_start(void);
void fixed_time_scheduler_stop(void);
void scheduler_fixed_time(void);
uint8_t fixed_time_scheduler_is_running(void);
void fixed_time_scheduler_reinit_if_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __TIMETRIGGER_H__ */

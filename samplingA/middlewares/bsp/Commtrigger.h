#ifndef __COMMTRIGGER_H__
#define __COMMTRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
    COMM_REQ_NONE = 0,
    COMM_REQ_SAMPLING = 1,
    COMM_REQ_DRAIN = 2,
    COMM_REQ_DELIVERY = 3
} CommTriggerRequestType;

typedef struct {
    CommTriggerRequestType request_type;
    uint8_t bucket_selector;
    uint16_t volume;
    uint8_t pending;
} CommTriggerRequest;

typedef struct {
    uint8_t active_bucket;
    uint8_t is_initialized;
    uint32_t last_delivery_time;
    uint32_t cycle_count;
    uint32_t total_samples;
} CommTriggerSchedulerState;

extern CommTriggerRequest g_comm_trigger_request;
extern CommTriggerSchedulerState g_comm_scheduler_state;

void scheduler_comm_trigger(void);

#ifdef __cplusplus
}
#endif

#endif /* __COMMTRIGGER_H__ */

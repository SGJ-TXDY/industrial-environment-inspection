#ifndef INSPECTION_CAN_H
#define INSPECTION_CAN_H
#include "inspection_core.h"
typedef struct {
    uint32_t queued, completed, failed, result_failed, bus_off_count, kernel_hz;
    uint32_t accepted, backlog, dropped, retried, result_completed, session_failed;
    uint8_t initialized, bus_off, passive;
} inspection_can_stats_t;
int inspection_can_init(uint32_t now);
/* 0: queued in volatile RAM; -1: invalid result or queue full. No application ACK. */
int inspection_can_send_result(const inspection_result_t *result);
void inspection_can_service(uint32_t now,uint8_t state,uint32_t seq);
const inspection_can_stats_t *inspection_can_stats(void);
#endif

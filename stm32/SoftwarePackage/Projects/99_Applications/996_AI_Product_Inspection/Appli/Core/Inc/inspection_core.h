#ifndef INSPECTION_CORE_H
#define INSPECTION_CORE_H
#include <stdint.h>
#include "inspection_config.h"
typedef enum { INSPECT_EMPTY=0, INSPECT_NORMAL=1, INSPECT_DEFECT=2 } inspection_class_t;
typedef enum { INSPECT_WAIT_PRODUCT, INSPECT_TRACKING, INSPECT_GAP_PENDING } inspection_state_t;
typedef struct {
    float p[3];
    uint32_t capture_ms, frame_id, inference_ms;
} inspection_sample_t;
typedef struct {
    uint32_t seq;
    uint8_t defect, confidence, review;
} inspection_result_t;
typedef struct {
    inspection_state_t state;
    uint32_t seq, normal_count, defect_count, review_count;
    uint32_t last_ms, last_frame_id, gap_start_ms, frame_gap_count;
    float max_normal, max_defect;
    uint8_t seen_frame, defect_locked, normal_seen;
    inspection_result_t last_result;
} inspection_tracker_t;
void inspection_tracker_init(inspection_tracker_t *t);
/* 1: one finalized item, 0: no item, -1: invalid model output, -2: duplicate/stale frame. */
int inspection_tracker_update(inspection_tracker_t *t, const inspection_sample_t *s, inspection_result_t *out);
/* Finalize an in-flight item conservatively on a latched fault. */
int inspection_tracker_fault(inspection_tracker_t *t, inspection_result_t *out);
int inspection_sample_valid(const inspection_sample_t *s);
/* Set once per boot before sending; zero is reserved. Never change on CAN recovery. */
int inspection_protocol_set_session(uint8_t session);
uint8_t inspection_protocol_session(void);
void inspection_encode_result(uint8_t bytes[8], const inspection_result_t *r);
void inspection_encode_heartbeat(uint8_t bytes[8], uint8_t state, uint32_t seq);
/* Monotonic time relative to simulator start, one item/second, 14 occupied + 6 empty frames. */
void inspection_simulate(uint32_t elapsed_ms, uint32_t frame_id, inspection_sample_t *out);
#endif

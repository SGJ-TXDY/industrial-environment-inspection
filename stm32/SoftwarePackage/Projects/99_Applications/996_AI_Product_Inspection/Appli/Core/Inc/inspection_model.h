#ifndef INSPECTION_MODEL_H
#define INSPECTION_MODEL_H
#include "inspection_core.h"
int inspection_model_init(void);
int inspection_model_start(const uint8_t *rgb, uint32_t stride, uint32_t capture_ms, uint32_t frame_id);
/* 0 busy/idle, 1 completed, -1 fault; completed result retains its capture timestamp. */
int inspection_model_poll(inspection_sample_t *out);
#endif

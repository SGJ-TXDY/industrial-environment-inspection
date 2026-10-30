#ifndef INSPECTION_CAMERA_H
#define INSPECTION_CAMERA_H
#include <stdint.h>
typedef struct { const uint8_t *data; uint32_t capture_ms, ready_ms, frame_id, stride; int slot; } inspection_frame_t;
int inspection_camera_init(void);
void inspection_camera_service(void);
int inspection_camera_acquire(inspection_frame_t *out);
void inspection_camera_release(int slot);
uint32_t inspection_camera_last_frame(void);
uint32_t inspection_camera_drops(void);
uint32_t inspection_camera_frames(void);
int inspection_camera_failed(void);
#endif

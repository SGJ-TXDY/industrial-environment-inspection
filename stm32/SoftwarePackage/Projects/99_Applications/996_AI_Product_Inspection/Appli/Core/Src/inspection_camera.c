#include "inspection_camera.h"
#include "inspection_config.h"
#include "app_camera.h"
#include "app_lcd.h"
#include "cmw_camera.h"
#include <string.h>
enum { SLOT_FREE, SLOT_CAPTURE, SLOT_READY, SLOT_READING };
static uint8_t buffers[INSPECTION_BUFFER_COUNT][INSPECTION_CAPTURE_BYTES] __attribute__((aligned(32),section(".EXTRAM")));
static volatile uint8_t state[INSPECTION_BUFFER_COUNT];
static volatile uint32_t capture_ms[INSPECTION_BUFFER_COUNT], ready_ms[INSPECTION_BUFFER_COUNT], frame_id[INSPECTION_BUFFER_COUNT];
static volatile uint32_t last_frame, drops, frames, vsync_ms;
static volatile int capture_slot, failed;
static void vsync(void) { vsync_ms=HAL_GetTick(); }
static void completed(void) {
    unsigned k;int next=-1,old=capture_slot;
    last_frame=HAL_GetTick();++frames;
    for(k=0;k<INSPECTION_BUFFER_COUNT;++k)if(state[k]==SLOT_FREE){next=(int)k;break;}
    if(next<0){++drops;return;} /* Reuse only the DMA-owned slot; never touch a CPU-owned slot. */
    if(HAL_DCMIPP_PIPE_SetMemoryAddress(CMW_CAMERA_GetDCMIPPHandle(),DCMIPP_PIPE2,DCMIPP_MEMORY_ADDRESS_0,(uint32_t)(uintptr_t)buffers[next])!=HAL_OK){failed=1;return;}
    state[next]=SLOT_CAPTURE;capture_slot=next;
    capture_ms[old]=vsync_ms;ready_ms[old]=last_frame;frame_id[old]=frames;__DMB();state[old]=SLOT_READY;
}
int inspection_camera_init(void) {
    memset((void *)state,0,sizeof(state));capture_slot=0;failed=0;drops=frames=0;
    memset(buffers,0,sizeof(buffers));SCB_CleanInvalidateDCache_by_Addr(buffers,sizeof(buffers));
    if(app_camera_init(NULL,NULL,vsync,completed)!=0) return -1;
    state[0]=SLOT_CAPTURE;last_frame=vsync_ms=HAL_GetTick();
    if(CMW_CAMERA_Start(DCMIPP_PIPE1,app_lcd_get_bg_buffer(),CMW_MODE_CONTINUOUS)!=CMW_ERROR_NONE)return -1;
    if(CMW_CAMERA_Start(DCMIPP_PIPE2,buffers[0],CMW_MODE_CONTINUOUS)!=CMW_ERROR_NONE)return -1;
    return 0;
}
void inspection_camera_service(void) { if(CMW_CAMERA_Run()!=CMW_ERROR_NONE)failed=1; }
int inspection_camera_acquire(inspection_frame_t *out) {
    unsigned k;int selected=-1;uint32_t primask=__get_PRIMASK();__disable_irq();
    for(k=0;k<INSPECTION_BUFFER_COUNT;++k)if(state[k]==SLOT_READY && (selected<0 || (int32_t)(frame_id[k]-frame_id[selected])<0))selected=(int)k;
    if(selected>=0){state[selected]=SLOT_READING;out->data=buffers[selected];out->capture_ms=capture_ms[selected];out->ready_ms=ready_ms[selected];out->frame_id=frame_id[selected];out->stride=INSPECTION_INPUT_STRIDE;out->slot=selected;}
    __set_PRIMASK(primask);
    if(selected<0)return 0;
    SCB_InvalidateDCache_by_Addr((void *)out->data,INSPECTION_CAPTURE_BYTES);return 1;
}
void inspection_camera_release(int slot) {if(slot>=0 && slot<(int)INSPECTION_BUFFER_COUNT){__DMB();state[slot]=SLOT_FREE;}}
uint32_t inspection_camera_last_frame(void){return last_frame;}
uint32_t inspection_camera_drops(void){return drops;}
uint32_t inspection_camera_frames(void){return frames;}
int inspection_camera_failed(void){return failed;}

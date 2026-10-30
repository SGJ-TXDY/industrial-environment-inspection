#include "inspection_camera.h"
#include "inspection_config.h"
#include "stm32n6xx_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void (*vsync_cb)(void),(*frame_cb)(void);
static uint8_t *base,*dma;static uint32_t now;static int init_fail,run_fail;
uint32_t HAL_GetTick(void){return now;}
int app_camera_init(void (*a)(void),void (*b)(void),void (*c)(void),void (*d)(void)){(void)a;(void)b;vsync_cb=c;frame_cb=d;return init_fail?-1:0;}
uint8_t *app_lcd_get_bg_buffer(void){return NULL;}
void *CMW_CAMERA_GetDCMIPPHandle(void){return NULL;}
int CMW_CAMERA_Start(uint32_t pipe,uint8_t *dest,uint32_t mode){(void)mode;if(pipe==DCMIPP_PIPE2)base=dma=dest;return 0;}
int CMW_CAMERA_Run(void){return run_fail?-1:0;}
int HAL_DCMIPP_PIPE_SetMemoryAddress(void *h,uint32_t p,uint32_t m,uint32_t addr){
 uint32_t offset=addr-(uint32_t)(uintptr_t)base;(void)h;(void)p;(void)m;
 assert(offset<INSPECTION_BUFFER_COUNT*INSPECTION_CAPTURE_BYTES);dma=base+offset;return 0;
}
static void capture(uint32_t start,uint8_t content){now=start;vsync_cb();memset(dma,content,INSPECTION_CAPTURE_BYTES);now+=20;frame_cb();}
int main(void){
 inspection_frame_t a,b;assert(inspection_camera_init()==0);
 capture(0,1);capture(50,2);capture(100,3);
 assert(inspection_camera_frames()==3 && inspection_camera_drops()==1);
 assert(inspection_camera_acquire(&a)==1 && a.frame_id==1 && a.capture_ms==0 && a.ready_ms==20 && a.data[0]==1);
 capture(150,4);assert(a.data[0]==1 && inspection_camera_drops()==2);
 inspection_camera_release(a.slot);capture(200,5);
 assert(inspection_camera_acquire(&b)==1 && b.frame_id==2 && b.data[0]==2);
 inspection_camera_release(b.slot);
 assert(inspection_camera_acquire(&a)==1 && a.frame_id==5 && a.data[0]==5);
 capture(250,6);assert(a.data[0]==5);inspection_camera_release(a.slot);
 run_fail=1;inspection_camera_service();assert(inspection_camera_failed());
 init_fail=1;assert(inspection_camera_init()==-1);
 puts("PASS camera three-buffer ownership, overload drops, oldest-frame order, capture/ready timestamps and error propagation");return 0;
}

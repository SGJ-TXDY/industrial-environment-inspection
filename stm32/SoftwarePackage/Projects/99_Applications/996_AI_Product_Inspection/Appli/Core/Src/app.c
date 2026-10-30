#include "app.h"
#include "app_lcd.h"
#include "app_config.h"
#include "inspection_core.h"
#include "inspection_camera.h"
#include "inspection_model.h"
#include "inspection_can.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart1;
static inspection_tracker_t tracker;
static inspection_sample_t latest;
static uint8_t fault_state;
static uint32_t processed, fps, fps_count, fps_at, max_gap, last_sample_ms, max_latency, last_latency;
static char diagnostic[512];
static uint32_t run_start_ms;
static uint32_t latency_hist[251], latency_p95, infer_skips;
static uint32_t elapsed(uint32_t now,uint32_t before){return now-before;}
static uint8_t heartbeat_state(void){return fault_state?fault_state:(tracker.state==INSPECT_WAIT_PRODUCT?1U:0U);}
static void fault(uint8_t state){
    inspection_result_t r;
    if(!fault_state && inspection_tracker_fault(&tracker,&r)==1)(void)inspection_can_send_result(&r);
    if(!fault_state || state==2U)fault_state=state;
}
static void accept(const inspection_sample_t *sample,uint32_t now,uint32_t ready_ms){
    inspection_result_t result;int rc;
    if(!inspection_sample_valid(sample)){fault(3);return;}
    rc=inspection_tracker_update(&tracker,sample,&result);
    if(rc==-1){fault(3);return;}if(rc==-2)return;
    if(processed){uint32_t gap=sample->capture_ms-last_sample_ms;if(gap>max_gap)max_gap=gap;}
    last_sample_ms=sample->capture_ms;latest=*sample;++processed;++fps_count;
    last_latency=now-ready_ms;if(last_latency>max_latency)max_latency=last_latency;
    ++latency_hist[last_latency<250U?last_latency:250U];
    if(rc==1)(void)inspection_can_send_result(&result);
}
static const char *state_name(void){
    if(fault_state==2)return "CAMERA FAULT - RESTART REQUIRED";
    if(fault_state==3)return "MODEL FAULT - RESTART REQUIRED";
    if(tracker.state==INSPECT_GAP_PENDING)return "CONFIRMING EMPTY GAP";
    return tracker.state==INSPECT_TRACKING?"TRACKING PRODUCT":"WAITING FOR PRODUCT";
}
static void display(void){
    const inspection_can_stats_t *can=inspection_can_stats();const inspection_result_t *r=&tracker.last_result;
    app_lcd_draw_area_update();
    UTIL_LCD_FillRect(0,0,LCD_FG_WIDTH,LCD_FG_HEIGHT,0x00000000);
    UTIL_LCD_SetTextColor(INSPECTION_SIMULATION?UTIL_LCD_COLOR_YELLOW:UTIL_LCD_COLOR_WHITE);
    UTIL_LCDEx_PrintfAt(8,LINE(0),LEFT_MODE,INSPECTION_SIMULATION?"SIMULATION - SYNTHETIC INSPECTION RESULTS":"PRODUCT INSPECTION - LIVE MODEL");
    UTIL_LCDEx_PrintfAt(8,LINE(1),LEFT_MODE,"%s",state_name());
    UTIL_LCDEx_PrintfAt(8,LINE(3),LEFT_MODE,"EMPTY %.1f%%   NORMAL %.1f%%   DEFECT %.1f%%",latest.p[0]*100,latest.p[1]*100,latest.p[2]*100);
    UTIL_LCDEx_PrintfAt(8,LINE(5),LEFT_MODE,"SEQ %lu  NORMAL %lu  DEFECT %lu",(unsigned long)tracker.seq,(unsigned long)tracker.normal_count,(unsigned long)tracker.defect_count);
    UTIL_LCDEx_PrintfAt(8,LINE(6),LEFT_MODE,"REVIEW %lu (included in DEFECT)",(unsigned long)tracker.review_count);
    if(tracker.normal_count || tracker.defect_count){
        if(r->confidence==255)UTIL_LCDEx_PrintfAt(8,LINE(8),LEFT_MODE,"LAST: DEFECT / REVIEW - confidence UNKNOWN");
        else UTIL_LCDEx_PrintfAt(8,LINE(8),LEFT_MODE,"LAST: %s  %u%%",r->defect?"DEFECT":"NORMAL",r->confidence);
    }
    UTIL_LCDEx_PrintfAt(8,LINE(10),LEFT_MODE,"%s FPS %lu  NPU %lums  MAX GAP %lums",INSPECTION_SIMULATION?"SIM":"LIVE",(unsigned long)fps,(unsigned long)latest.inference_ms,(unsigned long)max_gap);
    UTIL_LCDEx_PrintfAt(8,LINE(11),LEFT_MODE,"CAM %lu DROP %lu SKIP %lu P95 %lums",(unsigned long)inspection_camera_frames(),(unsigned long)inspection_camera_drops(),(unsigned long)infer_skips,(unsigned long)latency_p95);
    UTIL_LCDEx_PrintfAt(8,LINE(13),LEFT_MODE,"CAN %s  TX DONE %lu FAIL %lu",!can->initialized?"INIT ERROR":can->bus_off?"BUS OFF":can->passive?"PASSIVE":"ACTIVE",(unsigned long)can->completed,(unsigned long)can->failed);
    UTIL_LCDEx_PrintfAt(8,LINE(14),LEFT_MODE,"500k classic CAN / 0x210 + 0x211  SID %u Q %lu LOST %lu",inspection_protocol_session(),(unsigned long)can->backlog,(unsigned long)can->dropped);
    app_lcd_draw_area_commit();
}
static void log_status(void){
    const inspection_can_stats_t *c=inspection_can_stats();int n;
    if(HAL_UART_GetState(&huart1)!=HAL_UART_STATE_READY)return;
    n=snprintf(diagnostic,sizeof(diagnostic),"%s seq=%lu normal=%lu defect=%lu review=%lu fps=%lu gap_max=%lu latency=%lu infer=%lu drop=%lu p95=%lu processed=%lu age_ms=%lu can_done=%lu can_fail=%lu state=%u session=%u backlog=%lu lost=%lu retry=%lu rng_fail=%lu\r\n",
        INSPECTION_SIMULATION?"SIMULATION":"PRODUCTION",(unsigned long)tracker.seq,(unsigned long)tracker.normal_count,(unsigned long)tracker.defect_count,(unsigned long)tracker.review_count,(unsigned long)fps,(unsigned long)max_gap,(unsigned long)last_latency,(unsigned long)latest.inference_ms,(unsigned long)inspection_camera_drops(),(unsigned long)latency_p95,(unsigned long)processed,(unsigned long)(HAL_GetTick()-run_start_ms),(unsigned long)c->completed,(unsigned long)c->failed,heartbeat_state(),inspection_protocol_session(),(unsigned long)c->backlog,(unsigned long)c->dropped,(unsigned long)c->retried,(unsigned long)c->session_failed);
    if(n>0)(void)HAL_UART_Transmit_IT(&huart1,(uint8_t *)diagnostic,(uint16_t)((unsigned)n<sizeof(diagnostic)?(unsigned)n:sizeof(diagnostic)-1));
}
void app_run(void){
    uint32_t start,sim_at,lcd_at,log_at,sim_frame=0;int camera_ok,model_busy=0;uint32_t model_ready_ms=0;
    inspection_frame_t frame;
    inspection_tracker_init(&tracker);app_lcd_init();
    (void)inspection_can_init(HAL_GetTick());
    camera_ok=inspection_camera_init()==0;
    if(!camera_ok)fault(2);
#if !INSPECTION_SIMULATION
    if(inspection_model_init()!=0)fault(3);
#endif
    start=HAL_GetTick();run_start_ms=start;sim_at=start-INSPECTION_PERIOD_MS;lcd_at=start-INSPECTION_LCD_PERIOD_MS;log_at=fps_at=start;
    /* USART1 is only used for nonblocking once-per-second diagnostics. */
    HAL_NVIC_SetPriority(USART1_IRQn,8,0);HAL_NVIC_EnableIRQ(USART1_IRQn);
    for(;;){
        uint32_t now=HAL_GetTick();
        if(camera_ok && fault_state!=2U){
            uint32_t camera_last;
            inspection_camera_service();
            camera_last=inspection_camera_last_frame();now=HAL_GetTick();
            if(inspection_camera_failed() || elapsed(now,camera_last)>INSPECTION_CAMERA_TIMEOUT_MS)fault(2);
        }
        /* Complete one runtime epoch per iteration, servicing CAN between epochs. */
#if !INSPECTION_SIMULATION
        if(model_busy && !fault_state){
            inspection_sample_t sample;int result=inspection_model_poll(&sample);
            if(result<0){model_busy=0;fault(3);}else if(result>0){model_busy=0;accept(&sample,HAL_GetTick(),model_ready_ms);}
        }
#endif
        if(camera_ok && inspection_camera_acquire(&frame)){
#if !INSPECTION_SIMULATION
            if(!fault_state && !model_busy){
                if(inspection_model_start(frame.data,frame.stride,frame.capture_ms,frame.frame_id)!=0)fault(3);
                else {model_busy=1;model_ready_ms=frame.ready_ms;}
            } else if(!fault_state)++infer_skips;
#endif
            /* The adapter copies input synchronously, so DMA can safely reuse this slot. */
            inspection_camera_release(frame.slot);
        }
#if INSPECTION_SIMULATION
        if(!fault_state && elapsed(now,sim_at)>=INSPECTION_PERIOD_MS){
            inspection_sample_t sample;sim_at=now;
            inspection_simulate(elapsed(now,start),++sim_frame,&sample);sample.capture_ms=now;accept(&sample,now,now);
        }
#else
        (void)start;(void)sim_at;(void)sim_frame;
#endif
        now=HAL_GetTick();inspection_can_service(now,heartbeat_state(),tracker.seq);
        if(elapsed(now,fps_at)>=1000U){
            uint32_t bucket,acc=0,target=(processed/100U)*95U+((processed%100U)*95U+99U)/100U;
            for(bucket=0;bucket<251U;++bucket){acc+=latency_hist[bucket];if(acc>=target){latency_p95=bucket;break;}}
            fps=(fps_count*1000U)/elapsed(now,fps_at);fps_count=0;fps_at=now;}
        if(elapsed(now,lcd_at)>=INSPECTION_LCD_PERIOD_MS){lcd_at=now;display();}
        if(elapsed(now,log_at)>=1000U){log_at=now;log_status();}
        (void)model_busy;(void)model_ready_ms;
    }
}

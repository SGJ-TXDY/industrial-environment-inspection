#include "inspection_model.h"
#include "stm32n6xx_hal.h"
#if INSPECTION_MODEL_AVAILABLE && !INSPECTION_SIMULATION
#include "ll_aton_runtime.h"
#include <math.h>
#include <string.h>
LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(Default);
static inspection_sample_t pending;
static uint32_t started;
static int busy;
#ifndef INSPECTION_OUTPUT_IS_LOGITS
#define INSPECTION_OUTPUT_IS_LOGITS 1
#endif
/* Actual model index -> semantic class, override these in generated-model config. */
#ifndef INSPECTION_MODEL_EMPTY_INDEX
#define INSPECTION_MODEL_EMPTY_INDEX 0
#endif
#ifndef INSPECTION_MODEL_NORMAL_INDEX
#define INSPECTION_MODEL_NORMAL_INDEX 1
#endif
#ifndef INSPECTION_MODEL_DEFECT_INDEX
#define INSPECTION_MODEL_DEFECT_INDEX 2
#endif
_Static_assert(INSPECTION_MODEL_EMPTY_INDEX>=0 && INSPECTION_MODEL_EMPTY_INDEX<3 && INSPECTION_MODEL_NORMAL_INDEX>=0 && INSPECTION_MODEL_NORMAL_INDEX<3 && INSPECTION_MODEL_DEFECT_INDEX>=0 && INSPECTION_MODEL_DEFECT_INDEX<3 && INSPECTION_MODEL_EMPTY_INDEX!=INSPECTION_MODEL_NORMAL_INDEX && INSPECTION_MODEL_EMPTY_INDEX!=INSPECTION_MODEL_DEFECT_INDEX && INSPECTION_MODEL_NORMAL_INDEX!=INSPECTION_MODEL_DEFECT_INDEX, "Model class indices must be a permutation of 0,1,2");
int inspection_model_init(void) {
    const LL_Buffer_InfoTypeDef *in=LL_ATON_Input_Buffers_Info_Default();
    const LL_Buffer_InfoTypeDef *out=LL_ATON_Output_Buffers_Info_Default();
    if(LL_Buffer_len(in)!=INSPECTION_INPUT_BYTES || LL_Buffer_len(out)!=3U*sizeof(float)) return -1;
    busy=0; return 0;
}
int inspection_model_start(const uint8_t *rgb,uint32_t stride,uint32_t capture_ms,uint32_t frame_id) {
    const LL_Buffer_InfoTypeDef *in=LL_ATON_Input_Buffers_Info_Default();
    uint8_t *dst=(uint8_t *)LL_Buffer_addr_start(in);
    uint32_t row;
    if(busy || !rgb || stride<INSPECTION_INPUT_WIDTH*3U) return -1;
    for(row=0;row<INSPECTION_INPUT_HEIGHT;++row)
        memcpy(dst+row*INSPECTION_INPUT_WIDTH*3U,rgb+row*stride,INSPECTION_INPUT_WIDTH*3U);
    SCB_CleanDCache_by_Addr((void *)((uintptr_t)dst&~31U),(int32_t)((INSPECTION_INPUT_BYTES+((uintptr_t)dst&31U)+31U)&~31U));
    pending.capture_ms=capture_ms;pending.frame_id=frame_id;started=HAL_GetTick();
    LL_ATON_RT_RuntimeInit();LL_ATON_RT_Init_Network(&NN_Instance_Default);busy=1;return 0;
}
int inspection_model_poll(inspection_sample_t *out) {
    LL_ATON_RT_RetValues_t ret;const LL_Buffer_InfoTypeDef *info;float *raw;float prob[3];unsigned k;
    if(!busy) return 0;
    if((uint32_t)(HAL_GetTick()-started)>INSPECTION_MODEL_TIMEOUT_MS) {
        /* The application latches MODEL FAULT and resets before another inference. */
        busy=0;return -1;
    }
    ret=LL_ATON_RT_RunEpochBlock(&NN_Instance_Default);
    if(ret!=LL_ATON_RT_DONE) return 0;
    info=LL_ATON_Output_Buffers_Info_Default();raw=(float *)LL_Buffer_addr_start(info);
    SCB_InvalidateDCache_by_Addr((void *)((uintptr_t)raw&~31U),(int32_t)((3U*sizeof(float)+((uintptr_t)raw&31U)+31U)&~31U));
    for(k=0;k<3;++k) { if(!isfinite(raw[k])) {busy=0;return -1;} prob[k]=raw[k]; }
#if INSPECTION_OUTPUT_IS_LOGITS
    { float m=fmaxf(prob[0],fmaxf(prob[1],prob[2])),sum=0;
      for(k=0;k<3;++k){prob[k]=expf(prob[k]-m);sum+=prob[k];}
      for(k=0;k<3;++k)prob[k]/=sum; }
#endif
    pending.p[0]=prob[INSPECTION_MODEL_EMPTY_INDEX];pending.p[1]=prob[INSPECTION_MODEL_NORMAL_INDEX];pending.p[2]=prob[INSPECTION_MODEL_DEFECT_INDEX];
    pending.inference_ms=HAL_GetTick()-started;
    LL_ATON_RT_DeInit_Network(&NN_Instance_Default);LL_ATON_RT_RuntimeDeInit();busy=0;
    *out=pending;return inspection_sample_valid(out)?1:-1;
}
#else
/* Production without a real inspection model is deliberately non-operational. */
int inspection_model_init(void) { return -1; }
int inspection_model_start(const uint8_t *rgb,uint32_t stride,uint32_t ms,uint32_t id) {(void)rgb;(void)stride;(void)ms;(void)id;return -1;}
int inspection_model_poll(inspection_sample_t *out) {(void)out;return -1;}
#endif

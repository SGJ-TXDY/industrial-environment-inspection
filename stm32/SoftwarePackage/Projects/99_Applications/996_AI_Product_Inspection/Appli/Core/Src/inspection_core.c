#include "inspection_core.h"
#include <math.h>
#include <string.h>
static uint8_t percent(float p) {
    unsigned n = (unsigned)(p * 100.0f + 0.5f);
    return (uint8_t)(n > 100U ? 100U : n);
}
int inspection_sample_valid(const inspection_sample_t *s) {
    float sum;
    unsigned i;
    if (!s) return 0;
    for (i=0; i<3; ++i) if (!isfinite(s->p[i]) || s->p[i]<0.0f || s->p[i]>1.0f) return 0;
    sum=s->p[0]+s->p[1]+s->p[2];
    return fabsf(sum-1.0f)<=0.02f;
}
void inspection_tracker_init(inspection_tracker_t *t) { memset(t,0,sizeof(*t)); }
static int finish(inspection_tracker_t *t, inspection_result_t *out, int forced) {
    inspection_result_t r;
    r.seq=++t->seq;
    r.review=(uint8_t)(forced || (!t->defect_locked && !t->normal_seen));
    r.defect=(uint8_t)(r.review || t->defect_locked);
    r.confidence=r.review ? INSPECTION_CONF_UNKNOWN : percent(r.defect ? t->max_defect : t->max_normal);
    if(r.defect) ++t->defect_count; else ++t->normal_count;
    if(r.review) ++t->review_count;
    t->last_result=r; if(out) *out=r;
    t->state=INSPECT_WAIT_PRODUCT;
    t->max_normal=t->max_defect=0.0f;
    t->defect_locked=t->normal_seen=0;
    return 1;
}
int inspection_tracker_update(inspection_tracker_t *t, const inspection_sample_t *s, inspection_result_t *out) {
    int discontinuity=0;
    if(!inspection_sample_valid(s)) return -1;
    if(t->seen_frame) {
        uint32_t df=s->frame_id-t->last_frame_id, dt=s->capture_ms-t->last_ms;
        if(df==0U || df>0x7fffffffU || dt>0x7fffffffU) return -2;
        if(dt>INSPECTION_MAX_FRAME_GAP_MS) {
            ++t->frame_gap_count; discontinuity=1;
            if(t->state==INSPECT_GAP_PENDING) t->state=INSPECT_TRACKING;
        }
    }
    t->seen_frame=1; t->last_frame_id=s->frame_id; t->last_ms=s->capture_ms;
    if(t->state==INSPECT_WAIT_PRODUCT) {
        if(s->p[INSPECT_EMPTY]>INSPECTION_PRESENCE_EMPTY_MAX) return 0;
        t->state=INSPECT_TRACKING;
    }
    if(s->p[INSPECT_NORMAL]>t->max_normal) t->max_normal=s->p[INSPECT_NORMAL];
    if(s->p[INSPECT_DEFECT]>t->max_defect) t->max_defect=s->p[INSPECT_DEFECT];
    if(s->p[INSPECT_NORMAL]>=INSPECTION_TRUST) t->normal_seen=1;
    if(s->p[INSPECT_DEFECT]>=INSPECTION_TRUST) t->defect_locked=1;
    if(s->p[INSPECT_EMPTY]>=INSPECTION_TRUST) {
        if(t->state!=INSPECT_GAP_PENDING || discontinuity) {
            t->state=INSPECT_GAP_PENDING; t->gap_start_ms=s->capture_ms;
        } else if((uint32_t)(s->capture_ms-t->gap_start_ms)>INSPECTION_END_GAP_MS) return finish(t,out,0);
    } else t->state=INSPECT_TRACKING;
    return 0;
}
int inspection_tracker_fault(inspection_tracker_t *t, inspection_result_t *out) {
    return t->state==INSPECT_WAIT_PRODUCT ? 0 : finish(t,out,1);
}
static uint8_t boot_session;
int inspection_protocol_set_session(uint8_t session) {
    if (!session) return -1;
    boot_session=session; return 0;
}
uint8_t inspection_protocol_session(void) { return boot_session; }
static void le32(uint8_t *b,uint32_t v) {
    b[0]=(uint8_t)v; b[1]=(uint8_t)(v>>8); b[2]=(uint8_t)(v>>16); b[3]=(uint8_t)(v>>24);
}
void inspection_encode_result(uint8_t b[8],const inspection_result_t *r) {
    memset(b,0,8); b[0]=INSPECTION_PROTOCOL_VERSION; b[1]=r->defect ? 1 : 0; b[2]=r->confidence; b[3]=boot_session; le32(b+4,r->seq);
}
void inspection_encode_heartbeat(uint8_t b[8],uint8_t state,uint32_t seq) {
    memset(b,0,8); b[0]=INSPECTION_PROTOCOL_VERSION; b[1]=state; b[2]=boot_session; b[3]=INSPECTION_SIMULATION ? INSPECTION_FLAG_SIMULATION : 0U; le32(b+4,seq);
}
void inspection_simulate(uint32_t elapsed_ms,uint32_t frame_id,inspection_sample_t *out) {
    uint32_t item=elapsed_ms/INSPECTION_CYCLE_MS+1U;
    uint32_t phase=elapsed_ms%INSPECTION_CYCLE_MS;
    memset(out,0,sizeof(*out)); out->capture_ms=elapsed_ms; out->frame_id=frame_id;
    out->p[0]=0.98f; out->p[1]=out->p[2]=0.01f;
    if(phase<INSPECTION_VISIBLE_MS) {
        out->p[0]=0.02f; out->p[1]=0.97f; out->p[2]=0.01f;
        if(item%10U==0U) { out->p[0]=0.20f; out->p[1]=0.60f; out->p[2]=0.20f; }
        else if(item%10U==9U && phase>=300U && phase<400U) {
            out->p[0]=0.02f; out->p[1]=0.03f; out->p[2]=0.95f;
        }
    }
}

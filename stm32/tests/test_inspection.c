#include "inspection_core.h"
#include "inspect.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static uint32_t frame;
static int feed(inspection_tracker_t *t,uint32_t ms,float e,float n,float d,inspection_result_t *r) {
    inspection_sample_t s={{e,n,d},ms,++frame,0};
    return inspection_tracker_update(t,&s,r);
}
static void end_item(inspection_tracker_t *t,uint32_t ms,inspection_result_t *r) {
    assert(feed(t,ms,.98f,.01f,.01f,r)==0);
    assert(feed(t,ms+50,.98f,.01f,.01f,r)==0);
    assert(feed(t,ms+100,.98f,.01f,.01f,r)==1);
}
static void boundaries(void) {
    inspection_tracker_t t; inspection_result_t r;
    inspection_tracker_init(&t); frame=0;
    assert(feed(&t,0,.02f,.97f,.01f,&r)==0);
    assert(feed(&t,50,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,99,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,100,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,101,.98f,.01f,.01f,&r)==1);
    assert(r.seq==1 && r.defect==0 && r.confidence==97);
    assert(feed(&t,151,.98f,.01f,.01f,&r)==0);
    puts("PASS strict 49/50/51ms boundary and no empty recount");
}
static void lock_and_uncertain(void) {
    inspection_tracker_t t; inspection_result_t r;
    inspection_tracker_init(&t);frame=0;
    feed(&t,0,.02f,.97f,.01f,&r); feed(&t,50,.02f,.03f,.95f,&r);feed(&t,100,.02f,.97f,.01f,&r);
    end_item(&t,150,&r); assert(r.defect && r.confidence==95 && !r.review);
    feed(&t,300,.2f,.6f,.2f,&r);end_item(&t,350,&r);
    assert(r.defect && r.confidence==255 && r.review && t.review_count==1);
    assert(t.normal_count==0 && t.defect_count==2);
    puts("PASS defect latch and conservative low-confidence decision");
}
static void interrupted_gap(void) {
    inspection_tracker_t t;inspection_result_t r;
    inspection_tracker_init(&t);frame=0;
    feed(&t,0,.02f,.97f,.01f,&r);feed(&t,50,.98f,.01f,.01f,&r);
    feed(&t,100,.6f,.3f,.1f,&r); /* uncertain empty must break the interval */
    assert(feed(&t,150,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,200,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,250,.98f,.01f,.01f,&r)==1);
    feed(&t,300,.02f,.97f,.01f,&r);feed(&t,350,.98f,.01f,.01f,&r);
    assert(feed(&t,450,.98f,.01f,.01f,&r)==0); /* missing frame */
    assert(t.frame_gap_count==1);
    assert(feed(&t,500,.98f,.01f,.01f,&r)==0);
    assert(feed(&t,550,.98f,.01f,.01f,&r)==1);
    puts("PASS uncertain/interrupted and missing-frame gap handling");
}
static void wrap_and_fault(void) {
    inspection_tracker_t t;inspection_result_t r;inspection_sample_t s;
    inspection_tracker_init(&t);frame=0;t.seq=0xffffffffU;
    feed(&t,0xffffff80U,.02f,.97f,.01f,&r);
    end_item(&t,0xffffffb2U,&r);
    assert(r.seq==0 && !r.defect);
    feed(&t,80,.02f,.97f,.01f,&r);
    assert(inspection_tracker_fault(&t,&r)==1 && r.review && r.confidence==255);
    assert(inspection_tracker_fault(&t,&r)==0);
    s=(inspection_sample_t){{NAN,0,1},100,++frame,0};
    assert(inspection_tracker_update(&t,&s,&r)==-1);
    s=(inspection_sample_t){{.02f,.97f,.01f},130,++frame,0};
    assert(inspection_tracker_update(&t,&s,&r)==0);
    assert(inspection_tracker_update(&t,&s,&r)==-2);
    puts("PASS timer/sequence wrap, one-shot fault closure, NaN and duplicate frame rejection");
}
static void protocol(void) {
    uint8_t b[8];const uint8_t n[8]={2,0,97,37,1,0,0,0},d[8]={2,1,88,37,2,0,0,0},h[8]={2,0,37,0,2,0,0,0};
    inspection_result_t r={1,0,97,0}; struct inspect_data rx={0};
    assert(inspection_protocol_set_session(0)==-1);
    assert(inspection_protocol_set_session(37)==0);
    inspection_encode_result(b,&r);assert(!memcmp(b,n,8));assert(inspect_parse(&rx,0x210,b,8,0)==1);
    assert(inspect_parse(&rx,0x210,b,8,.1)==0 && rx.duplicates==1);
    r.seq=2;r.defect=1;r.confidence=88;inspection_encode_result(b,&r);assert(!memcmp(b,d,8));
    assert(inspect_parse(&rx,0x210,b,8,.2)==1 && rx.defect_count==1);
    inspection_encode_heartbeat(b,0,2);assert(!memcmp(b,h,8));assert(inspect_parse(&rx,0x211,b,8,1)==1);
    assert(inspect_online(&rx,3.9) && !inspect_online(&rx,4.1));
    r.seq=4;inspection_encode_result(b,&r);inspect_parse(&rx,0x210,b,8,5);assert(rx.missed==1);
    r.seq=1;inspection_encode_result(b,&r);inspect_parse(&rx,0x210,b,8,6);assert(rx.restarts==1);
    /* Reboot with the exact same seq must be counted as a new item. */
    assert(inspection_protocol_set_session(42)==0);
    inspection_encode_result(b,&r);
    assert(inspect_parse(&rx,0x210,b,8,6.1)==1 && rx.session==42 && rx.restarts==2);
    assert(inspect_parse(&rx,0x210,b,8,6.2)==0);
    inspection_encode_heartbeat(b,1,1);
    assert(inspect_parse(&rx,0x211,b,8,6.3)==1 && rx.stm32_session==42);
    inspection_encode_result(b,&r); b[3]=0;
    assert(inspect_parse(&rx,0x210,b,8,6.4)==-1);
    inspection_encode_result(b,&r);
    b[1]=2;assert(inspect_parse(&rx,0x210,b,8,7)==-1);
    puts("PASS exact protocol examples, real IMX6ULL parser, duplicate/loss/restart/offline");
}
static void replay(void) {
    inspection_tracker_t t;inspection_sample_t s;inspection_result_t r;struct inspect_data rx={0};
    uint8_t b[8];uint32_t ms, count=0;
    inspection_tracker_init(&t);
    for(ms=0;ms<600000;ms+=50) {
        inspection_simulate(ms,++count,&s);
        if(inspection_tracker_update(&t,&s,&r)==1) {
            inspection_encode_result(b,&r);
            assert(inspect_parse(&rx,0x210,b,8,ms/1000.0)==1);
        }
        if(ms%1000==0) { inspection_encode_heartbeat(b,0,t.seq);assert(inspect_parse(&rx,0x211,b,8,ms/1000.0)==1); }
    }
    assert(t.seq==600 && t.normal_count==480 && t.defect_count==120 && t.review_count==60);
    assert(rx.normal_count==480 && rx.defect_count==120 && rx.seq==600);
    assert(rx.missed==0 && rx.duplicates==0 && rx.rejected==0 && t.frame_gap_count==0);
    puts("PASS 10-minute virtual replay: 12000 frames, 600 items, NORMAL=480 DEFECT=120 REVIEW=60, MISSED/DUP/BAD=0");
}
int main(void) {boundaries();lock_and_uncertain();interrupted_gap();wrap_and_fault();protocol();replay();puts("ALL TESTS PASSED");return 0;}

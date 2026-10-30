#include "main.h"
#include "inspection_can.h"
#include "inspect.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
MockFDCAN mock_can;
static uint32_t now,free_slots=8,mask,freq=200000000,clock_fail,bus_off,init_calls,stop_calls,rng_fail=1,rng_calls,tx_fail,status_fail;
static uint8_t bytes[8]; static FDCAN_TxHeaderTypeDef header;
static uint32_t sent_seq[1024],sent_id[1024],sent_count;
HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *h){assert(h->Instance==RNG);return HAL_OK;}
HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *h,uint32_t *v){(void)h;++rng_calls;*v=36;return rng_fail?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *h){(void)h;return HAL_OK;}
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c){(void)c;return clock_fail?HAL_ERROR:HAL_OK;}
void HAL_GPIO_Init(void *p,GPIO_InitTypeDef *c){(void)p;assert(c->Pin==(GPIO_PIN_11|GPIO_PIN_12) && c->Alternate==6);}
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t p){(void)p;return freq;}
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *h){++init_calls;assert(h->Init.NominalPrescaler==freq/10000000U);assert(h->Init.NominalTimeSeg1==16 && h->Init.NominalTimeSeg2==3);return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h,uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)h;(void)a;(void)b;(void)c;(void)d;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h){(void)h;mock_can.TXBTO=mock_can.TXBCF=0;free_slots=8;bus_off=0;mask=0;return HAL_OK;}
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h){(void)h;return free_slots;}
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,FDCAN_TxHeaderTypeDef *t,uint8_t *d){
 (void)h;if(tx_fail)return HAL_ERROR;
 mask=mask==0 || mask==128?1:mask<<1;
 header=*t;memcpy(bytes,d,8);mock_can.TXBTO&=~mask;mock_can.TXBCF&=~mask;
 assert(sent_count<1024);sent_id[sent_count]=t->Identifier;
 sent_seq[sent_count++]=(uint32_t)d[4]|(uint32_t)d[5]<<8|(uint32_t)d[6]<<16|(uint32_t)d[7]<<24;
 return HAL_OK;
}
uint32_t HAL_FDCAN_GetLatestTxFifoQRequestBuffer(const FDCAN_HandleTypeDef *h){(void)h;return mask;}
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(FDCAN_HandleTypeDef *h,FDCAN_ProtocolStatusTypeDef *s){(void)h;s->BusOff=bus_off;s->ErrorPassive=0;return status_fail?HAL_ERROR:HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h,uint32_t m){(void)h;(void)m;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h){assert(h->Instance!=NULL);++stop_calls;return HAL_OK;}
HAL_StatusTypeDef HAL_FDCAN_DeInit(FDCAN_HandleTypeDef *h){assert(h->Instance!=NULL);return HAL_OK;}
uint32_t HAL_GetTick(void){return now;}
static void service(uint32_t t){now=t;inspection_can_service(now,1,99);}
static void fresh(uint32_t t){now=t;sent_count=0;free_slots=8;tx_fail=status_fail=0;assert(inspection_can_init(t)==0);}
int main(void){
 inspection_result_t r={1,0,97,0}; const inspection_can_stats_t *s; unsigned k; struct inspect_data rx={0};
 assert(inspection_can_init(0)==-1);service(1000);assert(stop_calls==0 && rng_calls==2);
 assert(inspection_can_stats()->session_failed==2 && inspection_protocol_session()==0);
 rng_fail=0;service(2000);assert(inspection_protocol_session()==37 && inspection_can_stats()->initialized);
 puts("PASS RNG failure is explicit, retries safely, nonzero session survives CAN recovery");
 fresh(0);s=inspection_can_stats();
 assert(inspection_can_send_result(&r)==0 && s->backlog==1 && s->queued==0);
 service(0);assert(s->queued==1 && s->result_completed==0 && s->backlog==1);
 assert(header.Identifier==0x210 && header.FDFormat==FDCAN_CLASSIC_CAN && header.BitRateSwitch==FDCAN_BRS_OFF && header.DataLength==8);
 assert(bytes[0]==2 && bytes[2]==97 && bytes[3]==37 && bytes[4]==1);
 assert(inspect_parse(&rx,0x210,bytes,8,0)==1);
 mock_can.TXBTO|=mask;service(1);assert(s->result_completed==1 && s->backlog==0);
 service(1000);assert(header.Identifier==0x211 && bytes[2]==37 && bytes[3]==(INSPECTION_SIMULATION?1:0));
 puts("PASS v2 classic framing, session and simulation flag, completion distinct from enqueue");
 fresh(0);free_slots=0;assert(inspection_can_send_result(&r)==0);service(0);assert(s->backlog==1 && s->queued==0);
 free_slots=8;service(1);r.seq=2;assert(inspection_can_send_result(&r)==0);
 service(202);assert(!s->initialized && s->backlog==2 && s->result_failed==1);
 service(1201);assert(!s->initialized);service(1202);assert(s->initialized && s->retried==1);
 assert(sent_seq[0]==1 && sent_seq[1]==1 && s->backlog==2);
 /* Unknown-outcome retry arrives adjacent, receiver deduplicates it. */
 inspection_encode_result(bytes,&(inspection_result_t){1,0,97,0});
 assert(inspect_parse(&rx,0x210,bytes,8,1)==0 && rx.normal_count==1);
 mock_can.TXBTO=255;service(1203);assert(s->backlog==1 && sent_seq[sent_count-1]==2);
 mock_can.TXBTO=255;service(1204);assert(s->backlog==0 && s->result_completed==2);
 puts("PASS FIFO full retains results, no-ACK timeout retains head, ordered retry without double count");
 fresh(0);r.seq=3;inspection_can_send_result(&r);service(0);bus_off=1;service(1);
 assert(s->backlog==1 && s->bus_off);service(1001);assert(!s->bus_off && s->backlog==1 && inspection_protocol_session()==37);
 mock_can.TXBTO=255;service(1002);assert(s->result_completed==1);
 puts("PASS bus-off recovery retains queue and boot session");
 fresh(0);for(k=0;k<INSPECTION_RESULT_QUEUE_CAPACITY;++k){r.seq=k+1;assert(inspection_can_send_result(&r)==0);}
 assert(inspection_can_send_result(&r)==-1 && s->dropped==1 && s->backlog==128);
 for(k=0;k<128;++k){service(k);assert(sent_seq[k]==k+1);mock_can.TXBTO=255;}
 service(128);assert(s->result_completed==128 && s->backlog==0);
 puts("PASS bounded overflow drops newest, retained 128 results drain in order");
 fresh(0xfffffff0U);r.seq=1;inspection_can_send_result(&r);service(0xfffffff0U);service(0xb9U);
 assert(!s->initialized);service(0x4a1U);assert(s->initialized);
 puts("PASS timeout and retry across tick wrap");
 fresh(0);tx_fail=1;inspection_can_send_result(&r);service(0);assert(s->backlog==1 && s->failed==1);
 tx_fail=0;service(1);status_fail=1;service(2);assert(!s->initialized && s->backlog==1);
 status_fail=0;service(1002);mock_can.TXBTO=255;service(1003);assert(s->backlog==0);
 freq=123456789;assert(inspection_can_init(0)==-1);
 freq=200000000;clock_fail=1;assert(inspection_can_init(0)==-1);clock_fail=0;service(1000);assert(s->initialized);
 assert(inspection_can_send_result(NULL)==-1);r.confidence=101;assert(inspection_can_send_result(&r)==-1);
 puts("PASS HAL submission/status/init failures and invalid clock/result rejection");
 return 0;
}

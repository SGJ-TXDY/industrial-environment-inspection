#include "inspection_can.h"
#include "main.h"
#include <string.h>
static FDCAN_HandleTypeDef hcan;
static inspection_can_stats_t stats;
static uint32_t pending, result_pending, submitted[8], retry_at, heartbeat_at;
static inspection_result_t results[INSPECTION_RESULT_QUEUE_CAPACITY];
static unsigned head, count;
static uint8_t attempts;

/* RNG failure is explicit: do not fall back to a repeatable startup tick. */
static int begin_session(void) {
    RNG_HandleTypeDef rng={0}; uint32_t value=0; int ok;
    if (inspection_protocol_session()) return 0;
    __HAL_RCC_RNG_CLK_ENABLE();
    __HAL_RCC_RNG_FORCE_RESET(); __HAL_RCC_RNG_RELEASE_RESET();
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_RNG,RIF_ATTRIBUTE_SEC|RIF_ATTRIBUTE_PRIV);
    rng.Instance=RNG; rng.Init.ClockErrorDetection=RNG_CED_ENABLE;
    ok=HAL_RNG_Init(&rng)==HAL_OK;
    if(ok) ok=HAL_RNG_GenerateRandomNumber(&rng,&value)==HAL_OK;
    (void)HAL_RNG_DeInit(&rng);
    __HAL_RCC_RNG_CLK_DISABLE();
    if(!ok) { ++stats.session_failed; return -1; }
    return inspection_protocol_set_session((uint8_t)(value%255U+1U));
}
static int configure(void) {
    RCC_PeriphCLKInitTypeDef clk={0};GPIO_InitTypeDef gpio={0};uint32_t hz;
    clk.PeriphClockSelection=RCC_PERIPHCLK_FDCAN;clk.FdcanClockSelection=RCC_FDCANCLKSOURCE_PCLK1;
    if(HAL_RCCEx_PeriphCLKConfig(&clk)!=HAL_OK)return -1;
    __HAL_RCC_GPIOE_CLK_ENABLE();__HAL_RCC_FDCAN_CLK_ENABLE();
    /* Only FDCAN3 is used. Reset the shared block so the CCU divider is 1,
       including debugger restarts; the HAL CCU API only accepts FDCAN1. */
    __HAL_RCC_FDCAN_FORCE_RESET();__HAL_RCC_FDCAN_RELEASE_RESET();
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_FDCAN1,RIF_ATTRIBUTE_SEC|RIF_ATTRIBUTE_PRIV);
    HAL_GPIO_ConfigPinAttributes(GPIOE,GPIO_PIN_11|GPIO_PIN_12,GPIO_PIN_SEC|GPIO_PIN_PRIV);
    gpio.Pin=GPIO_PIN_11|GPIO_PIN_12;gpio.Mode=GPIO_MODE_AF_PP;gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;gpio.Alternate=GPIO_AF6_FDCAN3;HAL_GPIO_Init(GPIOE,&gpio);
    hz=HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);stats.kernel_hz=hz;
    if(hz==0 || hz%10000000U || hz/10000000U>512U)return -1;
    memset(&hcan,0,sizeof(hcan));hcan.Instance=FDCAN3;
    hcan.Init.FrameFormat=FDCAN_FRAME_CLASSIC;hcan.Init.Mode=FDCAN_MODE_NORMAL;
    hcan.Init.AutoRetransmission=ENABLE;hcan.Init.TransmitPause=DISABLE;hcan.Init.ProtocolException=DISABLE;
    hcan.Init.NominalPrescaler=hz/10000000U;hcan.Init.NominalTimeSeg1=16;hcan.Init.NominalTimeSeg2=3;hcan.Init.NominalSyncJumpWidth=3;
    hcan.Init.DataPrescaler=1;hcan.Init.DataTimeSeg1=1;hcan.Init.DataTimeSeg2=1;hcan.Init.DataSyncJumpWidth=1;
    hcan.Init.MessageRAMOffset=0;hcan.Init.RxFifo0ElmtSize=FDCAN_DATA_BYTES_8;hcan.Init.RxFifo1ElmtSize=FDCAN_DATA_BYTES_8;
    hcan.Init.RxBufferSize=FDCAN_DATA_BYTES_8;hcan.Init.TxFifoQueueElmtsNbr=8;hcan.Init.TxFifoQueueMode=FDCAN_TX_FIFO_OPERATION;hcan.Init.TxElmtSize=FDCAN_DATA_BYTES_8;
    if(HAL_FDCAN_Init(&hcan)!=HAL_OK)return -1;
    if(HAL_FDCAN_ConfigGlobalFilter(&hcan,FDCAN_REJECT,FDCAN_REJECT,FDCAN_REJECT_REMOTE,FDCAN_REJECT_REMOTE)!=HAL_OK)return -1;
    if(HAL_FDCAN_Start(&hcan)!=HAL_OK)return -1;
    stats.initialized=1;stats.bus_off=stats.passive=0;pending=result_pending=0;return 0;
}
int inspection_can_init(uint32_t now) {
    memset(&stats,0,sizeof(stats)); pending=result_pending=0;
    head=count=0; attempts=0; retry_at=heartbeat_at=now;
    if(begin_session()!=0 || configure()!=0) { stats.initialized=0; return -1; }
    return 0;
}
/* Return success means retained in RAM, not delivered to Linux. Drop NEWEST
 * on overflow so the retained prefix stays in sequence order. */
int inspection_can_send_result(const inspection_result_t *r) {
    if(!r || r->defect>1U || (r->confidence>100U && r->confidence!=255U)) return -1;
    if(count==INSPECTION_RESULT_QUEUE_CAPACITY) { ++stats.dropped; return -1; }
    results[(head+count)%INSPECTION_RESULT_QUEUE_CAPACITY]=*r;
    ++count; stats.backlog=count; ++stats.accepted;
    return 0;
}
static int send8(uint32_t id,uint8_t data[8],uint32_t now) {
    FDCAN_TxHeaderTypeDef h={0}; uint32_t mask; unsigned k;
    if(!stats.initialized || stats.bus_off || HAL_FDCAN_GetTxFifoFreeLevel(&hcan)==0) return -1;
    h.Identifier=id; h.IdType=FDCAN_STANDARD_ID; h.TxFrameType=FDCAN_DATA_FRAME; h.DataLength=FDCAN_DLC_BYTES_8;
    h.ErrorStateIndicator=FDCAN_ESI_ACTIVE; h.BitRateSwitch=FDCAN_BRS_OFF; h.FDFormat=FDCAN_CLASSIC_CAN; h.TxEventFifoControl=FDCAN_NO_TX_EVENTS;
    if(HAL_FDCAN_AddMessageToTxFifoQ(&hcan,&h,data)!=HAL_OK) { ++stats.failed; return -1; }
    mask=HAL_FDCAN_GetLatestTxFifoQRequestBuffer(&hcan);
    for(k=0;k<8;++k) if(mask&(1UL<<k)) { submitted[k]=now; break; }
    pending|=mask;
    if(id==INSPECTION_RESULT_ID) { result_pending|=mask; if(attempts) ++stats.retried; attempts=1; }
    ++stats.queued; return 0;
}
static void complete(uint32_t bits) {
    unsigned k;
    for(k=0;k<8;++k) if((pending&bits)&(1UL<<k)) {
        uint32_t bit=1UL<<k;
        ++stats.completed;
        if(result_pending&bit) {
            head=(head+1U)%INSPECTION_RESULT_QUEUE_CAPACITY; --count;
            stats.backlog=count; ++stats.result_completed; attempts=0;
        }
        pending&=~bit; result_pending&=~bit;
    }
}
static void fail_pending(void) {
    unsigned k;
    for(k=0;k<8;++k) if(pending&(1UL<<k)) {
        ++stats.failed; if(result_pending&(1UL<<k)) ++stats.result_failed;
    }
    pending=result_pending=0;
}
void inspection_can_service(uint32_t now,uint8_t state,uint32_t seq) {
    unsigned k; FDCAN_ProtocolStatusTypeDef ps={0}; int reset=0;
    if(stats.initialized) {
        /* Check completion before timeout/bus-off, retaining only uncertain
         * results. One result in flight prevents out-of-order replay. */
        complete(hcan.Instance->TXBTO);
        if(HAL_FDCAN_GetProtocolStatus(&hcan,&ps)!=HAL_OK) reset=1;
        else {
            stats.passive=(uint8_t)ps.ErrorPassive;
            if(ps.BusOff) { stats.bus_off=1; ++stats.bus_off_count; reset=1; }
            for(k=0;k<8;++k) if(pending&(1UL<<k)) {
                uint32_t bit=1UL<<k;
                if((hcan.Instance->TXBCF&bit) || (uint32_t)(now-submitted[k])>INSPECTION_TX_TIMEOUT_MS) reset=1;
            }
        }
        if(reset) {
            /* Stop before forgetting masks: an asynchronous abort alone can
             * leave an old frame transmitting after a newer result. */
            (void)HAL_FDCAN_Stop(&hcan);
            complete(hcan.Instance->TXBTO);
            fail_pending(); stats.initialized=0; retry_at=now;
        }
    }
    if(!stats.initialized && (uint32_t)(now-retry_at)>=INSPECTION_CAN_RETRY_MS) {
        retry_at=now;
        if(hcan.Instance!=NULL) { (void)HAL_FDCAN_Stop(&hcan); (void)HAL_FDCAN_DeInit(&hcan); }
        if(begin_session()==0 && configure()==0) stats.bus_off=0;
    }
    if(!stats.initialized) return;
    if(count && !result_pending) {
        uint8_t b[8]; inspection_encode_result(b,&results[head]); (void)send8(INSPECTION_RESULT_ID,b,now);
    }
    if((uint32_t)(now-heartbeat_at)>=INSPECTION_HEARTBEAT_MS) {
        uint8_t b[8]; inspection_encode_heartbeat(b,state,seq);
        if(send8(INSPECTION_HEARTBEAT_ID,b,now)==0) heartbeat_at=now;
    }
}
const inspection_can_stats_t *inspection_can_stats(void) { return &stats; }

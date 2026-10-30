#ifndef MOCK_MAIN_H
#define MOCK_MAIN_H
#define RIF_RISC_PERIPH_INDEX_RNG 12
#define RNG_CED_ENABLE 0
#define RNG ((void *)2)
#define __HAL_RCC_RNG_CLK_ENABLE() ((void)0)
#define __HAL_RCC_RNG_CLK_DISABLE() ((void)0)
#define __HAL_RCC_RNG_FORCE_RESET() ((void)0)
#define __HAL_RCC_RNG_RELEASE_RESET() ((void)0)
#include <stdint.h>
#include <stddef.h>
#include "inspection_config.h"
typedef struct { void *Instance; struct {uint32_t ClockErrorDetection;} Init; } RNG_HandleTypeDef;
typedef int HAL_StatusTypeDef;
HAL_StatusTypeDef HAL_RNG_Init(RNG_HandleTypeDef *h);
HAL_StatusTypeDef HAL_RNG_GenerateRandomNumber(RNG_HandleTypeDef *h,uint32_t *v);
HAL_StatusTypeDef HAL_RNG_DeInit(RNG_HandleTypeDef *h);
#define HAL_OK 0
#define HAL_ERROR 1
#define ENABLE 1
#define DISABLE 0
#define RCC_PERIPHCLK_FDCAN 1
#define RCC_FDCANCLKSOURCE_PCLK1 1
#define RIF_RISC_PERIPH_INDEX_FDCAN1 26
#define RIF_ATTRIBUTE_SEC 1
#define RIF_ATTRIBUTE_PRIV 2
#define GPIO_PIN_11 (1U<<11)
#define GPIO_PIN_12 (1U<<12)
#define GPIO_PIN_SEC 1
#define GPIO_PIN_PRIV 2
#define GPIO_MODE_AF_PP 2
#define GPIO_NOPULL 0
#define GPIO_SPEED_FREQ_VERY_HIGH 3
#define GPIO_AF6_FDCAN3 6
#define GPIOE ((void *)1)
#define FDCAN_FRAME_CLASSIC 0
#define FDCAN_MODE_NORMAL 0
#define FDCAN_DATA_BYTES_8 0
#define FDCAN_TX_FIFO_OPERATION 0
#define FDCAN_REJECT 2
#define FDCAN_REJECT_REMOTE 1
#define FDCAN_STANDARD_ID 0
#define FDCAN_DATA_FRAME 0
#define FDCAN_DLC_BYTES_8 8
#define FDCAN_ESI_ACTIVE 0
#define FDCAN_BRS_OFF 0
#define FDCAN_CLASSIC_CAN 0
#define FDCAN_NO_TX_EVENTS 0
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_FDCAN_CLK_ENABLE() ((void)0)
#define __HAL_RCC_FDCAN_FORCE_RESET() ((void)0)
#define __HAL_RCC_FDCAN_RELEASE_RESET() ((void)0)
#define HAL_RIF_RISC_SetSlaveSecureAttributes(a,b) ((void)(a),(void)(b))
#define HAL_GPIO_ConfigPinAttributes(a,b,c) ((void)(a),(void)(b),(void)(c))
typedef struct {uint32_t TXBTO,TXBCF;} MockFDCAN;
extern MockFDCAN mock_can;
#define FDCAN3 (&mock_can)
typedef struct {uint32_t PeriphClockSelection,FdcanClockSelection;} RCC_PeriphCLKInitTypeDef;
typedef struct {uint32_t Pin,Mode,Pull,Speed,Alternate;} GPIO_InitTypeDef;
typedef struct {
 uint32_t FrameFormat,Mode,AutoRetransmission,TransmitPause,ProtocolException,NominalPrescaler,NominalTimeSeg1,NominalTimeSeg2,NominalSyncJumpWidth;
 uint32_t DataPrescaler,DataTimeSeg1,DataTimeSeg2,DataSyncJumpWidth,MessageRAMOffset,RxFifo0ElmtSize,RxFifo1ElmtSize,RxBufferSize,TxFifoQueueElmtsNbr,TxFifoQueueMode,TxElmtSize;
} FDCAN_InitTypeDef;
typedef struct {MockFDCAN *Instance;FDCAN_InitTypeDef Init;} FDCAN_HandleTypeDef;
typedef struct {uint32_t Identifier,IdType,TxFrameType,DataLength,ErrorStateIndicator,BitRateSwitch,FDFormat,TxEventFifoControl;} FDCAN_TxHeaderTypeDef;
typedef struct {uint32_t ErrorPassive,BusOff;} FDCAN_ProtocolStatusTypeDef;
HAL_StatusTypeDef HAL_RCCEx_PeriphCLKConfig(RCC_PeriphCLKInitTypeDef *c);
void HAL_GPIO_Init(void *port,GPIO_InitTypeDef *c);
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint32_t p);
HAL_StatusTypeDef HAL_FDCAN_Init(FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *h,uint32_t a,uint32_t b,uint32_t c,uint32_t d);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *h);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(const FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *h,FDCAN_TxHeaderTypeDef *t,uint8_t *d);
uint32_t HAL_FDCAN_GetLatestTxFifoQRequestBuffer(const FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_GetProtocolStatus(FDCAN_HandleTypeDef *h,FDCAN_ProtocolStatusTypeDef *s);
HAL_StatusTypeDef HAL_FDCAN_AbortTxRequest(FDCAN_HandleTypeDef *h,uint32_t mask);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *h);
HAL_StatusTypeDef HAL_FDCAN_DeInit(FDCAN_HandleTypeDef *h);
uint32_t HAL_GetTick(void);
#endif

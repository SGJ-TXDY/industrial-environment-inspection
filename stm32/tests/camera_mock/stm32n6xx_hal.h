#ifndef CAMERA_MOCK_HAL_H
#define CAMERA_MOCK_HAL_H
#include <stdint.h>
#include <stddef.h>
#define HAL_OK 0
#define DCMIPP_PIPE1 1
#define DCMIPP_PIPE2 2
#define DCMIPP_MEMORY_ADDRESS_0 0
#define CMW_MODE_CONTINUOUS 0
#define CMW_ERROR_NONE 0
uint32_t HAL_GetTick(void);
static inline uint32_t __get_PRIMASK(void){return 0;}
static inline void __disable_irq(void){}
static inline void __set_PRIMASK(uint32_t p){(void)p;}
static inline void __DMB(void){}
static inline void SCB_CleanInvalidateDCache_by_Addr(void *p,int32_t n){(void)p;(void)n;}
static inline void SCB_InvalidateDCache_by_Addr(void *p,int32_t n){(void)p;(void)n;}
int HAL_DCMIPP_PIPE_SetMemoryAddress(void *h,uint32_t p,uint32_t m,uint32_t addr);
#endif

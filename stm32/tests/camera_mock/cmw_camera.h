#include "stm32n6xx_hal.h"
void *CMW_CAMERA_GetDCMIPPHandle(void);
int CMW_CAMERA_Start(uint32_t pipe,uint8_t *dest,uint32_t mode);
int CMW_CAMERA_Run(void);

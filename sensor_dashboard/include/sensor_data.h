/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ENV_SENSOR_DATA_H
#define ENV_SENSOR_DATA_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef s32 env_s32;
typedef s16 env_s16;
typedef u16 env_u16;
#else
#include <stdint.h>
typedef int32_t env_s32;
typedef int16_t env_s16;
typedef uint16_t env_u16;
#endif

#define ENV_TEMP_DEVICE "/dev/env_lm75"
#define ENV_ACCEL_DEVICE "/dev/env_adxl345"

/* Native-endian fixed-size ABI, shared by ARM kernel and ARM application. */
struct env_temp_sample {
    env_s32 temperature_mc; /* 1000 = 1 degree C; LM75B step is 125 */
    env_s32 reserved;       /* zero */
};

struct env_accel_sample {
    env_s16 x;
    env_s16 y;
    env_s16 z;
    env_u16 reserved; /* zero; full-resolution scale: nominal 3.9 mg/LSB */
};

#endif

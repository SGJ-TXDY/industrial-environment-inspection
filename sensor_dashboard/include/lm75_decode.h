/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ENV_LM75_DECODE_H
#define ENV_LM75_DECODE_H

/* LM75B (LM75BDP) registers, NXP datasheet: Temp is read-only, two bytes
 * MSB first; D15..D5 hold an 11-bit two's complement value, 0.125 C/LSB.
 */
#define ENV_LM75_REG_TEMP 0x00
#define ENV_LM75_REG_CONF 0x01
#define ENV_LM75_REG_THYST 0x02
#define ENV_LM75_REG_TOS 0x03
#define ENV_LM75_CONF_SHUTDOWN 0x01

/* msb/lsb as received; returns millidegrees C (-55000 .. +125000 valid). */
static inline int env_lm75_decode(unsigned char msb, unsigned char lsb)
{
    int raw = ((msb << 8) | lsb) >> 5;      /* 11 bits, unsigned */
    if (raw & 0x400)
        raw -= 0x800;                        /* sign-extend */
    return raw * 125;
}
#endif

#ifndef INSPECT_H
#define INSPECT_H
#include <stdint.h>

/* STM32N647 image inspection over classic CAN, see docs/CAN_PROTOCOL.md.
 * Standard 11-bit IDs, DLC 8, little-endian multi-byte fields.
 */
#define INSPECT_RESULT_ID 0x210
#define INSPECT_HEARTBEAT_ID 0x211
/* Version 2 adds the STM32 boot session in result byte 3 and heartbeat
 * byte 2. Version 1 frames (no session) are still accepted.
 */
#define INSPECT_VERSION 2
#define INSPECT_VERSION_MIN 1
#define INSPECT_SESSION_NONE 0    /* version 1 frame: session unknown */
#define INSPECT_NORMAL 0
#define INSPECT_DEFECT 1
#define INSPECT_CONF_UNKNOWN 0xff
#define INSPECT_TIMEOUT 3.0
#define INSPECT_MAX_GAP 10000u

/* Board-side controller state shown on screen. */
enum bus_state { BUS_CLOSED, BUS_ACTIVE, BUS_PASSIVE, BUS_OFF };

struct inspect_data {
    int result_seen, result, confidence;   /* confidence -1 when not sent */
    int session;                           /* of the last result, 0 for v1 */
    uint32_t seq;
    double result_at;
    int heartbeat_seen, stm32_state, stm32_session;
    uint32_t stm32_total;
    double heartbeat_at;
    unsigned long normal_count, defect_count;
    unsigned long missed, duplicates, rejected, restarts;
};

/* id excludes EFF/RTR/ERR flags; caller drops those frames.
 * Returns 1 accepted, 0 other ID or duplicate, -1 malformed.
 */
int inspect_parse(struct inspect_data *d, uint32_t id,
                  const uint8_t *data, int dlc, double now);
int inspect_online(const struct inspect_data *d, double now);
const char *inspect_state_name(int state);
#endif

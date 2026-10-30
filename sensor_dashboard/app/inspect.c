#include "inspect.h"

static uint32_t le32(const uint8_t *b)
{
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 |
           (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int inspect_parse(struct inspect_data *d, uint32_t id,
                  const uint8_t *data, int dlc, double now)
{
    uint32_t seq;
    int session;
    if (id != INSPECT_RESULT_ID && id != INSPECT_HEARTBEAT_ID)
        return 0;
    if (!data || dlc != 8 || data[0] < INSPECT_VERSION_MIN || data[0] > INSPECT_VERSION)
        goto bad;
    if (id == INSPECT_HEARTBEAT_ID) {
        d->heartbeat_seen = 1;
        d->heartbeat_at = now;
        d->stm32_state = data[1];
        d->stm32_session = data[0] >= 2 ? data[2] : INSPECT_SESSION_NONE;
        d->stm32_total = le32(data + 4);
        return 1;
    }
    if (data[1] != INSPECT_NORMAL && data[1] != INSPECT_DEFECT)
        goto bad;
    if (data[2] > 100 && data[2] != INSPECT_CONF_UNKNOWN)
        goto bad;
    session = data[0] >= 2 ? data[3] : INSPECT_SESSION_NONE;
    if (data[0] >= 2 && session == INSPECT_SESSION_NONE)
        goto bad;
    seq = le32(data + 4);
    /* An item is identified by session + sequence number, never by the
     * sequence number alone: after an STM32 reboot the counter starts at 1
     * again, and that first item must not be mistaken for a retransmission.
     */
    if (d->result_seen) {
        if (session != d->session) {
            /* New boot session. Its numbering starts at 1, so anything
             * before this seq was sent while we were not seeing it.
             */
            d->restarts++;
            if (session != INSPECT_SESSION_NONE && seq && seq - 1 <= INSPECT_MAX_GAP)
                d->missed += seq - 1;
        } else if (seq == d->seq) {
            /* Same item retransmitted: keep the first copy, count once. */
            d->duplicates++;
            return 0;
        } else if (seq < d->seq || seq - d->seq > INSPECT_MAX_GAP) {
            /* Version 1 (no session), or a reboot that drew the same
             * session number: a lower or implausible number is a restart.
             */
            d->restarts++;
        } else {
            d->missed += seq - d->seq - 1;
        }
    }
    d->session = session;
    d->result_seen = 1;
    d->result = data[1];
    d->confidence = data[2] == INSPECT_CONF_UNKNOWN ? -1 : data[2];
    d->seq = seq;
    d->result_at = now;
    if (d->result == INSPECT_DEFECT)
        d->defect_count++;
    else
        d->normal_count++;
    return 1;
bad:
    d->rejected++;
    return -1;
}

int inspect_online(const struct inspect_data *d, double now)
{
    return (d->heartbeat_seen && now - d->heartbeat_at <= INSPECT_TIMEOUT) ||
           (d->result_seen && now - d->result_at <= INSPECT_TIMEOUT);
}

const char *inspect_state_name(int state)
{
    switch (state) {
    case 0: return "RUNNING";
    case 1: return "IDLE";
    case 2: return "CAMERA FAULT";
    case 3: return "MODEL FAULT";
    default: return "UNKNOWN STATE";
    }
}

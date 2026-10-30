/* Reference sender for the STM32N647 inspection board, see CAN_PROTOCOL.md
 * (protocol version 2). Not built by this project: copy into the STM32
 * firmware.
 *
 * Assumes CubeMX generated MX_FDCAN1_Init() with:
 *   Frame format  FDCAN_FRAME_CLASSIC
 *   Mode          FDCAN_MODE_NORMAL
 *   AutoRetransmission ENABLE
 *   Nominal bitrate 500 kbit/s, e.g. FDCAN kernel clock 50 MHz:
 *     NominalPrescaler 5, NominalTimeSeg1 16, NominalTimeSeg2 3,
 *     NominalSyncJumpWidth 3  -> 20 tq, sample point 85 %
 * Recalculate the prescaler from the real clock tree in CubeMX.
 * Call HAL_FDCAN_Start(&hfdcan1) once after init, then inspect_begin_session().
 */
#include "main.h"

extern FDCAN_HandleTypeDef hfdcan1;
extern RNG_HandleTypeDef hrng;      /* CubeMX: enable RNG */

#define INSPECT_RESULT_ID 0x210
#define INSPECT_HEARTBEAT_ID 0x211
#define INSPECT_VERSION 2

static uint32_t inspect_seq;
static uint8_t inspect_session;

static void put_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* Once per boot: a session number 1..255 so the receiver can tell this
 * run's seq 1 from the previous run's seq 1. A value kept in a backup
 * register and incremented each boot is even better than RNG.
 */
void inspect_begin_session(void)
{
    uint32_t r = 0;
    if (HAL_RNG_GenerateRandomNumber(&hrng, &r) != HAL_OK)
        r = HAL_GetTick();
    inspect_session = (uint8_t)(r % 255u + 1u);
    inspect_seq = 0;
}

static int send8(uint32_t id, uint8_t data[8])
{
    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier = id;
    h.IdType = FDCAN_STANDARD_ID;
    h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = FDCAN_DLC_BYTES_8;
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF;
    h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    h.MessageMarker = 0;
    /* Never block the vision pipeline: a full FIFO drops this frame and
     * the IMX6ULL reports the gap as MISSED.
     */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan1) == 0)
        return -1;
    return HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, data) == HAL_OK ? 0 : -1;
}

/* defect: 0 normal product, 1 defective. confidence 0-100, or 0xFF. */
int inspect_send_result(int defect, uint8_t confidence)
{
    uint8_t d[8] = { INSPECT_VERSION, defect ? 1 : 0, confidence, inspect_session };
    /* Count every inspected item even if sending fails. */
    put_le32(d + 4, ++inspect_seq);
    return send8(INSPECT_RESULT_ID, d);
}

/* Call every 1000 ms. state: 0 running, 1 idle, 2 camera fault, 3 model fault. */
int inspect_send_heartbeat(uint8_t state)
{
    uint8_t d[8] = { INSPECT_VERSION, state, inspect_session, 0 };
    put_le32(d + 4, inspect_seq);
    return send8(INSPECT_HEARTBEAT_ID, d);
}

#ifndef SERVICES_H
#define SERVICES_H
#include <stdint.h>
#include "upload.h"

/* Background threads connected by the mailbox:
 *   collect (main loop) -> "alarm": temperature, inspection results
 *   collect (main loop) -> "db":    temperature, inspection results
 *   "alarm"             -> "db":    temperature alarm ON/OFF events
 *   collect and "alarm" -> "mqtt":  the same records, published by upload.c
 * The alarm thread drives LED0 and the buzzer through /dev/env_alarm.
 */
struct service_status {
    int running;
    int alarm_device_ok, alarm_temp, defect_active;
    unsigned int outputs;           /* ALARM_OUT_* */
    int32_t alarm_temperature_mc;   /* reading that raised/kept the alarm */
    int32_t alarm_low_mc, alarm_high_mc;   /* configured limits */
    int db_ok;
    unsigned long db_rows, db_errors, mail_dropped;
    struct upload_status upload;    /* MQTT */
};

int services_start(void);
void services_publish_sensor(int32_t temperature_mc);
/* session 0: version 1 frame without a boot session. */
void services_publish_inspect(int session, uint32_t seq, int result, int confidence);
void services_status(struct service_status *out);
/* Turns the alarm outputs off and flushes the database. */
void services_stop(void);
#endif

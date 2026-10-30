#ifndef UPLOAD_H
#define UPLOAD_H
#include "mailbox.h"

/* "mqtt" mailbox task: formats records (mqtt_format.c) and publishes them
 * with the Eclipse Paho C client over plain TCP. Records wait in a bounded
 * backlog while the broker is unreachable and are sent in order after
 * reconnecting; reconnects back off from 2 s up to 60 s.
 * Settings come from MQTT_* in /etc/default/sensor-dashboard.
 */
enum upload_state {
    UPLOAD_DISABLED,        /* MQTT_ENABLE=0 */
    UPLOAD_UNCONFIGURED,    /* MQTT_HOST empty */
    UPLOAD_PAUSED,          /* paused from the screen */
    UPLOAD_CONNECTING,
    UPLOAD_CONNECTED,
    UPLOAD_WAITING          /* connect failed, retry at retry_at */
};

#define UPLOAD_RECENT 5

struct upload_record {
    struct mail_msg msg;    /* msg.type -1: status message */
    int ok;
};

struct upload_status {
    int state, qos, error;  /* error: last Paho/CONNACK code, 0 none */
    char server[96], client_id[48], prefix[64];
    unsigned long sent, failed, dropped, backlog, backlog_max, connects;
    double connected_at, retry_at;      /* CLOCK_MONOTONIC seconds */
    struct upload_record recent[UPLOAD_RECENT];   /* newest first */
    unsigned int recent_count;
};

/* Reads the MQTT_* environment; call before starting the task. */
void upload_configure(void);
void *upload_task(void *arg);
void upload_control(int toggle_pause, int reconnect);
void upload_get_status(struct upload_status *out);
#endif

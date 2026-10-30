#include "mqtt_format.h"
#include "alarm_logic.h"
#include <stdio.h>
#include <string.h>

int mqtt_format(const struct mail_msg *m, const char *prefix, struct mqtt_item *out)
{
    const char *topic;
    char session[12], confidence[12];
    long long ts = (long long)m->wall;
    memset(out, 0, sizeof *out);
    out->msg = *m;
    switch (m->type) {
    case MAIL_SENSOR:
        topic = "sensor";
        snprintf(out->payload, sizeof out->payload,
                 "{\"ts\":%lld,\"temperature\":%.3f}", ts, m->u.sensor.temperature_mc / 1000.0);
        break;
    case MAIL_INSPECT:
        topic = "inspection";
        /* session + seq identify the item; null when unknown (v1 frame). */
        if (m->u.inspect.session)
            snprintf(session, sizeof session, "%d", m->u.inspect.session);
        else
            strcpy(session, "null");
        if (m->u.inspect.confidence >= 0)
            snprintf(confidence, sizeof confidence, "%d", m->u.inspect.confidence);
        else
            strcpy(confidence, "null");
        snprintf(out->payload, sizeof out->payload,
                 "{\"ts\":%lld,\"session\":%s,\"seq\":%lu,\"result\":\"%s\",\"confidence\":%s}",
                 ts, session, (unsigned long)m->u.inspect.seq,
                 m->u.inspect.result ? "DEFECT" : "NORMAL", confidence);
        break;
    case MAIL_ALARM:
        topic = "alarm";
        snprintf(out->payload, sizeof out->payload,
                 "{\"ts\":%lld,\"kind\":\"%s\",\"state\":\"%s\",\"value\":%.2f}", ts,
                 alarm_kind_name(m->u.alarm.kind), m->u.alarm.active ? "ON" : "OFF",
                 m->u.alarm.value / 1000.0);
        break;
    default:
        return -1;
    }
    snprintf(out->topic, sizeof out->topic, "%s/%s", prefix, topic);
    return 0;
}

void mqtt_format_status(const char *prefix, int online, time_t ts, struct mqtt_item *out)
{
    memset(out, 0, sizeof *out);
    out->msg.type = -1;
    out->msg.wall = ts;
    out->retained = 1;
    snprintf(out->topic, sizeof out->topic, "%s/status", prefix);
    snprintf(out->payload, sizeof out->payload, "{\"ts\":%lld,\"state\":\"%s\"}",
             (long long)ts, online ? "online" : "offline");
}

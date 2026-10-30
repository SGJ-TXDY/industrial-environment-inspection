#ifndef MQTT_FORMAT_H
#define MQTT_FORMAT_H
#include "mailbox.h"

/* Generic MQTT topics and JSON payloads, no cloud-specific schema yet:
 *   <prefix>/sensor      {"ts":..,"temperature":25.125}   (LM75, 0.125 C steps)
 *   <prefix>/inspection  {"ts":..,"session":37,"seq":1284,"result":"DEFECT","confidence":88}
 *   <prefix>/alarm       {"ts":..,"kind":"TEMP_HIGH","state":"ON","value":41.20}
 *   <prefix>/status      {"ts":..,"state":"online"} retained; "offline" is the will
 * A cloud platform (e.g. OneNET) maps onto this later in one place: here.
 */
#define MQTT_TOPIC_MAX 128
#define MQTT_PAYLOAD_MAX 192

struct mqtt_item {
    char topic[MQTT_TOPIC_MAX];
    char payload[MQTT_PAYLOAD_MAX];
    int retained;
    struct mail_msg msg;        /* source record, for the upload screen */
};

/* 0 on success, -1 for message types that are not uploaded. */
int mqtt_format(const struct mail_msg *m, const char *prefix, struct mqtt_item *out);
void mqtt_format_status(const char *prefix, int online, time_t ts, struct mqtt_item *out);
#endif

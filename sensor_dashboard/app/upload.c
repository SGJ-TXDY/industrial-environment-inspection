#define _DEFAULT_SOURCE
#include "upload.h"
#include "mqtt_format.h"
#include "MQTTClient.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BACKOFF_MIN 2.0
#define BACKOFF_MAX 60.0
#define PUBLISH_BATCH 20

#if !defined(_WIN32)
/* Library constructor in the shared libpaho-mqtt3c (-Wl,-init); with the
 * static build it is called here instead, once.
 */
void MQTTClient_init(void);
static pthread_once_t paho_once = PTHREAD_ONCE_INIT;
#endif

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static struct upload_status status;
static int paused, reconnect_request;

static struct {
    char host[64], client_id[48], user[64], password[96], prefix[64];
    int enabled, port, qos, keepalive;
    double sensor_period;
    unsigned int backlog_max;
} cfg;

static struct mqtt_item *ring;
static unsigned int head, count;
static MQTTClient client;
static int have_client;

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static const char *env(const char *name, const char *fallback)
{
    const char *v = getenv(name);
    return v && *v ? v : fallback;
}

static long env_long(const char *name, long fallback, long min, long max)
{
    char *end;
    const char *text = env(name, NULL);
    long v;
    if (!text)
        return fallback;
    v = strtol(text, &end, 10);
    if (*end || v < min || v > max) {
        fprintf(stderr, "%s=%s invalid, using %ld\n", name, text, fallback);
        return fallback;
    }
    return v;
}

void upload_configure(void)
{
    char host[40] = "board";
    memset(&cfg, 0, sizeof cfg);
    cfg.enabled = env_long("MQTT_ENABLE", 1, 0, 1);
    snprintf(cfg.host, sizeof cfg.host, "%s", env("MQTT_HOST", ""));
    cfg.port = (int)env_long("MQTT_PORT", 1883, 1, 65535);
    gethostname(host, sizeof host - 1);
    if (env("MQTT_CLIENT_ID", NULL))
        snprintf(cfg.client_id, sizeof cfg.client_id, "%s", env("MQTT_CLIENT_ID", ""));
    else
        snprintf(cfg.client_id, sizeof cfg.client_id, "imx6ull-%s", host);
    snprintf(cfg.user, sizeof cfg.user, "%s", env("MQTT_USERNAME", ""));
    snprintf(cfg.password, sizeof cfg.password, "%s", env("MQTT_PASSWORD", ""));
    snprintf(cfg.prefix, sizeof cfg.prefix, "%s", env("MQTT_TOPIC_PREFIX", "imx6ull/sensor-dashboard"));
    cfg.qos = (int)env_long("MQTT_QOS", 1, 0, 1);
    cfg.keepalive = (int)env_long("MQTT_KEEPALIVE", 60, 10, 3600);
    cfg.sensor_period = env_long("MQTT_SENSOR_PERIOD", 10, 0, 3600);
    cfg.backlog_max = (unsigned int)env_long("MQTT_BACKLOG", 200, 1, 5000);

    pthread_mutex_lock(&lock);
    memset(&status, 0, sizeof status);
    status.state = !cfg.enabled ? UPLOAD_DISABLED : !cfg.host[0] ? UPLOAD_UNCONFIGURED : UPLOAD_CONNECTING;
    status.qos = cfg.qos;
    status.backlog_max = cfg.backlog_max;
    if (cfg.host[0])
        snprintf(status.server, sizeof status.server, "tcp://%s:%d", cfg.host, cfg.port);
    snprintf(status.client_id, sizeof status.client_id, "%s", cfg.client_id);
    snprintf(status.prefix, sizeof status.prefix, "%s", cfg.prefix);
    paused = reconnect_request = 0;
    pthread_mutex_unlock(&lock);
    if (cfg.enabled)
        fprintf(stderr, "MQTT: %s client %s topics %s/*, QoS %d\n",
                cfg.host[0] ? status.server : "(MQTT_HOST not set)", cfg.client_id, cfg.prefix, cfg.qos);
}

/* Oldest records are dropped first; the database still has them. */
static void push(const struct mqtt_item *it)
{
    if (count == cfg.backlog_max) {
        head = (head + 1) % cfg.backlog_max;
        count--;
        pthread_mutex_lock(&lock);
        status.dropped++;
        pthread_mutex_unlock(&lock);
    }
    ring[(head + count) % cfg.backlog_max] = *it;
    count++;
}

static void record(const struct mqtt_item *it, int ok)
{
    pthread_mutex_lock(&lock);
    memmove(status.recent + 1, status.recent, sizeof status.recent[0] * (UPLOAD_RECENT - 1));
    status.recent[0].msg = it->msg;
    status.recent[0].ok = ok;
    if (status.recent_count < UPLOAD_RECENT)
        status.recent_count++;
    if (ok)
        status.sent++;
    else
        status.failed++;
    pthread_mutex_unlock(&lock);
}

static void set_state(int state, int error)
{
    pthread_mutex_lock(&lock);
    status.state = state;
    if (error)
        status.error = error;
    status.backlog = count;
    pthread_mutex_unlock(&lock);
}

static int link_connect(void)
{
    MQTTClient_connectOptions o = MQTTClient_connectOptions_initializer;
    MQTTClient_willOptions will = MQTTClient_willOptions_initializer;
    struct mqtt_item offline;
    char uri[96];
    int rc;
    if (!have_client) {
        snprintf(uri, sizeof uri, "tcp://%s:%d", cfg.host, cfg.port);
        rc = MQTTClient_create(&client, uri, cfg.client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
        if (rc != MQTTCLIENT_SUCCESS)
            return rc;
        have_client = 1;
    }
    /* Broker publishes "offline" (retained) if the board disappears. */
    mqtt_format_status(cfg.prefix, 0, time(NULL), &offline);
    will.topicName = offline.topic;
    will.message = offline.payload;
    will.retained = 1;
    will.qos = cfg.qos;
    o.keepAliveInterval = cfg.keepalive;
    o.cleansession = 1;
    o.connectTimeout = 5;
    o.will = &will;
    o.username = cfg.user[0] ? cfg.user : NULL;
    o.password = cfg.password[0] ? cfg.password : NULL;
    return MQTTClient_connect(client, &o);
}

static int link_publish(const struct mqtt_item *it)
{
    MQTTClient_message m = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int rc;
    m.payload = (void *)it->payload;
    m.payloadlen = (int)strlen(it->payload);
    m.qos = cfg.qos;
    m.retained = it->retained;
    rc = MQTTClient_publishMessage(client, it->topic, &m, &token);
    if (rc == MQTTCLIENT_SUCCESS && cfg.qos)
        rc = MQTTClient_waitForCompletion(client, token, 5000L);
    return rc;
}

static void link_close(int say_offline)
{
    struct mqtt_item offline;
    if (say_offline) {
        mqtt_format_status(cfg.prefix, 0, time(NULL), &offline);
        link_publish(&offline);
    }
    MQTTClient_disconnect(client, 1000);
}

void *upload_task(void *arg)
{
    const char *self = arg;
    struct mail_msg m;
    struct mqtt_item it;
    double now, last_sensor = -1e9, next_try = 0, backoff = BACKOFF_MIN;
    int connected = 0, rc, want_pause, want_reconnect, i;
    ring = calloc(cfg.backlog_max, sizeof *ring);
    if (!ring) {
        fprintf(stderr, "MQTT: no memory for backlog\n");
        while (recv_mail_msg(self, &m, 1000) >= 0)
            ;
        return NULL;
    }
#if !defined(_WIN32)
    pthread_once(&paho_once, MQTTClient_init);
#endif
    while ((rc = recv_mail_msg(self, &m, connected ? 20 : 200)) >= 0) {
        now = mono();
        if (!rc && cfg.enabled && (m.type != MAIL_SENSOR || now - last_sensor >= cfg.sensor_period)) {
            if (m.type == MAIL_SENSOR)
                last_sensor = now;
            if (!mqtt_format(&m, cfg.prefix, &it))
                push(&it);
        }
        if (!cfg.enabled || !cfg.host[0]) {
            set_state(cfg.enabled ? UPLOAD_UNCONFIGURED : UPLOAD_DISABLED, 0);
            continue;
        }
        pthread_mutex_lock(&lock);
        want_pause = paused;
        want_reconnect = reconnect_request;
        reconnect_request = 0;
        pthread_mutex_unlock(&lock);

        if (want_reconnect) {
            if (connected)
                link_close(0);
            connected = 0;
            next_try = now;
            backoff = BACKOFF_MIN;
        }
        if (want_pause) {
            if (connected)
                link_close(1);
            connected = 0;
            next_try = now;
            set_state(UPLOAD_PAUSED, 0);
            continue;
        }
        if (connected && !MQTTClient_isConnected(client)) {
            fprintf(stderr, "MQTT: connection lost\n");
            connected = 0;
            next_try = now;
            set_state(UPLOAD_WAITING, MQTTCLIENT_DISCONNECTED);
        }
        if (!connected && now >= next_try) {
            set_state(UPLOAD_CONNECTING, 0);
            rc = link_connect();
            now = mono();
            if (rc == MQTTCLIENT_SUCCESS) {
                mqtt_format_status(cfg.prefix, 1, time(NULL), &it);
                rc = link_publish(&it);
                if (rc == MQTTCLIENT_SUCCESS)
                    record(&it, 1);
                else
                    MQTTClient_disconnect(client, 0);
            }
            if (rc == MQTTCLIENT_SUCCESS) {
                connected = 1;
                backoff = BACKOFF_MIN;
                fprintf(stderr, "MQTT: connected to %s:%d\n", cfg.host, cfg.port);
                pthread_mutex_lock(&lock);
                status.connects++;
                status.connected_at = now;
                status.error = 0;
                pthread_mutex_unlock(&lock);
            } else {
                next_try = now + backoff;
                fprintf(stderr, "MQTT: connect %s:%d failed (%d), retry in %.0f s\n",
                        cfg.host, cfg.port, rc, backoff);
                backoff = backoff * 2 > BACKOFF_MAX ? BACKOFF_MAX : backoff * 2;
                pthread_mutex_lock(&lock);
                status.retry_at = next_try;
                pthread_mutex_unlock(&lock);
                set_state(UPLOAD_WAITING, rc);
            }
        }
        if (connected) {
            for (i = 0; i < PUBLISH_BATCH && count; i++) {
                rc = link_publish(&ring[head]);
                record(&ring[head], rc == MQTTCLIENT_SUCCESS);
                if (rc != MQTTCLIENT_SUCCESS) {
                    /* Keep the record; it goes out first after reconnecting. */
                    fprintf(stderr, "MQTT: publish failed (%d)\n", rc);
                    MQTTClient_disconnect(client, 0);
                    connected = 0;
                    next_try = mono();
                    set_state(UPLOAD_WAITING, rc);
                    break;
                }
                head = (head + 1) % cfg.backlog_max;
                count--;
            }
            /* Sync client without callbacks: keepalive pings happen here. */
            if (connected)
                MQTTClient_yield();
            if (connected)
                set_state(UPLOAD_CONNECTED, 0);
        } else {
            set_state(status.state == UPLOAD_CONNECTING ? UPLOAD_WAITING : status.state, 0);
        }
    }
    if (connected)
        link_close(1);
    if (have_client)
        MQTTClient_destroy(&client);
    have_client = 0;
    free(ring);
    ring = NULL;
    head = count = 0;
    return NULL;
}

void upload_control(int toggle_pause, int reconnect)
{
    pthread_mutex_lock(&lock);
    if (toggle_pause)
        paused = !paused;
    if (reconnect) {
        reconnect_request = 1;
        paused = 0;
    }
    pthread_mutex_unlock(&lock);
}

void upload_get_status(struct upload_status *out)
{
    pthread_mutex_lock(&lock);
    *out = status;
    pthread_mutex_unlock(&lock);
}

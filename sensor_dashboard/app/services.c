#define _DEFAULT_SOURCE
#include "services.h"
#include "mailbox.h"
#include "alarm_logic.h"
#include "store.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SELF "collect"

static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;
static struct service_status status;
static struct alarm_config alarm_cfg;
static int started;

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

static double env_number(const char *name, double fallback)
{
    char *end;
    const char *text = env(name, NULL);
    double v;
    if (!text)
        return fallback;
    v = strtod(text, &end);
    if (*end || v != v) {
        fprintf(stderr, "%s=%s invalid, using %g\n", name, text, fallback);
        return fallback;
    }
    return v;
}

static int32_t celsius_mc(double c)
{
    return (int32_t)(c * 1000.0 + (c < 0 ? -0.5 : 0.5));
}

static void load_alarm_config(void)
{
    struct alarm_config c;
    alarm_config_default(&c);
    c.temp_high_mc = celsius_mc(env_number("ALARM_TEMP_HIGH", c.temp_high_mc / 1000.0));
    c.temp_low_mc = celsius_mc(env_number("ALARM_TEMP_LOW", c.temp_low_mc / 1000.0));
    c.hysteresis_mc = celsius_mc(env_number("ALARM_HYSTERESIS", c.hysteresis_mc / 1000.0));
    c.defect_ms = (unsigned int)env_number("ALARM_DEFECT_MS", c.defect_ms);
    c.buzzer = env_number("ALARM_BUZZER", 1) != 0;
    if (c.temp_low_mc >= c.temp_high_mc || c.hysteresis_mc < 0 ||
        c.hysteresis_mc >= c.temp_high_mc - c.temp_low_mc || c.defect_ms > 60000) {
        fprintf(stderr, "ALARM_* limits inconsistent, using defaults\n");
        alarm_config_default(&c);
    }
    alarm_cfg = c;
    fprintf(stderr, "Alarm: temperature %.1f..%.1f C (hysteresis %.1f), defect %u ms, buzzer %s\n",
            c.temp_low_mc / 1000.0, c.temp_high_mc / 1000.0, c.hysteresis_mc / 1000.0,
            c.defect_ms, c.buzzer ? "on" : "off");
}

static int write_outputs(int fd, unsigned int outputs)
{
    char c = (char)('0' + outputs);
    return write(fd, &c, 1) == 1 ? 0 : -1;
}

static void *alarm_task(void *arg)
{
    const char *self = arg, *device = env("ALARM_DEVICE", "/dev/env_alarm");
    struct alarm_state st = { 0 };
    struct mail_msg m;
    unsigned int last = ~0u, out;
    double next_open = 0;
    int fd = -1, logged = 0, rc;
    int32_t temperature = 0;
    while ((rc = recv_mail_msg(self, &m, 50)) >= 0) {
        double now = mono();
        if (!rc && m.type == MAIL_SENSOR) {
            int before = st.temp;
            temperature = m.u.sensor.temperature_mc;
            if (alarm_temperature(&st, &alarm_cfg, temperature, now)) {
                struct mail_msg e;
                int kind = st.temp != ALARM_NONE ? st.temp : before;
                memset(&e, 0, sizeof e);
                e.type = MAIL_ALARM;
                e.wall = m.wall;
                e.u.alarm.kind = kind;
                e.u.alarm.active = st.temp != ALARM_NONE;
                e.u.alarm.value = temperature;
                fprintf(stderr, "ALARM %s %s at %.1f C\n", alarm_kind_name(kind),
                        e.u.alarm.active ? "ON" : "OFF", temperature / 1000.0);
                send_mail_msg(self, "db", &e);
                send_mail_msg(self, "mqtt", &e);
            }
        } else if (!rc && m.type == MAIL_INSPECT && m.u.inspect.result) {
            alarm_defect(&st, &alarm_cfg, now);
        }
        if (fd < 0 && now >= next_open) {
            fd = open(device, O_WRONLY | O_CLOEXEC);
            if (fd < 0) {
                if (!logged)
                    fprintf(stderr, "Alarm output %s: %s\n", device, strerror(errno));
                logged = 1;
                next_open = now + 2;
            } else {
                logged = 0;
                last = ~0u;
            }
        }
        out = alarm_outputs(&st, &alarm_cfg, now);
        if (fd >= 0 && out != last) {
            if (write_outputs(fd, out)) {
                fprintf(stderr, "Alarm output write: %s\n", strerror(errno));
                close(fd);
                fd = -1;
                next_open = now + 2;
            } else {
                last = out;
            }
        }
        pthread_mutex_lock(&status_lock);
        status.alarm_device_ok = fd >= 0;
        status.alarm_temp = st.temp;
        status.alarm_temperature_mc = temperature;
        status.defect_active = now < st.defect_until;
        status.outputs = out;
        pthread_mutex_unlock(&status_lock);
    }
    if (fd >= 0) {
        write_outputs(fd, 0);
        close(fd);
    }
    return NULL;
}

static void make_parent(const char *path)
{
    char dir[256];
    char *p;
    snprintf(dir, sizeof dir, "%s", path);
    for (p = dir + 1; *p; p++) {
        if (*p != '/')
            continue;
        *p = 0;
        mkdir(dir, 0755);
        *p = '/';
    }
}

static void *db_task(void *arg)
{
    const char *self = arg, *path = env("DB_PATH", "/var/lib/sensor-dashboard/sensor.db");
    double period = env_number("DB_SENSOR_PERIOD", 10);
    double keep_days = env_number("DB_KEEP_DAYS", 30);
    double now, next_open = 0, last_commit = 0, next_prune = 0, last_sensor = -1e9;
    struct store st;
    struct mail_msg m;
    unsigned long rows = 0, errors = 0;
    int ok = 0, logged = 0, rc;
    memset(&st, 0, sizeof st);
    do {
        rc = recv_mail_msg(self, &m, 500);
        now = mono();
        if (!ok && rc >= 0 && now >= next_open) {
            make_parent(path);
            if (!store_open(&st, path)) {
                ok = 1;
                logged = 0;
                fprintf(stderr, "Database %s: OK\n", path);
            } else {
                if (!logged)
                    fprintf(stderr, "Database %s: %s\n", path, store_error(&st));
                logged = 1;
                store_close(&st);
                next_open = now + 10;
            }
        }
        if (!rc) {
            int r = 0, wrote = 1;
            if (!ok)
                r = -1;
            else if (m.type == MAIL_SENSOR) {
                /* LM75 is read every second; keep one row per period. */
                if (now - last_sensor >= period) {
                    r = store_sensor(&st, m.wall, m.u.sensor.temperature_mc);
                    last_sensor = now;
                } else {
                    wrote = 0;
                }
            } else if (m.type == MAIL_INSPECT) {
                r = store_inspect(&st, m.wall, m.u.inspect.session, m.u.inspect.seq,
                                  m.u.inspect.result, m.u.inspect.confidence);
            } else if (m.type == MAIL_ALARM) {
                r = store_alarm(&st, m.wall, alarm_kind_name(m.u.alarm.kind),
                                m.u.alarm.active, m.u.alarm.value / 1000.0);
            }
            if (r) {
                if (ok && !errors)
                    fprintf(stderr, "Database insert: %s\n", store_error(&st));
                errors++;
            } else if (wrote) {
                rows++;
            }
        }
        if (ok && st.pending && (now - last_commit >= 1.0 || st.pending >= 100 || rc < 0)) {
            unsigned int pending = st.pending;
            if (store_commit(&st)) {
                fprintf(stderr, "Database commit: %s\n", store_error(&st));
                errors += pending;
            }
            last_commit = now;
        }
        if (ok && keep_days > 0 && now >= next_prune) {
            long removed = store_prune(&st, time(NULL) - (time_t)(keep_days * 86400));
            if (removed > 0)
                fprintf(stderr, "Database: removed %ld rows older than %g days\n",
                        removed, keep_days);
            next_prune = now + 3600;
        }
        pthread_mutex_lock(&status_lock);
        status.db_ok = ok;
        status.db_rows = rows;
        status.db_errors = errors;
        pthread_mutex_unlock(&status_lock);
    } while (rc >= 0);
    store_close(&st);
    return NULL;
}

int services_start(void)
{
    if (started)
        return 0;
    load_alarm_config();
    upload_configure();
    create_mailbox_system();
    if (register_thread_mail_task("alarm", alarm_task, 64) ||
        register_thread_mail_task("db", db_task, 512) ||
        register_thread_mail_task("mqtt", upload_task, 256) ||
        start_mail_tasks()) {
        fprintf(stderr, "Mailbox threads: %s\n", strerror(errno));
        destroy_mailbox_system();
        return -1;
    }
    started = 1;
    pthread_mutex_lock(&status_lock);
    status.running = 1;
    pthread_mutex_unlock(&status_lock);
    return 0;
}

static void publish(struct mail_msg *m)
{
    if (!started)
        return;
    m->wall = time(NULL);
    send_mail_msg(SELF, "alarm", m);
    send_mail_msg(SELF, "db", m);
    send_mail_msg(SELF, "mqtt", m);
}

void services_publish_sensor(int32_t temperature_mc)
{
    struct mail_msg m;
    memset(&m, 0, sizeof m);
    m.type = MAIL_SENSOR;
    m.u.sensor.temperature_mc = temperature_mc;
    publish(&m);
}

void services_publish_inspect(int session, uint32_t seq, int result, int confidence)
{
    struct mail_msg m;
    memset(&m, 0, sizeof m);
    m.type = MAIL_INSPECT;
    m.u.inspect.session = session;
    m.u.inspect.seq = seq;
    m.u.inspect.result = result;
    m.u.inspect.confidence = confidence;
    publish(&m);
}

void services_status(struct service_status *out)
{
    pthread_mutex_lock(&status_lock);
    *out = status;
    pthread_mutex_unlock(&status_lock);
    out->alarm_low_mc = alarm_cfg.temp_low_mc;
    out->alarm_high_mc = alarm_cfg.temp_high_mc;
    if (started)
        out->mail_dropped = mail_dropped("alarm") + mail_dropped("db");
    upload_get_status(&out->upload);
}

void services_stop(void)
{
    if (!started)
        return;
    destroy_mailbox_system();
    started = 0;
    pthread_mutex_lock(&status_lock);
    status.running = 0;
    pthread_mutex_unlock(&status_lock);
}

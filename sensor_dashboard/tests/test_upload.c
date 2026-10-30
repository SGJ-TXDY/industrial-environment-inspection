/* Drives the "mqtt" upload task against tests/fake_broker.py.
 * Usage: test-upload flow <port>   broker starts late; backlog, overflow,
 *                                  pause/resume, reconnect, shutdown
 *        test-upload auth <port>   broker refuses the password (CONNACK 4)
 * tests/test_upload.py starts the broker and checks what it received.
 */
#define _DEFAULT_SOURCE
#include "mailbox.h"
#include "upload.h"
#include "alarm_logic.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static struct upload_status st;

static int wait_for(int (*done)(void), int seconds)
{
    int i;
    for (i = 0; i < seconds * 20; i++) {
        upload_get_status(&st);
        if (done())
            return 1;
        usleep(50000);
    }
    upload_get_status(&st);
    fprintf(stderr, "timeout: state %d sent %lu backlog %lu error %d\n",
            st.state, st.sent, st.backlog, st.error);
    return 0;
}

static unsigned long want_sent;
static int sent_reached(void) { return st.sent >= want_sent && !st.backlog; }
static int is_paused(void) { return st.state == UPLOAD_PAUSED; }
static int is_waiting(void) { return st.state == UPLOAD_WAITING; }
static int refused(void) { return st.error == 4; }

static void post(int type, uint32_t a, int b, int c)
{
    struct mail_msg m;
    memset(&m, 0, sizeof m);
    m.type = type;
    m.wall = time(NULL);
    if (type == MAIL_SENSOR) {
        m.u.sensor.temperature_mc = (int32_t)a;
    } else if (type == MAIL_INSPECT) {
        m.u.inspect.session = a == 2 ? 0 : 5;   /* seq 2 as a version 1 record */
        m.u.inspect.seq = a;
        m.u.inspect.result = b;
        m.u.inspect.confidence = c;
    } else {
        m.u.alarm.kind = ALARM_TEMP_HIGH;
        m.u.alarm.active = 1;
        m.u.alarm.value = (int32_t)a;
    }
    assert(!send_mail_msg("test", "mqtt", &m));
}

static void start(const char *port)
{
    setenv("MQTT_HOST", "127.0.0.1", 1);
    setenv("MQTT_PORT", port, 1);
    setenv("MQTT_CLIENT_ID", "test-board", 1);
    setenv("MQTT_TOPIC_PREFIX", "test/dash", 1);
    setenv("MQTT_SENSOR_PERIOD", "0", 1);
    setenv("MQTT_BACKLOG", "3", 1);
    upload_configure();
    assert(!create_mailbox_system());
    assert(!register_thread_mail_task("mqtt", upload_task, 32));
    assert(!start_mail_tasks());
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (!strcmp(argv[1], "auth")) {
        setenv("MQTT_USERNAME", "user", 1);
        setenv("MQTT_PASSWORD", "wrong", 1);
        start(argv[2]);
        assert(wait_for(refused, 20) && st.state == UPLOAD_WAITING && !st.connects);
        destroy_mailbox_system();
        puts("PASS upload: refused password reported as CONNACK 4, no connection counted");
        return 0;
    }
    start(argv[2]);
    /* Broker is still closed: records wait, the oldest overflows. */
    post(MAIL_SENSOR, 25400, 0, 0);
    post(MAIL_INSPECT, 1, 0, 97);
    post(MAIL_INSPECT, 2, 1, -1);
    post(MAIL_ALARM, 41200, 0, 0);
    assert(wait_for(is_waiting, 10));
    upload_get_status(&st);
    assert(st.backlog == 3 && st.dropped == 1 && !st.sent);
    want_sent = 4;          /* online status + 3 records */
    assert(wait_for(sent_reached, 30));
    assert(st.state == UPLOAD_CONNECTED && st.connects == 1 && !st.failed);
    assert(st.recent_count == 4 && st.recent[0].msg.type == MAIL_ALARM && st.recent[0].ok);
    assert(st.recent[3].msg.type == -1);

    post(MAIL_INSPECT, 3, 0, 90);
    want_sent = 5;
    assert(wait_for(sent_reached, 10));

    upload_control(1, 0);
    assert(wait_for(is_paused, 10));
    post(MAIL_SENSOR, 26125, 0, 0);
    usleep(500000);
    upload_get_status(&st);
    assert(st.state == UPLOAD_PAUSED && st.backlog == 1 && st.sent == 5);
    upload_control(1, 0);
    want_sent = 7;          /* online again + sensor */
    assert(wait_for(sent_reached, 10) && st.connects == 2);

    upload_control(0, 1);   /* reconnect on request */
    want_sent = 8;
    assert(wait_for(sent_reached, 10) && st.connects == 3);
    destroy_mailbox_system();
    puts("PASS upload: offline backlog and overflow, ordered resend, pause/resume, reconnect, offline status at exit");
    return 0;
}

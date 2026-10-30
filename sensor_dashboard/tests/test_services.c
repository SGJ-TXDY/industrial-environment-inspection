/* Host test: mailbox, SQLite store and the alarm/db threads end to end.
 * The alarm device is a plain file; argv[1] is a scratch directory.
 */
#define _DEFAULT_SOURCE
#include "mailbox.h"
#include "store.h"
#include "services.h"
#include "alarm_logic.h"
#include "sqlite3.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void *idle_task(void *arg) { (void)arg; return NULL; }

static long count(const char *path, const char *sql)
{
    sqlite3 *db;
    sqlite3_stmt *q;
    long n = -1;
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    assert(sqlite3_prepare_v2(db, sql, -1, &q, NULL) == SQLITE_OK);
    if (sqlite3_step(q) == SQLITE_ROW)
        n = (long)sqlite3_column_int64(q, 0);
    sqlite3_finalize(q);
    sqlite3_close(db);
    return n;
}

static void test_mailbox(void)
{
    struct mail_msg m, r;
    int i;
    memset(&m, 0, sizeof m);
    assert(!create_mailbox_system());
    assert(!register_thread_mail_task("box", idle_task, 2));
    assert(register_thread_mail_task("box", idle_task, 2) == -1);
    assert(register_thread_mail_task("name-too-long-for-box", idle_task, 2) == -1);
    assert(recv_mail_msg("box", &r, 20) == 1);
    for (i = 0; i < 3; i++) {
        m.u.inspect.seq = i;
        assert(send_mail_msg("test", "box", &m) == (i < 2 ? 0 : -1));
    }
    assert(mail_dropped("box") == 1 && send_mail_msg("test", "none", &m) == -1);
    assert(!recv_mail_msg("box", &r, 0) && r.u.inspect.seq == 0 && !strcmp(r.sender, "test"));
    assert(!start_mail_tasks());
    destroy_mailbox_system();
    puts("PASS mailbox: register, timeout, FIFO, full drop, unknown receiver");
}

static void test_store(const char *dir)
{
    char path[512];
    struct store s;
    snprintf(path, sizeof path, "%s/store-test.db", dir);
    unlink(path);
    assert(!store_open(&s, path));
    assert(!store_sensor(&s, 100, 25400) && !store_sensor(&s, 5000, 26125));
    assert(!store_inspect(&s, 5000, 0, 7, 1, -1) && !store_inspect(&s, 5001, 37, 8, 0, 97));
    assert(!store_alarm(&s, 5002, "TEMP_HIGH", 1, 41.5) && s.pending == 5);
    assert(!store_commit(&s) && !s.pending);
    assert(store_prune(&s, 1000) == 1);
    store_close(&s);
    assert(count(path, "SELECT count(*) FROM sensor") == 1);
    assert(count(path, "SELECT temperature*1000 FROM sensor") == 26125);
    assert(count(path, "SELECT count(*) FROM inspection WHERE result='DEFECT' AND confidence IS NULL") == 1);
    assert(count(path, "SELECT confidence FROM inspection WHERE seq=8") == 97);
    assert(count(path, "SELECT count(*) FROM inspection WHERE session=37 AND seq=8") == 1);
    assert(count(path, "SELECT count(*) FROM inspection WHERE session IS NULL AND seq=7") == 1);
    assert(count(path, "SELECT count(*) FROM alarm WHERE kind='TEMP_HIGH' AND state='ON'") == 1);
    unlink(path);

    /* Database from earlier versions: inspection without session and the
     * DHT11 sensor table with humidity NOT NULL. */
    {
        sqlite3 *db;
        assert(sqlite3_open(path, &db) == SQLITE_OK);
        assert(sqlite3_exec(db, "CREATE TABLE inspection(id INTEGER PRIMARY KEY, ts INTEGER NOT NULL,"
                            " seq INTEGER NOT NULL, result TEXT NOT NULL, confidence INTEGER);"
                            "INSERT INTO inspection(ts,seq,result,confidence) VALUES(1,5,'NORMAL',90);"
                            "CREATE TABLE sensor(id INTEGER PRIMARY KEY, ts INTEGER NOT NULL,"
                            " temperature REAL NOT NULL, humidity REAL NOT NULL);"
                            "CREATE INDEX sensor_ts ON sensor(ts);"
                            "INSERT INTO sensor(ts,temperature,humidity) VALUES(1,24.0,55.0);",
                            NULL, NULL, NULL) == SQLITE_OK);
        sqlite3_close(db);
    }
    assert(!store_open(&s, path));
    assert(!store_inspect(&s, 2, 4, 1, 0, 99) && !store_sensor(&s, 2, 30500) && !store_commit(&s));
    store_close(&s);
    assert(!store_open(&s, path));   /* column already there: no error */
    store_close(&s);
    assert(count(path, "SELECT count(*) FROM inspection") == 2);
    assert(count(path, "SELECT count(*) FROM inspection WHERE session=4 AND seq=1") == 1);
    assert(count(path, "SELECT count(*) FROM sensor_dht11 WHERE humidity=55") == 1);
    assert(count(path, "SELECT count(*) FROM sensor") == 1);
    assert(count(path, "SELECT count(*) FROM sqlite_master WHERE name='sensor_ts' AND tbl_name='sensor'") == 1);
    unlink(path);
    puts("PASS store: schema, inserts, NULL confidence/session, transaction commit, prune, session column added and DHT11 table kept as sensor_dht11 in old DB");
}

static char last_output(const char *path)
{
    FILE *f = fopen(path, "rb");
    int c, last = -1;
    assert(f);
    while ((c = fgetc(f)) != EOF)
        last = c;
    fclose(f);
    return (char)last;
}

static void test_services(const char *dir)
{
    char db[512], dev[512];
    struct service_status st;
    FILE *f;
    int i;
    snprintf(db, sizeof db, "%s/services-test/sensor.db", dir);
    snprintf(dev, sizeof dev, "%s/alarm-device.txt", dir);
    unlink(db);
    f = fopen(dev, "wb");
    assert(f);
    fclose(f);
    setenv("DB_PATH", db, 1);
    setenv("ALARM_DEVICE", dev, 1);
    setenv("ALARM_TEMP_HIGH", "30", 1);
    setenv("DB_SENSOR_PERIOD", "0", 1);
    assert(!services_start());
    services_publish_sensor(25000);
    services_publish_sensor(31000);              /* temperature alarm ON */
    services_publish_inspect(5, 1, 0, 95);
    services_publish_inspect(5, 2, 1, 88);       /* defect beep */
    for (i = 0; i < 40; i++) {
        services_status(&st);
        if (st.alarm_temp == ALARM_TEMP_HIGH && st.defect_active && st.db_rows >= 5)
            break;
        usleep(50000);
    }
    assert(st.running && st.alarm_device_ok && st.alarm_temp == ALARM_TEMP_HIGH);
    assert(st.defect_active && (st.outputs & ALARM_OUT_LED) && st.db_ok);
    assert(st.upload.state == UPLOAD_UNCONFIGURED);   /* MQTT_HOST not set */
    services_publish_sensor(28000);              /* below 30 - 1: OFF */
    usleep(300000);
    services_stop();
    services_status(&st);
    assert(!st.running && !st.db_errors && !st.mail_dropped);
    assert(last_output(dev) == '0');
    assert(count(db, "SELECT count(*) FROM sensor") == 3);
    assert(count(db, "SELECT count(*) FROM inspection") == 2);
    assert(count(db, "SELECT count(*) FROM inspection WHERE session=5 AND seq=2 AND result='DEFECT' AND confidence=88") == 1);
    assert(count(db, "SELECT count(*) FROM alarm WHERE kind='TEMP_HIGH'") == 2);
    assert(count(db, "SELECT count(*) FROM alarm WHERE state='OFF' AND value=28") == 1);
    puts("PASS services: alarm thread drives device, db thread stores sensor/inspection/alarm, outputs off at stop, MQTT idle without MQTT_HOST");
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    test_mailbox();
    test_store(dir);
    test_services(dir);
    puts("ALL SERVICE TESTS PASSED");
    return 0;
}

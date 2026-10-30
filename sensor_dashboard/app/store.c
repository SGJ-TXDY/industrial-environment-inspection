#include "store.h"
#include "sqlite3.h"
#include <stdio.h>
#include <string.h>

static const char schema[] =
    "PRAGMA synchronous=NORMAL;"
    "CREATE TABLE IF NOT EXISTS sensor("
    " id INTEGER PRIMARY KEY, ts INTEGER NOT NULL,"
    " temperature REAL NOT NULL);"
    "CREATE TABLE IF NOT EXISTS inspection("
    " id INTEGER PRIMARY KEY, ts INTEGER NOT NULL, seq INTEGER NOT NULL,"
    " result TEXT NOT NULL CHECK(result IN('NORMAL','DEFECT')),"
    " confidence INTEGER, session INTEGER);"
    "CREATE TABLE IF NOT EXISTS alarm("
    " id INTEGER PRIMARY KEY, ts INTEGER NOT NULL, kind TEXT NOT NULL,"
    " state TEXT NOT NULL CHECK(state IN('ON','OFF')), value REAL);"
    "CREATE INDEX IF NOT EXISTS sensor_ts ON sensor(ts);"
    "CREATE INDEX IF NOT EXISTS inspection_ts ON inspection(ts);"
    "CREATE INDEX IF NOT EXISTS alarm_ts ON alarm(ts);";

#define DB(s) ((sqlite3 *)(s)->db)

static int has_column(sqlite3 *db, const char *table, const char *column)
{
    char sql[64];
    sqlite3_stmt *q;
    int found = 0;
    snprintf(sql, sizeof sql, "PRAGMA table_info(%s)", table);
    if (sqlite3_prepare_v2(db, sql, -1, &q, NULL) != SQLITE_OK)
        return -1;
    while (sqlite3_step(q) == SQLITE_ROW)
        if (!strcmp((const char *)sqlite3_column_text(q, 1), column))
            found = 1;
    sqlite3_finalize(q);
    return found;
}

/* The DHT11 version stored humidity NOT NULL. SQLite 3.7 cannot drop a
 * column, so the old table is kept, renamed, and a new one is created.
 */
static int keep_dht11_table(sqlite3 *db)
{
    int old = has_column(db, "sensor", "humidity");
    if (old <= 0)
        return old;
    return sqlite3_exec(db, "ALTER TABLE sensor RENAME TO sensor_dht11;"
                        "DROP INDEX IF EXISTS sensor_ts;", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

/* Databases created before the session column existed get it added. */
static int add_session_column(sqlite3 *db)
{
    int found = has_column(db, "inspection", "session");
    if (found < 0)
        return -1;
    if (!found &&
        sqlite3_exec(db, "ALTER TABLE inspection ADD COLUMN session INTEGER", NULL, NULL, NULL) != SQLITE_OK)
        return -1;
    return sqlite3_exec(db, "CREATE INDEX IF NOT EXISTS inspection_item ON inspection(session, seq)",
                        NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int prepare(struct store *s, const char *sql, void **stmt)
{
    return sqlite3_prepare_v2(DB(s), sql, -1, (sqlite3_stmt **)stmt, NULL);
}

int store_open(struct store *s, const char *path)
{
    sqlite3 *db = NULL;
    memset(s, 0, sizeof *s);
    if (sqlite3_open(path, &db) != SQLITE_OK) {
        s->db = db;     /* keeps the error message readable */
        return -1;
    }
    s->db = db;
    /* Another reader (sqlite3 shell) may briefly hold the lock. */
    sqlite3_busy_timeout(db, 2000);
    if (keep_dht11_table(db) ||
        sqlite3_exec(db, schema, NULL, NULL, NULL) != SQLITE_OK ||
        add_session_column(db) ||
        prepare(s, "INSERT INTO sensor(ts,temperature) VALUES(?,?)",
                &s->sensor) != SQLITE_OK ||
        prepare(s, "INSERT INTO inspection(ts,seq,result,confidence,session) VALUES(?,?,?,?,?)",
                &s->inspect) != SQLITE_OK ||
        prepare(s, "INSERT INTO alarm(ts,kind,state,value) VALUES(?,?,?,?)",
                &s->alarm) != SQLITE_OK)
        return -1;
    return 0;
}

/* SQLite's autocommit flag is the truth: some errors roll back on their own. */
static int begin(struct store *s)
{
    if (!sqlite3_get_autocommit(DB(s)))
        return 0;
    s->pending = 0;
    return sqlite3_exec(DB(s), "BEGIN", NULL, NULL, NULL) == SQLITE_OK ? 0 : -1;
}

static int run(struct store *s, void *stmt)
{
    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    if (rc != SQLITE_DONE)
        return -1;
    s->pending++;
    return 0;
}

int store_sensor(struct store *s, time_t ts, int32_t t)
{
    sqlite3_stmt *q = s->sensor;
    if (!q || begin(s))
        return -1;
    sqlite3_bind_int64(q, 1, ts);
    sqlite3_bind_double(q, 2, t / 1000.0);
    return run(s, q);
}

int store_inspect(struct store *s, time_t ts, int session, uint32_t seq,
                  int result, int confidence)
{
    sqlite3_stmt *q = s->inspect;
    if (!q || begin(s))
        return -1;
    sqlite3_bind_int64(q, 1, ts);
    sqlite3_bind_int64(q, 2, seq);
    sqlite3_bind_text(q, 3, result ? "DEFECT" : "NORMAL", -1, SQLITE_STATIC);
    if (confidence >= 0)
        sqlite3_bind_int(q, 4, confidence);
    else
        sqlite3_bind_null(q, 4);
    if (session)
        sqlite3_bind_int(q, 5, session);
    else
        sqlite3_bind_null(q, 5);
    return run(s, q);
}

int store_alarm(struct store *s, time_t ts, const char *kind, int active,
                double value)
{
    sqlite3_stmt *q = s->alarm;
    if (!q || begin(s))
        return -1;
    sqlite3_bind_int64(q, 1, ts);
    sqlite3_bind_text(q, 2, kind, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(q, 3, active ? "ON" : "OFF", -1, SQLITE_STATIC);
    sqlite3_bind_double(q, 4, value);
    return run(s, q);
}

int store_commit(struct store *s)
{
    if (!s->db || sqlite3_get_autocommit(DB(s)))
        return 0;
    if (sqlite3_exec(DB(s), "COMMIT", NULL, NULL, NULL) != SQLITE_OK)
        return -1;
    s->pending = 0;
    return 0;
}

long store_prune(struct store *s, time_t before)
{
    static const char *const tables[] = { "sensor", "inspection", "alarm" };
    long removed = 0;
    unsigned int i;
    if (store_commit(s))
        return -1;
    for (i = 0; i < 3; i++) {
        char sql[64];
        sqlite3_stmt *q;
        strcpy(sql, "DELETE FROM ");
        strcat(sql, tables[i]);
        strcat(sql, " WHERE ts<?");
        if (sqlite3_prepare_v2(DB(s), sql, -1, &q, NULL) != SQLITE_OK)
            return -1;
        sqlite3_bind_int64(q, 1, before);
        if (sqlite3_step(q) != SQLITE_DONE) {
            sqlite3_finalize(q);
            return -1;
        }
        sqlite3_finalize(q);
        removed += sqlite3_changes(DB(s));
    }
    return removed;
}

const char *store_error(const struct store *s)
{
    return s->db ? sqlite3_errmsg(DB(s)) : "not open";
}

void store_close(struct store *s)
{
    if (!s->db)
        return;
    /* Uncommitted rows are rolled back by sqlite3_close. */
    store_commit(s);
    sqlite3_finalize(s->sensor);
    sqlite3_finalize(s->inspect);
    sqlite3_finalize(s->alarm);
    sqlite3_close(DB(s));
    memset(s, 0, sizeof *s);
}

#ifndef STORE_H
#define STORE_H
#include <stdint.h>
#include <time.h>

/* SQLite record store, used only by the db thread. Inserts are grouped in
 * a transaction until store_commit() to limit flash/NFS writes.
 * Tables: sensor (LM75 temperature), inspection, alarm; ts is Unix seconds
 * (UTC). A sensor table from the DHT11 version is kept as sensor_dht11.
 */
struct store {
    void *db, *sensor, *inspect, *alarm;
    unsigned int pending;     /* rows in the open transaction */
};

int store_open(struct store *s, const char *path);
int store_sensor(struct store *s, time_t ts, int32_t temperature_mc);
/* result 0 normal, 1 defect; session 0 and confidence -1 are stored as
 * NULL. An item is identified by (session, seq).
 */
int store_inspect(struct store *s, time_t ts, int session, uint32_t seq,
                  int result, int confidence);
int store_alarm(struct store *s, time_t ts, const char *kind, int active,
                double value);
int store_commit(struct store *s);
/* Deletes rows older than before; returns rows removed or -1. */
long store_prune(struct store *s, time_t before);
const char *store_error(const struct store *s);
void store_close(struct store *s);
#endif

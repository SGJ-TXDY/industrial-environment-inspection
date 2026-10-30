#ifndef NMEA_H
#define NMEA_H
struct gps_data {
    double latitude, longitude, speed_kmh, altitude, hdop;
    double rmc_at, gga_at;
    int rmc_seen, gga_seen, fix, quality, satellites;
    char utc[16], date[12];
    unsigned long sentences, rejected;
};
/* Complete NMEA line with mandatory checksum. Returns 1 for supported data. */
int nmea_parse(struct gps_data *g, const char *line, double now);
int gps_has_fix(const struct gps_data *g, double now);
#endif

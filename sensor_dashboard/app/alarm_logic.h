#ifndef ALARM_LOGIC_H
#define ALARM_LOGIC_H
#include <stdint.h>

#define ALARM_OUT_LED 1u
#define ALARM_OUT_BUZZER 2u

enum alarm_kind { ALARM_NONE, ALARM_TEMP_HIGH, ALARM_TEMP_LOW, ALARM_DEFECT };

struct alarm_config {
    int32_t temp_high_mc, temp_low_mc, hysteresis_mc;
    unsigned int defect_ms;
    int buzzer;                 /* 0 keeps the LED only */
};

struct alarm_state {
    int temp;                   /* ALARM_NONE, ALARM_TEMP_HIGH or ALARM_TEMP_LOW */
    double temp_since, defect_until;
};

void alarm_config_default(struct alarm_config *c);
/* Returns 1 when the temperature alarm state changed. */
int alarm_temperature(struct alarm_state *s, const struct alarm_config *c,
                      int32_t temperature_mc, double now);
void alarm_defect(struct alarm_state *s, const struct alarm_config *c, double now);
/* Defect: LED and buzzer steady for defect_ms, retriggered by each defect.
 * Temperature: LED steady, buzzer 200 ms every 2 s while abnormal.
 */
unsigned int alarm_outputs(const struct alarm_state *s,
                           const struct alarm_config *c, double now);
const char *alarm_kind_name(int kind);
#endif

#include "alarm_logic.h"
#include <math.h>

void alarm_config_default(struct alarm_config *c)
{
    c->temp_high_mc = 40000;
    c->temp_low_mc = 0;
    c->hysteresis_mc = 1000;
    c->defect_ms = 1000;
    c->buzzer = 1;
}

int alarm_temperature(struct alarm_state *s, const struct alarm_config *c,
                      int32_t t, double now)
{
    int next = s->temp;
    /* Hysteresis keeps a reading hovering at the limit from toggling. */
    if (s->temp == ALARM_TEMP_HIGH && t <= c->temp_high_mc - c->hysteresis_mc)
        next = ALARM_NONE;
    if (s->temp == ALARM_TEMP_LOW && t >= c->temp_low_mc + c->hysteresis_mc)
        next = ALARM_NONE;
    if (t > c->temp_high_mc)
        next = ALARM_TEMP_HIGH;
    else if (t < c->temp_low_mc)
        next = ALARM_TEMP_LOW;
    if (next == s->temp)
        return 0;
    s->temp = next;
    s->temp_since = now;
    return 1;
}

void alarm_defect(struct alarm_state *s, const struct alarm_config *c, double now)
{
    s->defect_until = now + c->defect_ms / 1000.0;
}

unsigned int alarm_outputs(const struct alarm_state *s,
                           const struct alarm_config *c, double now)
{
    unsigned int out = 0;
    if (now < s->defect_until)
        out = ALARM_OUT_LED | ALARM_OUT_BUZZER;
    if (s->temp != ALARM_NONE) {
        out |= ALARM_OUT_LED;
        if (fmod(now - s->temp_since, 2.0) < 0.2)
            out |= ALARM_OUT_BUZZER;
    }
    if (!c->buzzer)
        out &= ~ALARM_OUT_BUZZER;
    return out;
}

const char *alarm_kind_name(int kind)
{
    switch (kind) {
    case ALARM_TEMP_HIGH: return "TEMP_HIGH";
    case ALARM_TEMP_LOW: return "TEMP_LOW";
    case ALARM_DEFECT: return "DEFECT";
    default: return "NONE";
    }
}

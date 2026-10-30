#ifndef ENV_SENSOR_LOGIC_H
#define ENV_SENSOR_LOGIC_H
#include "sensor_data.h"

struct attitude {
    double x_g, y_g, z_g;
    double roll_deg, pitch_deg;
    int valid;
    const char *direction;
};
void attitude_compute(double x, double y, double z, struct attitude *out);

struct dashboard_data {
    struct env_temp_sample temp;      /* LM75BDP */
    struct attitude attitude;
    int temp_valid, accel_valid, temp_error, accel_error;
    double temp_age, accel_age;
    unsigned long frame;
};
#endif

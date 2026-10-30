#include "sensor_logic.h"
#include <math.h>

void attitude_compute(double x, double y, double z, struct attitude *out)
{
    const double degrees = 57.29577951308232;
    double norm = sqrt(x*x + y*y + z*z);
    out->x_g = x;
    out->y_g = y;
    out->z_g = z;
    /* Acceleration significantly different from gravity invalidates tilt. */
    out->valid = isfinite(norm) && norm >= 0.75 && norm <= 1.25;
    out->roll_deg = 0;
    out->pitch_deg = 0;
    out->direction = "MOVING / TILT INVALID";
    if (!out->valid)
        return;
    out->roll_deg = atan2(y, z) * degrees;
    out->pitch_deg = atan2(-x, sqrt(y*y + z*z)) * degrees;
    if (z < -0.75)
        out->direction = "UPSIDE DOWN";
    else if (fabs(out->roll_deg) < 10 && fabs(out->pitch_deg) < 10)
        out->direction = "LEVEL";
    else if (fabs(out->roll_deg) >= fabs(out->pitch_deg))
        out->direction = out->roll_deg >= 0 ? "TILT +Y" : "TILT -Y";
    else
        out->direction = out->pitch_deg >= 0 ? "TILT -X" : "TILT +X";
}

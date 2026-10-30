#include "ui_tick.h"
#include <time.h>

uint32_t ui_tick_ms(void)
{
    /* Monotonic, unlike gettimeofday in the port example: setting the
     * board clock must not stall LVGL timers. Wraps after ~49 days, which
     * LVGL handles with lv_tick_elaps().
     */
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)((uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u);
}

#ifndef UI_H
#define UI_H
#include "graphics.h"
#include "sensor_logic.h"
#include "nmea.h"
#include "inspect.h"
#include "services.h"
struct ui_data {
    struct dashboard_data sensors;
    struct gps_data gps;
    struct inspect_data inspect;
    struct service_status services;   /* alarm and database threads */
    double now, inspect_now;   /* inspect_now keeps running while paused */
    int paused, touch_ok, gps_open, page, can_state;
};

/* LVGL screen (ui_lvgl.c). lv_init() and a display must exist first. */
#define UI_ACTION_PAUSE 1u
#define UI_ACTION_PAGE 2u
#define UI_ACTION_UPLOAD_PAUSE 4u       /* pause/resume MQTT upload */
#define UI_ACTION_UPLOAD_RECONNECT 8u
void ui_lvgl_create(void);
/* Updates widgets; LVGL redraws only what changed in lv_timer_handler(). */
void ui_lvgl_update(const struct ui_data *d);
/* Button presses since the last call, UI_ACTION_* bits. */
unsigned int ui_lvgl_take_actions(void);
/* Copies LVGL pixels (LV_COLOR_DEPTH 32, 0xAARRGGBB) of the inclusive
 * area x1..x2, y1..y2 into any surface format.
 */
void ui_lvgl_blit(struct surface *s, int x1, int y1, int x2, int y2,
                  const uint32_t *pixels);
#endif

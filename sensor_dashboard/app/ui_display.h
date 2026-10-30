#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H
/* Linux side of the LVGL port: framebuffer flush and GT9147 touch input.
 * Replaces lv_drivers fbdev/evdev so the touch device is still found by
 * name (env-gt9147) and TOUCH_SWAP_XY/INVERT_* keep working.
 */
int ui_display_open(void);          /* lv_init, display, touch, screen */
void ui_display_retry_touch(void);  /* reopen touch after unplug/reload */
int ui_display_touch_ok(void);
void ui_display_close(void);
#endif

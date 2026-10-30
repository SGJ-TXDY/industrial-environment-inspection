/* Host test for the LVGL screen: renders sample states into memory
 * framebuffers (16/24/32 bpp, padded rows, y offset) as PPM files, checks
 * that every character shown has a glyph, and clicks both buttons through
 * a virtual pointer. Usage: test-ui [output-dir]
 */
#include "ui.h"
#include "alarm_logic.h"
#include "lvgl.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct surface target;
static lv_indev_state_t touch_state = LV_INDEV_STATE_RELEASED;
static lv_point_t touch_point;
static int missing;

static void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *pixels)
{
    ui_lvgl_blit(&target, a->x1, a->y1, a->x2, a->y2, (const uint32_t *)pixels);
    lv_disp_flush_ready(drv);
}

static void pointer(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    data->point = touch_point;
    data->state = touch_state;
}

static void make_surface(int bits)
{
    memset(&target, 0, sizeof target);
    target.width = 800;
    target.height = 480;
    target.bpp = bits;
    target.stride = 800 * (bits / 8) + 16;
    target.yoffset = 1;
    target.length = target.stride * 481;
    target.pixels = calloc(1, target.length);
    target.red = (struct color_field){ bits == 16 ? 11 : 16, bits == 16 ? 5 : 8 };
    target.green = (struct color_field){ bits == 16 ? 5 : 8, bits == 16 ? 6 : 8 };
    target.blue = (struct color_field){ 0, bits == 16 ? 5 : 8 };
    assert(target.pixels && !surface_validate(&target));
}

static void render(const char *dir, const char *mode, int bits)
{
    char name[512];
    make_surface(bits);
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    snprintf(name, sizeof name, "%s/ui-%s-%d.ppm", dir, mode, bits);
    assert(!surface_ppm(&target, name));
    free(target.pixels);
}

/* Every letter of every visible label must resolve to a glyph. */
static void check_glyphs(lv_obj_t *obj)
{
    uint32_t i, n = lv_obj_get_child_cnt(obj);
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const lv_font_t *font = lv_obj_get_style_text_font(obj, LV_PART_MAIN);
        const char *text = lv_label_get_text(obj);
        uint32_t pos = 0, letter;
        lv_font_glyph_dsc_t g;
        while ((letter = _lv_txt_encoded_next(text, &pos)) != 0) {
            if (letter != '\n' && !lv_font_get_glyph_dsc(font, &g, letter, 0)) {
                fprintf(stderr, "missing glyph U+%04X in \"%s\"\n", (unsigned int)letter, text);
                missing++;
            }
        }
    }
    for (i = 0; i < n; i++)
        check_glyphs(lv_obj_get_child(obj, i));
}

static void sample(struct ui_data *d, const char *mode)
{
    memset(d, 0, sizeof *d);
    d->now = d->inspect_now = 10;
    d->touch_ok = d->gps_open = 1;
    d->sensors.temp_valid = d->sensors.accel_valid = 1;
    d->sensors.temp.temperature_mc = 25375;
    d->sensors.temp_age = 0.4;
    attitude_compute(.12, .08, .99, &d->sensors.attitude);
    d->gps.rmc_at = d->gps.gga_at = 10;
    d->gps.rmc_seen = d->gps.gga_seen = d->gps.fix = d->gps.quality = 1;
    d->gps.latitude = 31.230416;
    d->gps.longitude = 121.473701;
    d->gps.satellites = 12;
    d->gps.hdop = .8;
    d->gps.speed_kmh = .2;
    d->gps.altitude = 12.3;
    d->gps.sentences = 4210;
    strcpy(d->gps.utc, "082530.00");
    strcpy(d->gps.date, "301026");
    d->can_state = BUS_ACTIVE;
    d->inspect.result_seen = d->inspect.heartbeat_seen = 1;
    d->inspect.result = INSPECT_NORMAL;
    d->inspect.confidence = 97;
    d->inspect.seq = 1284;
    d->inspect.session = d->inspect.stm32_session = 37;
    d->inspect.result_at = 9.6;
    d->inspect.heartbeat_at = 9.8;
    d->inspect.stm32_total = 1284;
    d->inspect.normal_count = 1251;
    d->inspect.defect_count = 33;
    d->services.running = d->services.alarm_device_ok = d->services.db_ok = 1;
    d->services.db_rows = 3021;
    d->services.alarm_low_mc = 0;
    d->services.alarm_high_mc = 40000;
    d->services.upload.state = UPLOAD_CONNECTED;
    d->services.upload.qos = 1;
    strcpy(d->services.upload.server, "tcp://192.168.1.10:1883");
    strcpy(d->services.upload.client_id, "imx6ull-board");
    strcpy(d->services.upload.prefix, "imx6ull/sensor-dashboard");
    d->services.upload.sent = 1532;
    d->services.upload.backlog_max = 200;
    d->services.upload.connects = 1;
    d->services.upload.connected_at = 10 - 3723;
    {
        static const int types[] = { MAIL_INSPECT, MAIL_SENSOR, MAIL_ALARM, MAIL_INSPECT, -1 };
        unsigned int i;
        for (i = 0; i < UPLOAD_RECENT; i++) {
            struct upload_record *r = &d->services.upload.recent[i];
            r->ok = 1;
            r->msg.type = types[i];
            r->msg.wall = 1761811200 - (time_t)i * 7;
            r->msg.u.inspect.seq = 1284 - i;
            r->msg.u.inspect.session = 37;
            r->msg.u.inspect.result = i == 3;
        }
        d->services.upload.recent[1].msg.u.sensor.temperature_mc = 25375;
        d->services.upload.recent[2].msg.u.alarm.kind = ALARM_TEMP_HIGH;
        d->services.upload.recent[2].msg.u.alarm.active = 1;
        d->services.upload.recent[2].msg.u.alarm.value = 41200;
        d->services.upload.recent_count = UPLOAD_RECENT;
    }
    if (!strcmp(mode, "defect")) {
        d->inspect.result = INSPECT_DEFECT;
        d->inspect.confidence = -1;
        d->inspect.missed = 2;
        d->services.defect_active = 1;
        d->services.outputs = ALARM_OUT_LED | ALARM_OUT_BUZZER;
        d->page = 1;
    } else if (!strcmp(mode, "alarm")) {
        d->sensors.temp.temperature_mc = 41250;
        d->services.alarm_temp = ALARM_TEMP_HIGH;
        d->services.alarm_temperature_mc = 41250;
        d->services.outputs = ALARM_OUT_LED | ALARM_OUT_BUZZER;
        d->paused = 1;
    } else if (!strcmp(mode, "offline")) {
        d->services.upload.state = UPLOAD_WAITING;
        d->services.upload.error = -1;
        d->services.upload.retry_at = 18;
        d->services.upload.backlog = 57;
        d->services.upload.failed = 3;
        d->services.upload.recent[0].ok = 0;
    } else if (!strcmp(mode, "error")) {
        memset(d, 0, sizeof *d);
        d->now = d->inspect_now = 10;
        d->sensors.temp_error = 121;   /* EREMOTEIO: no ACK on I2C */
        d->sensors.accel_error = 19;
        d->can_state = BUS_CLOSED;
        d->services.upload.state = UPLOAD_UNCONFIGURED;
        d->services.upload.backlog_max = 200;
        strcpy(d->services.upload.client_id, "imx6ull-board");
        strcpy(d->services.upload.prefix, "imx6ull/sensor-dashboard");
    }
}

static void run_ms(int ms)
{
    for (; ms > 0; ms -= 5) {
        lv_timer_handler();
        usleep(5000);
    }
}

static void click(int x, int y)
{
    touch_point.x = (lv_coord_t)x;
    touch_point.y = (lv_coord_t)y;
    touch_state = LV_INDEV_STATE_PRESSED;
    run_ms(120);
    touch_state = LV_INDEV_STATE_RELEASED;
    run_ms(120);
}

int main(int argc, char **argv)
{
    static lv_color_t pixels[800 * 60];
    static lv_disp_draw_buf_t buf;
    static lv_disp_drv_t disp;
    static lv_indev_drv_t indev;
    const char *dir = argc > 1 ? argv[1] : ".";
    const char *modes[] = { "normal", "defect", "alarm", "error", "offline" };
    struct ui_data d;
    unsigned int i;
    char name[32];
    lv_obj_t *main;

    lv_init();
    lv_disp_draw_buf_init(&buf, pixels, NULL, 800 * 60);
    lv_disp_drv_init(&disp);
    disp.draw_buf = &buf;
    disp.flush_cb = flush;
    disp.hor_res = 800;
    disp.ver_res = 480;
    lv_disp_drv_register(&disp);
    lv_indev_drv_init(&indev);
    indev.type = LV_INDEV_TYPE_POINTER;
    indev.read_cb = pointer;
    lv_indev_drv_register(&indev);
    make_surface(32);
    ui_lvgl_create();
    main = lv_scr_act();

    for (i = 0; i < 5; i++) {
        sample(&d, modes[i]);
        ui_lvgl_update(&d);
        check_glyphs(lv_scr_act());
        render(dir, modes[i], 32);
    }
    sample(&d, "normal");
    ui_lvgl_update(&d);
    render(dir, "normal", 16);
    render(dir, "normal", 24);
    assert(!missing);
    puts("PASS: rendered normal/defect/alarm/error/offline at 32 bpp, normal at 16/24 bpp; all glyphs present");

    make_surface(32);
    ui_lvgl_take_actions();
    click(136, 445);
    assert(ui_lvgl_take_actions() == UI_ACTION_PAUSE);
    click(400, 445);
    assert(ui_lvgl_take_actions() == UI_ACTION_PAGE);
    click(400, 300);      /* a card, not a button */
    assert(ui_lvgl_take_actions() == 0);
    puts("PASS: pause and page buttons respond to pointer clicks");

    /* Upload screen: open, render states, its buttons, back. */
    click(664, 445);
    assert(lv_scr_act() != main && ui_lvgl_take_actions() == 0);
    for (i = 0; i < 3; i++) {
        static const char *const upload_modes[] = { "normal", "offline", "error" };
        sample(&d, upload_modes[i]);
        ui_lvgl_update(&d);
        check_glyphs(lv_scr_act());
        snprintf(name, sizeof name, "upload-%s", upload_modes[i]);
        render(dir, name, 32);
    }
    assert(!missing);
    make_surface(32);
    click(136, 445);
    assert(ui_lvgl_take_actions() == UI_ACTION_UPLOAD_PAUSE);
    click(400, 445);
    assert(ui_lvgl_take_actions() == UI_ACTION_UPLOAD_RECONNECT);
    click(664, 445);
    assert(lv_scr_act() == main && ui_lvgl_take_actions() == 0);
    free(target.pixels);
    puts("PASS: upload screen renders connected/offline/unconfigured, its buttons work, back returns");
    return 0;
}

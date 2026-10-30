/* LVGL v8.4 screens, 800x480: dashboard and MQTT upload. Chinese text uses the Noto Sans SC
 * subsets in app/fonts: after changing any string literal here, re-run
 * tools/gen_lv_font.py so the new characters are included.
 */
#include "ui.h"
#include "alarm_logic.h"
#include "lvgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef char lv_color_is_32bit[sizeof(lv_color_t) == 4 ? 1 : -1];

#define C_BG 0x0b1324
#define C_CARD 0x17243a
#define C_BORDER 0x2a3b57
#define C_ALERT 0x5a1f2a
#define C_FG 0xe7efff
#define C_DIM 0x8fa3c0
#define C_OK 0x54dfba
#define C_WARN 0xffc369
#define C_BAD 0xff6b6b

static struct {
    lv_obj_t *clock, *alarm, *db, *touch;
    lv_obj_t *temp, *temp_exact, *temp_bar, *temp_range, *temp_status;
    lv_obj_t *att_title, *att, *att_status;
    lv_obj_t *gps_status, *gps[7];
    lv_obj_t *insp_card, *result, *seq, *age, *normal, *defect, *rate_bar, *rate, *stats, *link, *stm32;
    lv_obj_t *pause_label, *page_label, *upload;
} w;
static struct {
    lv_obj_t *clock, *state, *server, *client, *since, *error;
    lv_obj_t *sent, *failed, *backlog, *backlog_bar, *dropped, *connects, *topics;
    lv_obj_t *recent[UPLOAD_RECENT], *pause_label;
} u;
static lv_obj_t *main_screen, *upload_screen;
static unsigned int actions;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                       int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    return l;
}

static lv_obj_t *card(lv_obj_t *screen, int x, int y, int width, int height,
                      const char *title)
{
    lv_obj_t *c = lv_obj_create(screen);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, width, height);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(c, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(c, 1, 0);
    label(c, &ui_font_cn_16, C_DIM, 14, 8, title);
    return c;
}

/* Only touch widgets whose content changed, so LVGL redraws little. */
static void set_text(lv_obj_t *l, const char *text)
{
    if (strcmp(lv_label_get_text(l), text))
        lv_label_set_text(l, text);
}

static void set_color(lv_obj_t *l, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if (lv_obj_get_style_text_color(l, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_text_color(l, c, 0);
}

static void set_font(lv_obj_t *l, const lv_font_t *font)
{
    if (lv_obj_get_style_text_font(l, LV_PART_MAIN) != font)
        lv_obj_set_style_text_font(l, font, 0);
}

static void set_bg(lv_obj_t *obj, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if (lv_obj_get_style_bg_color(obj, LV_PART_MAIN).full != c.full)
        lv_obj_set_style_bg_color(obj, c, 0);
}

static void set_bar_color(lv_obj_t *bar, uint32_t color)
{
    lv_color_t c = lv_color_hex(color);
    if (lv_obj_get_style_bg_color(bar, LV_PART_INDICATOR).full != c.full)
        lv_obj_set_style_bg_color(bar, c, LV_PART_INDICATOR);
}

static void set_bar(lv_obj_t *bar, int value)
{
    if (lv_bar_get_value(bar) != value)
        lv_bar_set_value(bar, value, LV_ANIM_OFF);
}

static lv_obj_t *bar(lv_obj_t *parent, int x, int y, int width, uint32_t color)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, width, 8);
    lv_bar_set_range(b, 0, 1000);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), LV_PART_INDICATOR);
    return b;
}

static void clicked(lv_event_t *e)
{
    actions |= (unsigned int)(uintptr_t)lv_event_get_user_data(e);
}

/* Switches screens inside the UI; the main loop never sees it. */
static void show_screen(lv_event_t *e)
{
    lv_scr_load(lv_event_get_user_data(e));
}

static lv_obj_t *button(lv_obj_t *screen, int x, int y, int width, unsigned int action,
                        uint32_t color)
{
    lv_obj_t *b = lv_btn_create(screen);
    lv_obj_t *l;
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, width, 54);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_add_event_cb(b, clicked, LV_EVENT_CLICKED, (void *)(uintptr_t)action);
    l = label(b, &ui_font_cn_20, C_FG, 0, 0, "");
    lv_obj_center(l);
    return l;
}

static void create_upload_screen(void)
{
    lv_obj_t *scr = upload_screen = lv_obj_create(NULL), *c, *b;
    int i;
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    label(scr, &ui_font_cn_20, C_FG, 16, 6, "数据上传  MQTT");
    label(scr, &ui_font_cn_16, C_DIM, 16, 34, "传感器、检测结果和报警经 MQTT 发布；断线时暂存，恢复后按顺序补发");
    u.clock = label(scr, &ui_font_cn_20, C_FG, 0, 0, "--:--:--");
    lv_obj_align(u.clock, LV_ALIGN_TOP_RIGHT, -14, 6);

    c = card(scr, 8, 62, 384, 200, "连接状态");
    u.state = label(c, &ui_font_cn_32, C_WARN, 14, 30, "");
    u.server = label(c, &ui_font_cn_16, C_FG, 14, 82, "");
    u.client = label(c, &ui_font_cn_16, C_FG, 14, 106, "");
    u.since = label(c, &ui_font_cn_16, C_DIM, 14, 136, "");
    u.error = label(c, &ui_font_cn_16, C_WARN, 14, 162, "");
    for (i = 0; i < 4; i++)
        lv_obj_set_width(i == 0 ? u.server : i == 1 ? u.client : i == 2 ? u.since : u.error, 356);
    lv_label_set_long_mode(u.server, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(u.client, LV_LABEL_LONG_CLIP);

    c = card(scr, 400, 62, 392, 200, "上传统计");
    u.sent = label(c, &ui_font_cn_20, C_OK, 14, 32, "");
    u.failed = label(c, &ui_font_cn_20, C_FG, 200, 32, "");
    u.backlog = label(c, &ui_font_cn_16, C_FG, 14, 72, "");
    u.backlog_bar = bar(c, 14, 98, 362, C_WARN);
    u.dropped = label(c, &ui_font_cn_16, C_FG, 14, 114, "");
    u.connects = label(c, &ui_font_cn_16, C_FG, 200, 114, "");
    u.topics = label(c, &ui_font_cn_16, C_DIM, 14, 144, "");
    lv_obj_set_width(u.topics, 364);
    lv_label_set_long_mode(u.topics, LV_LABEL_LONG_WRAP);

    c = card(scr, 8, 270, 784, 140, "最近上传");
    for (i = 0; i < UPLOAD_RECENT; i++)
        u.recent[i] = label(c, &ui_font_cn_16, C_FG, 14, 30 + i * 21, "");

    u.pause_label = button(scr, 8, 418, 256, UI_ACTION_UPLOAD_PAUSE, 0x235b61);
    b = lv_obj_get_parent(button(scr, 272, 418, 256, UI_ACTION_UPLOAD_RECONNECT, 0x293c61));
    lv_label_set_text(lv_obj_get_child(b, 0), LV_SYMBOL_REFRESH " 立即重连");
    b = lv_obj_get_parent(button(scr, 536, 418, 256, 0, 0x3a2f61));
    lv_label_set_text(lv_obj_get_child(b, 0), LV_SYMBOL_LEFT " 返回主界面");
    lv_obj_add_event_cb(b, show_screen, LV_EVENT_CLICKED, main_screen);
}

void ui_lvgl_create(void)
{
    lv_obj_t *scr = main_screen = lv_scr_act(), *c, *b;
    int i;
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    label(scr, &ui_font_cn_20, C_FG, 16, 6, "产线监测终端");
    w.upload = label(scr, &ui_font_cn_16, C_DIM, 16, 34, "");
    lv_obj_set_width(w.upload, 408);
    lv_label_set_long_mode(w.upload, LV_LABEL_LONG_CLIP);
    w.touch = label(scr, &ui_font_cn_16, C_WARN, 150, 10, "");
    /* CLIP, not DOT: DOT rewrites the text and would defeat set_text(). */
    w.alarm = label(scr, &ui_font_cn_16, C_OK, 250, 10, "");
    lv_obj_set_width(w.alarm, 440);
    lv_label_set_long_mode(w.alarm, LV_LABEL_LONG_CLIP);
    w.db = label(scr, &ui_font_cn_16, C_DIM, 432, 34, "");
    lv_obj_set_width(w.db, 356);
    lv_label_set_long_mode(w.db, LV_LABEL_LONG_CLIP);
    w.clock = label(scr, &ui_font_cn_20, C_FG, 0, 0, "--:--:--");
    lv_obj_align(w.clock, LV_ALIGN_TOP_RIGHT, -14, 6);

    c = card(scr, 8, 62, 250, 170, "温度  LM75BD");
    w.temp = label(c, &ui_font_cn_32, C_FG, 14, 30, "--");
    w.temp_exact = label(c, &ui_font_cn_16, C_DIM, 14, 76, "");
    w.temp_bar = bar(c, 14, 100, 220, 0x4aa3ff);
    w.temp_range = label(c, &ui_font_cn_16, C_DIM, 14, 112, "");
    w.temp_status = label(c, &ui_font_cn_16, C_DIM, 14, 136, "");

    c = card(scr, 266, 62, 250, 170, "");
    w.att_title = lv_obj_get_child(c, 0);
    w.att = label(c, &ui_font_cn_20, C_FG, 14, 30, "--");
    lv_obj_set_style_text_line_space(w.att, 4, 0);
    w.att_status = label(c, &ui_font_cn_16, C_DIM, 14, 136, "");

    c = card(scr, 8, 240, 508, 170, "GPS 定位  ATGM336H");
    w.gps_status = label(c, &ui_font_cn_20, C_WARN, 14, 30, "");
    for (i = 0; i < 6; i++)
        w.gps[i] = label(c, &ui_font_cn_16, C_FG, i % 2 ? 260 : 14, 64 + (i / 2) * 26, "");
    w.gps[6] = label(c, &ui_font_cn_16, C_DIM, 14, 142, "");

    w.insp_card = c = card(scr, 524, 62, 268, 348, "STM32N647 检测结果  CAN");
    w.result = label(c, &ui_font_cn_32, C_WARN, 14, 30, "");
    w.seq = label(c, &ui_font_cn_16, C_FG, 14, 80, "");
    w.age = label(c, &ui_font_cn_16, C_DIM, 14, 104, "");
    w.normal = label(c, &ui_font_cn_20, C_OK, 14, 136, "");
    w.defect = label(c, &ui_font_cn_20, C_BAD, 14, 166, "");
    w.rate_bar = bar(c, 14, 204, 238, C_BAD);
    w.rate = label(c, &ui_font_cn_16, C_FG, 14, 220, "");
    w.stats = label(c, &ui_font_cn_16, C_DIM, 14, 250, "");
    w.link = label(c, &ui_font_cn_16, C_OK, 14, 284, "");
    w.stm32 = label(c, &ui_font_cn_16, C_OK, 14, 310, "");

    w.pause_label = button(scr, 8, 418, 256, UI_ACTION_PAUSE, 0x235b61);
    w.page_label = button(scr, 272, 418, 256, UI_ACTION_PAGE, 0x293c61);
    create_upload_screen();
    b = lv_obj_get_parent(button(scr, 536, 418, 256, 0, 0x3a2f61));
    lv_label_set_text(lv_obj_get_child(b, 0), "数据上传 " LV_SYMBOL_UPLOAD);
    lv_obj_add_event_cb(b, show_screen, LV_EVENT_CLICKED, upload_screen);
}

static const char *stm32_state(int state)
{
    switch (state) {
    case 0: return "运行中";
    case 1: return "空闲";
    case 2: return "摄像头故障";
    case 3: return "模型故障";
    default: return "未知状态";
    }
}

static void update_header(const struct ui_data *d)
{
    const struct service_status *v = &d->services;
    char b[128];
    time_t now = time(NULL);
    struct tm tm;
    uint32_t color;
    localtime_r(&now, &tm);
    strftime(b, sizeof b, "%H:%M:%S", &tm);
    set_text(w.clock, b);
    set_text(w.touch, d->touch_ok ? "" : "触摸离线");
    if (!v->running) {
        set_text(w.alarm, "报警/数据库线程未运行");
        set_color(w.alarm, C_WARN);
        set_text(w.db, "");
        return;
    }
    if (v->alarm_temp)
        snprintf(b, sizeof b, "报警：%s %.1f °C", v->alarm_temp == ALARM_TEMP_HIGH ? "高温" : "低温",
                 v->alarm_temperature_mc / 1000.0);
    else if (v->defect_active)
        snprintf(b, sizeof b, "报警：残次品");
    else
        snprintf(b, sizeof b, "无报警");
    if (v->outputs)
        strncat(b, v->outputs & ALARM_OUT_BUZZER ? "  灯+蜂鸣器" : "  灯", sizeof b - strlen(b) - 1);
    if (!v->alarm_device_ok)
        strncat(b, "  无报警设备", sizeof b - strlen(b) - 1);
    color = v->alarm_temp || v->defect_active ? C_BAD : v->alarm_device_ok ? C_OK : C_WARN;
    set_text(w.alarm, b);
    set_color(w.alarm, color);
    snprintf(b, sizeof b, "数据库 %s  已写 %lu  失败 %lu  丢弃 %lu", v->db_ok ? "正常" : "错误",
             v->db_rows, v->db_errors, v->mail_dropped);
    set_text(w.db, b);
    set_color(w.db, v->db_ok && !v->db_errors && !v->mail_dropped ? C_DIM : C_WARN);
}

static void update_sensors(const struct ui_data *d)
{
    const struct dashboard_data *v = &d->sensors;
    char b[96];
    const struct service_status *sv = &d->services;
    /* Bar spans the alarm range plus 10 C on each side. */
    int lo = sv->alarm_low_mc - 10000, hi = sv->alarm_high_mc + 10000;
    if (v->temp_valid) {
        int t = v->temp.temperature_mc;
        snprintf(b, sizeof b, "%.1f °C", t / 1000.0);
        set_text(w.temp, b);
        snprintf(b, sizeof b, "读数 %.3f °C（0.125 °C/位）", t / 1000.0);
        set_text(w.temp_exact, b);
        set_bar(w.temp_bar, hi > lo ? (t <= lo ? 0 : t >= hi ? 1000 : (int)(1000LL * (t - lo) / (hi - lo))) : 0);
    } else {
        set_text(w.temp, "无数据");
        set_text(w.temp_exact, "读数 --");
        set_bar(w.temp_bar, 0);
    }
    set_color(w.temp, !v->temp_valid ? C_WARN : sv->alarm_temp ? C_BAD : C_FG);
    set_bar_color(w.temp_bar, sv->alarm_temp ? C_BAD : 0x4aa3ff);
    if (sv->running)
        snprintf(b, sizeof b, "报警范围 %.1f ~ %.1f °C", sv->alarm_low_mc / 1000.0, sv->alarm_high_mc / 1000.0);
    else
        b[0] = 0;
    set_text(w.temp_range, b);
    if (v->temp_error)
        snprintf(b, sizeof b, "读取错误 %d", v->temp_error);
    else if (v->temp_valid)
        snprintf(b, sizeof b, "正常 · %.1f 秒前", v->temp_age);
    else
        snprintf(b, sizeof b, "等待数据");
    set_text(w.temp_status, b);
    set_color(w.temp_status, v->temp_error ? C_WARN : v->temp_valid ? C_OK : C_DIM);

    set_text(w.att_title, d->page ? "三轴加速度  ADXL345" : "重力倾角  ADXL345");
    if (!v->accel_valid)
        snprintf(b, sizeof b, "无数据");
    else if (d->page)
        snprintf(b, sizeof b, "X  %+.3f g\nY  %+.3f g\nZ  %+.3f g",
                 v->attitude.x_g, v->attitude.y_g, v->attitude.z_g);
    else if (v->attitude.valid)
        snprintf(b, sizeof b, "横滚  %+.1f°\n俯仰  %+.1f°", v->attitude.roll_deg, v->attitude.pitch_deg);
    else
        snprintf(b, sizeof b, "运动中\n倾角不可用");
    set_text(w.att, b);
    /* Two tilt lines fit the large font, three XYZ lines do not. */
    set_font(w.att, !d->page && v->accel_valid && v->attitude.valid ? &ui_font_cn_32 : &ui_font_cn_20);
    set_color(w.att, v->accel_valid && (d->page || v->attitude.valid) ? C_FG : C_WARN);
    if (v->accel_error)
        snprintf(b, sizeof b, "读取错误 %d", v->accel_error);
    else if (v->accel_valid)
        snprintf(b, sizeof b, "正常 · %.1f 秒前", v->accel_age);
    else
        snprintf(b, sizeof b, "等待数据");
    set_text(w.att_status, b);
    set_color(w.att_status, v->accel_error ? C_WARN : v->accel_valid ? C_OK : C_DIM);
}

static void update_gps(const struct ui_data *d)
{
    const struct gps_data *g = &d->gps;
    int fix = gps_has_fix(g, d->now), fresh = g->gga_seen && d->now - g->gga_at <= 3;
    char b[96];
    set_text(w.gps_status, !d->gps_open ? "串口不可用" : !g->rmc_seen ? "等待 NMEA 数据" :
             d->now - g->rmc_at > 3 ? "数据过期" : fix ? "已定位" : "未定位");
    set_color(w.gps_status, fix ? C_OK : C_WARN);
    if (fix) snprintf(b, sizeof b, "纬度 %+.6f", g->latitude); else snprintf(b, sizeof b, "纬度 --");
    set_text(w.gps[0], b);
    if (fix) snprintf(b, sizeof b, "经度 %+.6f", g->longitude); else snprintf(b, sizeof b, "经度 --");
    set_text(w.gps[1], b);
    if (fix && isfinite(g->speed_kmh)) snprintf(b, sizeof b, "速度 %.1f km/h", g->speed_kmh);
    else snprintf(b, sizeof b, "速度 --");
    set_text(w.gps[2], b);
    if (fix && fresh && isfinite(g->altitude)) snprintf(b, sizeof b, "海拔 %.1f m", g->altitude);
    else snprintf(b, sizeof b, "海拔 --");
    set_text(w.gps[3], b);
    if (fresh) snprintf(b, sizeof b, "卫星 %d  HDOP %.1f", g->satellites, g->hdop);
    else snprintf(b, sizeof b, "卫星 --  HDOP --");
    set_text(w.gps[4], b);
    snprintf(b, sizeof b, "UTC %s  日期 %s", g->utc[0] ? g->utc : "--", g->date[0] ? g->date : "--");
    set_text(w.gps[5], b);
    snprintf(b, sizeof b, "NMEA 语句 %lu  校验错误 %lu", g->sentences, g->rejected);
    set_text(w.gps[6], b);
}

static void update_inspection(const struct ui_data *d)
{
    const struct inspect_data *q = &d->inspect;
    unsigned long total = q->normal_count + q->defect_count;
    int online = inspect_online(q, d->inspect_now);
    int alert = q->result_seen && q->result == INSPECT_DEFECT &&
                d->inspect_now - q->result_at <= INSPECT_TIMEOUT;
    char b[96];
    set_bg(w.insp_card, alert ? C_ALERT : C_CARD);
    if (!q->result_seen) {
        set_text(w.result, "无结果");
        set_color(w.result, C_WARN);
        set_text(w.seq, "序号 --  置信度 --");
        set_text(w.age, "等待 STM32 检测结果");
    } else {
        set_text(w.result, q->result == INSPECT_DEFECT ? "残次品" : "正常");
        set_color(w.result, q->result == INSPECT_DEFECT ? C_BAD : C_OK);
        if (q->confidence >= 0)
            snprintf(b, sizeof b, "序号 %lu  置信度 %d%%", (unsigned long)q->seq, q->confidence);
        else
            snprintf(b, sizeof b, "序号 %lu  置信度 --（需复核）", (unsigned long)q->seq);
        set_text(w.seq, b);
        if (q->session)
            snprintf(b, sizeof b, "%.1f 秒前 · 会话 %d", d->inspect_now - q->result_at, q->session);
        else
            snprintf(b, sizeof b, "%.1f 秒前", d->inspect_now - q->result_at);
        set_text(w.age, b);
    }
    snprintf(b, sizeof b, "正常  %lu", q->normal_count);
    set_text(w.normal, b);
    snprintf(b, sizeof b, "残次品  %lu", q->defect_count);
    set_text(w.defect, b);
    set_bar(w.rate_bar, total ? (int)(1000.0 * q->defect_count / total + 0.5) : 0);
    snprintf(b, sizeof b, "残次率 %.1f%%  共 %lu 件", total ? 100.0 * q->defect_count / total : 0.0, total);
    set_text(w.rate, b);
    snprintf(b, sizeof b, "丢失 %lu  重发 %lu  错误 %lu  重启 %lu",
             q->missed, q->duplicates, q->rejected, q->restarts);
    set_text(w.stats, b);
    set_color(w.stats, q->missed || q->rejected ? C_WARN : C_DIM);

    set_text(w.link, d->can_state == BUS_CLOSED ? "CAN：链路断开" : d->can_state == BUS_OFF ? "CAN：总线关闭" :
             d->can_state == BUS_PASSIVE ? "CAN：错误被动" : "CAN：正常");
    set_color(w.link, d->can_state == BUS_ACTIVE ? C_OK : C_WARN);
    if (!online)
        snprintf(b, sizeof b, "STM32：离线（无心跳）");
    else if (q->heartbeat_seen)
        snprintf(b, sizeof b, "STM32：%s  已发 %lu", stm32_state(q->stm32_state), (unsigned long)q->stm32_total);
    else
        snprintf(b, sizeof b, "STM32：在线");
    set_text(w.stm32, b);
    set_color(w.stm32, online && (!q->heartbeat_seen || q->stm32_state <= 1) ? C_OK : C_WARN);
}

static const char *upload_state_text(int state)
{
    switch (state) {
    case UPLOAD_DISABLED: return "未启用";
    case UPLOAD_UNCONFIGURED: return "未配置服务器";
    case UPLOAD_PAUSED: return "已暂停";
    case UPLOAD_CONNECTING: return "连接中";
    case UPLOAD_CONNECTED: return "已连接";
    default: return "等待重连";
    }
}

static uint32_t upload_state_color(int state)
{
    return state == UPLOAD_CONNECTED ? C_OK : state == UPLOAD_DISABLED ? C_DIM : C_WARN;
}

/* Paho return codes (< 0) and MQTT CONNACK refusals (> 0). */
static void upload_error_text(int code, char *b, size_t n)
{
    const char *text;
    switch (code) {
    case 0: b[0] = 0; return;
    case -1: text = "网络连接失败"; break;
    case -3: text = "连接已断开"; break;
    case -4: text = "未确认的消息过多"; break;
    case -14: text = "协议错误"; break;
    case 1: text = "服务器不支持该协议版本"; break;
    case 2: text = "客户端 ID 被拒绝"; break;
    case 3: text = "服务器不可用"; break;
    case 4: text = "用户名或密码错误"; break;
    case 5: text = "未授权"; break;
    default: snprintf(b, n, "错误代码 %d", code); return;
    }
    snprintf(b, n, "%s（%d）", text, code);
}

static void upload_record_text(const struct upload_record *r, char *b, size_t n)
{
    const struct mail_msg *m = &r->msg;
    char when[16];
    struct tm tm;
    time_t wall = m->wall;
    localtime_r(&wall, &tm);
    strftime(when, sizeof when, "%H:%M:%S", &tm);
    if (m->type == MAIL_SENSOR)
        snprintf(b, n, "%s  传感器  温度 %.3f °C", when, m->u.sensor.temperature_mc / 1000.0);
    else if (m->type == MAIL_INSPECT && m->u.inspect.session)
        snprintf(b, n, "%s  检测结果  会话 %d  序号 %lu  %s", when, m->u.inspect.session,
                 (unsigned long)m->u.inspect.seq, m->u.inspect.result ? "残次品" : "正常");
    else if (m->type == MAIL_INSPECT)
        snprintf(b, n, "%s  检测结果  序号 %lu  %s", when, (unsigned long)m->u.inspect.seq,
                 m->u.inspect.result ? "残次品" : "正常");
    else if (m->type == MAIL_ALARM)
        snprintf(b, n, "%s  报警  %s %s  %.1f °C", when,
                 m->u.alarm.kind == ALARM_TEMP_LOW ? "低温" : "高温",
                 m->u.alarm.active ? "开始" : "解除", m->u.alarm.value / 1000.0);
    else
        snprintf(b, n, "%s  状态  设备上线", when);
    strncat(b, r->ok ? "  成功" : "  失败，稍后重发", n - strlen(b) - 1);
}

static void update_upload(const struct ui_data *d)
{
    const struct upload_status *v = &d->services.upload;
    char b[160];
    unsigned int i;
    set_text(u.state, upload_state_text(v->state));
    set_color(u.state, upload_state_color(v->state));
    snprintf(b, sizeof b, "服务器  %s", v->server[0] ? v->server : "未设置（MQTT_HOST）");
    set_text(u.server, b);
    snprintf(b, sizeof b, "客户端 ID  %s", v->client_id);
    set_text(u.client, b);
    if (v->state == UPLOAD_CONNECTED) {
        long up = (long)(d->inspect_now - v->connected_at);
        snprintf(b, sizeof b, "已连接 %02ld:%02ld:%02ld", up / 3600, up / 60 % 60, up % 60);
    } else if (v->state == UPLOAD_WAITING && v->retry_at > d->inspect_now) {
        snprintf(b, sizeof b, "%.0f 秒后重试", v->retry_at - d->inspect_now);
    } else if (v->state == UPLOAD_UNCONFIGURED || v->state == UPLOAD_DISABLED) {
        snprintf(b, sizeof b, "在 /etc/default/sensor-dashboard 中设置 MQTT_*");
    } else {
        b[0] = 0;
    }
    set_text(u.since, b);
    upload_error_text(v->state == UPLOAD_CONNECTED ? 0 : v->error, b, sizeof b);
    set_text(u.error, b);

    snprintf(b, sizeof b, "已发布 %lu", v->sent);
    set_text(u.sent, b);
    snprintf(b, sizeof b, "失败 %lu", v->failed);
    set_text(u.failed, b);
    set_color(u.failed, v->failed ? C_WARN : C_FG);
    snprintf(b, sizeof b, "待发送 %lu / %lu", v->backlog, v->backlog_max);
    set_text(u.backlog, b);
    set_bar(u.backlog_bar, v->backlog_max ? (int)(1000 * v->backlog / v->backlog_max) : 0);
    snprintf(b, sizeof b, "溢出丢弃 %lu", v->dropped);
    set_text(u.dropped, b);
    set_color(u.dropped, v->dropped ? C_WARN : C_FG);
    snprintf(b, sizeof b, "连接次数 %lu", v->connects);
    set_text(u.connects, b);
    snprintf(b, sizeof b, "主题  %s/{sensor,inspection,alarm,status}  QoS %d", v->prefix, v->qos);
    set_text(u.topics, b);
    for (i = 0; i < UPLOAD_RECENT; i++) {
        if (i < v->recent_count) {
            upload_record_text(&v->recent[i], b, sizeof b);
            set_color(u.recent[i], v->recent[i].ok ? C_FG : C_WARN);
        } else {
            snprintf(b, sizeof b, i ? "" : "暂无上传记录");
            set_color(u.recent[i], C_DIM);
        }
        set_text(u.recent[i], b);
    }
    set_bg(lv_obj_get_parent(u.pause_label), v->state == UPLOAD_PAUSED ? 0x75552c : 0x235b61);
    set_text(u.pause_label, v->state == UPLOAD_PAUSED ? LV_SYMBOL_PLAY " 恢复上传" : LV_SYMBOL_PAUSE " 暂停上传");

    snprintf(b, sizeof b, "上传 %s · 已发布 %lu · 待发送 %lu", upload_state_text(v->state), v->sent, v->backlog);
    set_text(w.upload, b);
    set_color(w.upload, upload_state_color(v->state));
}

void ui_lvgl_update(const struct ui_data *d)
{
    update_header(d);
    update_sensors(d);
    update_gps(d);
    update_inspection(d);
    update_upload(d);
    set_text(u.clock, lv_label_get_text(w.clock));
    set_bg(lv_obj_get_parent(w.pause_label), d->paused ? 0x75552c : 0x235b61);
    set_text(w.pause_label, d->paused ? "恢复传感器显示" : "暂停传感器显示");
    set_text(w.page_label, d->page ? "切换为倾角" : "切换为 XYZ 加速度");
}

unsigned int ui_lvgl_take_actions(void)
{
    unsigned int a = actions;
    actions = 0;
    return a;
}

void ui_lvgl_blit(struct surface *s, int x1, int y1, int x2, int y2,
                  const uint32_t *pixels)
{
    int width = x2 - x1 + 1, x, y;
    /* XRGB8888 framebuffer (MXS at 24 bpp): same memory layout as LVGL. */
    int direct = s->bpp == 32 && s->red.offset == 16 && s->red.length == 8 &&
                 s->green.offset == 8 && s->green.length == 8 &&
                 s->blue.offset == 0 && s->blue.length == 8;
    if (x1 < 0 || y1 < 0 || x2 >= (int)s->width || y2 >= (int)s->height || width <= 0)
        return;
    for (y = y1; y <= y2; y++, pixels += width) {
        if (direct) {
            memcpy(s->pixels + ((size_t)y + s->yoffset) * s->stride +
                   ((size_t)x1 + s->xoffset) * 4, pixels, (size_t)width * 4);
            continue;
        }
        for (x = 0; x < width; x++)
            surface_pixel(s, x1 + x, y, pixels[x] & 0xffffff);
    }
}

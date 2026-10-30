#define _DEFAULT_SOURCE
#include "ui_display.h"
#include "ui.h"
#include "lvgl.h"
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRAW_LINES 60   /* partial draw buffer: 800 x 60 x 4 B = 192 KiB */

struct touch {
    int fd, x, y, down, swap, invert_x, invert_y;
    struct input_absinfo ax, ay;
};

static int fb = -1;
static unsigned char *memory = MAP_FAILED;
static size_t length;
static struct surface screen;
static lv_color_t *draw_pixels;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static struct touch touch = { .fd = -1 };

static const char *env(const char *n, const char *d)
{
    const char *v = getenv(n);
    return v && *v ? v : d;
}

static void flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *pixels)
{
    ui_lvgl_blit(&screen, a->x1, a->y1, a->x2, a->y2, (const uint32_t *)pixels);
    lv_disp_flush_ready(drv);
}

static void touch_open(void)
{
    const char *forced = env("TOUCH_DEVICE", NULL);
    char path[64], name[128];
    int i, fd;
    for (i = 0; i < 64; i++) {
        snprintf(path, sizeof path, "/dev/input/event%d", i);
        fd = open(forced ? forced : path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) {
            memset(name, 0, sizeof name);
            ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
            if ((forced || !strcmp(name, "env-gt9147")) &&
                !ioctl(fd, EVIOCGABS(ABS_X), &touch.ax) &&
                !ioctl(fd, EVIOCGABS(ABS_Y), &touch.ay) &&
                touch.ax.maximum > touch.ax.minimum &&
                touch.ay.maximum > touch.ay.minimum) {
                touch.fd = fd;
                touch.down = 0;
                fprintf(stderr, "touch: %s (%s)\n", forced ? forced : path, name);
                return;
            }
            close(fd);
        }
        if (forced)
            break;
    }
}

/* LVGL polls this every LV_INDEV_DEF_READ_PERIOD ms. */
static void touch_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    struct input_event e;
    ssize_t n = -1;
    double x, y, t;
    (void)drv;
    while (touch.fd >= 0 && (n = read(touch.fd, &e, sizeof e)) == (ssize_t)sizeof e) {
        if (e.type == EV_ABS && e.code == ABS_X)
            touch.x = e.value;
        else if (e.type == EV_ABS && e.code == ABS_Y)
            touch.y = e.value;
        else if (e.type == EV_KEY && e.code == BTN_TOUCH)
            touch.down = e.value != 0;
        else if (e.type == EV_SYN && e.code == SYN_DROPPED)
            touch.down = 0;
    }
    if (touch.fd >= 0 && (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR))) {
        close(touch.fd);
        touch.fd = -1;
        touch.down = 0;
    }
    data->state = touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (touch.ax.maximum <= touch.ax.minimum || touch.ay.maximum <= touch.ay.minimum)
        return;   /* no touch device found yet */
    /* Same scaling as before: controller range to 0..1, then screen. */
    x = (double)(touch.x - touch.ax.minimum) / (touch.ax.maximum - touch.ax.minimum);
    y = (double)(touch.y - touch.ay.minimum) / (touch.ay.maximum - touch.ay.minimum);
    if (touch.swap) { t = x; x = y; y = t; }
    if (touch.invert_x) x = 1 - x;
    if (touch.invert_y) y = 1 - y;
    x = x < 0 ? 0 : x > 1 ? 1 : x;
    y = y < 0 ? 0 : y > 1 ? 1 : y;
    data->point.x = (lv_coord_t)(x * (screen.width - 1) + 0.5);
    data->point.y = (lv_coord_t)(y * (screen.height - 1) + 0.5);
}

int ui_display_open(void)
{
    struct fb_fix_screeninfo fi;
    struct fb_var_screeninfo vi;
    fb = open(env("FB_DEVICE", "/dev/fb0"), O_RDWR | O_CLOEXEC);
    if (fb < 0) {
        perror("framebuffer");
        return -1;
    }
    if (ioctl(fb, FBIOGET_FSCREENINFO, &fi) || ioctl(fb, FBIOGET_VSCREENINFO, &vi)) {
        perror("fb info");
        return -1;
    }
    if (fi.type != FB_TYPE_PACKED_PIXELS || fi.visual != FB_VISUAL_TRUECOLOR ||
        vi.red.msb_right || vi.green.msb_right || vi.blue.msb_right || vi.transp.msb_right) {
        fprintf(stderr, "Unsupported framebuffer type\n");
        return -1;
    }
    length = fi.smem_len;
    memory = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
    if (memory == MAP_FAILED) {
        perror("mmap framebuffer");
        return -1;
    }
    screen.pixels = memory;
    screen.length = length;
    screen.stride = fi.line_length;
    screen.width = vi.xres;
    screen.height = vi.yres;
    screen.bpp = vi.bits_per_pixel;
    screen.xoffset = vi.xoffset;
    screen.yoffset = vi.yoffset;
    screen.red = (struct color_field){ vi.red.offset, vi.red.length };
    screen.green = (struct color_field){ vi.green.offset, vi.green.length };
    screen.blue = (struct color_field){ vi.blue.offset, vi.blue.length };
    screen.alpha = (struct color_field){ vi.transp.offset, vi.transp.length };
    if (surface_validate(&screen)) {
        fprintf(stderr, "Invalid framebuffer geometry\n");
        return -1;
    }
    if (screen.width != 800 || screen.height != 480)
        fprintf(stderr, "Layout is designed for 800x480, framebuffer is %ux%u\n",
                screen.width, screen.height);
    ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK);

    lv_init();
    draw_pixels = malloc(sizeof(lv_color_t) * screen.width * DRAW_LINES);
    if (!draw_pixels)
        return -1;
    lv_disp_draw_buf_init(&draw_buf, draw_pixels, NULL, screen.width * DRAW_LINES);
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = flush;
    disp_drv.hor_res = (lv_coord_t)screen.width;
    disp_drv.ver_res = (lv_coord_t)screen.height;
    lv_disp_drv_register(&disp_drv);

    touch.swap = atoi(env("TOUCH_SWAP_XY", "0"));
    touch.invert_x = atoi(env("TOUCH_INVERT_X", "0"));
    touch.invert_y = atoi(env("TOUCH_INVERT_Y", "0"));
    touch_open();
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    ui_lvgl_create();
    fprintf(stderr, "Display: %ux%u %ubpp, LVGL %d.%d.%d\n", screen.width, screen.height,
            screen.bpp, LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return 0;
}

void ui_display_retry_touch(void)
{
    if (touch.fd < 0)
        touch_open();
}

int ui_display_touch_ok(void)
{
    return touch.fd >= 0;
}

void ui_display_close(void)
{
    if (touch.fd >= 0)
        close(touch.fd);
    touch.fd = -1;
    if (memory != MAP_FAILED)
        munmap(memory, length);
    memory = MAP_FAILED;
    if (fb >= 0)
        close(fb);
    fb = -1;
    /* LVGL keeps pointers into draw_pixels; it is not used after this. */
    free(draw_pixels);
    draw_pixels = NULL;
}

#ifndef ENV_GRAPHICS_H
#define ENV_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>

/* Framebuffer description and pixel encoding; LVGL draws the content. */
struct color_field { unsigned int offset, length; };
struct surface {
    unsigned char *pixels;
    size_t length, stride;
    unsigned int width, height, xoffset, yoffset, bpp;
    struct color_field red, green, blue, alpha;
};
int surface_validate(const struct surface *s);
void surface_pixel(struct surface *s, int x, int y, uint32_t rgb);
void surface_rect(struct surface *s, int x, int y, int w, int h, uint32_t rgb);
int surface_ppm(const struct surface *s, const char *path);
#endif

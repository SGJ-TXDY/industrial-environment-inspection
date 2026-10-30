#include "graphics.h"
#include <stdio.h>
#include <limits.h>


static uint32_t field_mask(struct color_field f)
{
    return f.length ? ((1u << f.length) - 1u) << f.offset : 0;
}

int surface_validate(const struct surface *s)
{
    struct color_field fields[4];
    uint32_t used = 0;
    size_t bytes, row_end, last;
    unsigned int i;
    if (!s || !s->pixels || !s->width || !s->height ||
        s->width > 16384 || s->height > 16384 ||
        (s->bpp != 16 && s->bpp != 24 && s->bpp != 32))
        return -1;
    bytes = s->bpp / 8;
    if ((size_t)s->xoffset > SIZE_MAX / bytes - s->width)
        return -1;
    row_end = ((size_t)s->xoffset + s->width) * bytes;
    if (row_end > s->stride || !s->stride)
        return -1;
    if ((size_t)s->yoffset > SIZE_MAX - ((size_t)s->height - 1))
        return -1;
    last = (size_t)s->yoffset + s->height - 1;
    if (last > (SIZE_MAX - row_end) / s->stride ||
        last * s->stride + row_end > s->length)
        return -1;
    fields[0] = s->red;
    fields[1] = s->green;
    fields[2] = s->blue;
    fields[3] = s->alpha;
    for (i = 0; i < 4; ++i) {
        uint32_t mask;
        if ((!fields[i].length && i < 3) || fields[i].length > 8 ||
            fields[i].offset > s->bpp ||
            fields[i].length > s->bpp - fields[i].offset)
            return -1;
        mask = field_mask(fields[i]);
        if (mask & used)
            return -1;
        used |= mask;
    }
    return 0;
}

static uint32_t encode(unsigned int channel, struct color_field f)
{
    return f.length ? ((channel * ((1u << f.length) - 1u) + 127) / 255)
                     << f.offset : 0;
}

void surface_pixel(struct surface *s, int x, int y, uint32_t rgb)
{
    unsigned char *p;
    uint32_t value;
    unsigned int i;
    if (x < 0 || y < 0 || (unsigned int)x >= s->width ||
        (unsigned int)y >= s->height)
        return;
    value = encode((rgb >> 16) & 255, s->red) |
            encode((rgb >> 8) & 255, s->green) |
            encode(rgb & 255, s->blue) | encode(255, s->alpha);
    p = s->pixels + ((size_t)y + s->yoffset) * s->stride +
        ((size_t)x + s->xoffset) * (s->bpp / 8);
    /* IMX6ULL and the screenshot test use little-endian pixel storage. */
    for (i = 0; i < s->bpp / 8; ++i)
        p[i] = (unsigned char)(value >> (8 * i));
}

void surface_rect(struct surface *s, int x, int y, int w, int h, uint32_t rgb)
{
    int right, bottom, xx, yy;
    if (w <= 0 || h <= 0 || x >= (int)s->width || y >= (int)s->height)
        return;
    /* Saturate additions before clipping to prevent signed overflow. */
    right = x > INT_MAX - w ? INT_MAX : x + w;
    bottom = y > INT_MAX - h ? INT_MAX : y + h;
    if (right <= 0 || bottom <= 0)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > (int)s->width) right = (int)s->width;
    if (bottom > (int)s->height) bottom = (int)s->height;
    for (yy = y; yy < bottom; ++yy)
        for (xx = x; xx < right; ++xx)
            surface_pixel(s, xx, yy, rgb);
}

static unsigned char decode(uint32_t pixel, struct color_field f)
{
    unsigned int mask = (1u << f.length) - 1u;
    return (unsigned char)(((pixel >> f.offset) & mask) * 255 / mask);
}

int surface_ppm(const struct surface *s, const char *path)
{
    FILE *file;
    unsigned int x, y, i;
    int bad = 0;
    if (surface_validate(s))
        return -1;
    file = fopen(path, "wb");
    if (!file)
        return -1;
    if (fprintf(file, "P6\n%u %u\n255\n", s->width, s->height) < 0)
        bad = 1;
    for (y = 0; !bad && y < s->height; ++y) {
        for (x = 0; x < s->width; ++x) {
            const unsigned char *p = s->pixels + (y + (size_t)s->yoffset) *
                s->stride + (x + (size_t)s->xoffset) * (s->bpp / 8);
            unsigned char rgb[3];
            uint32_t pixel = 0;
            for (i = 0; i < s->bpp / 8; ++i)
                pixel |= (uint32_t)p[i] << (i * 8);
            rgb[0] = decode(pixel, s->red);
            rgb[1] = decode(pixel, s->green);
            rgb[2] = decode(pixel, s->blue);
            if (fwrite(rgb, 1, 3, file) != 3) {
                bad = 1;
                break;
            }
        }
    }
    if (fclose(file))
        bad = 1;
    return bad ? -1 : 0;
}

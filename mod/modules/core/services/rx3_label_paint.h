/* SPDX-License-Identifier: MPL-2.0
 * painting a coverage mask into an rgb565 image.
 *
 * the mask holds 0..15 per pixel: 0 leaves the ground, 15 takes the ink. it
 * is box-averaged down when it is larger than the target and centred, so one
 * mask serves both the caption and the smaller images that repeat it. no
 * size is baked in here; the target's own width and height decide.
 */

#ifndef RX3_LABEL_PAINT_H
#define RX3_LABEL_PAINT_H

#include <stdint.h>

struct rx3_label_mask {
    unsigned width;
    unsigned height;
    const uint8_t *coverage;
};

/* how far apart two rgb565 colours are, blue and red weighted up because the
   format spends fewer bits on them. */
static inline unsigned rx3_label_distance(uint16_t a, uint16_t b)
{
    int red = (int)(a >> 11) - (int)(b >> 11);
    int green = (int)((a >> 5) & 0x3fu) - (int)((b >> 5) & 0x3fu);
    int blue = (int)(a & 0x1fu) - (int)(b & 0x1fu);
    return (unsigned)(4 * red * red + green * green + 4 * blue * blue);
}

static inline uint16_t rx3_label_mix(uint16_t ground, uint16_t ink, unsigned level)
{
    unsigned red = ((ground >> 11) * (15u - level) + (ink >> 11) * level) / 15u;
    unsigned green = (((ground >> 5) & 0x3fu) * (15u - level) +
                      ((ink >> 5) & 0x3fu) * level) / 15u;
    unsigned blue = ((ground & 0x1fu) * (15u - level) +
                     (ink & 0x1fu) * level) / 15u;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

/* The ink of a stock image: the pixel furthest from its ground. The records
   store the lettering and its ground, nothing else, so the furthest pixel is
   the lettering's colour -- including the dim variant, whose ink is a grey
   rather than white. */
static inline uint16_t rx3_label_ink(const uint16_t *pixels, unsigned count,
                                     uint16_t ground)
{
    unsigned index;
    unsigned best = 0;
    uint16_t ink = ground;
    for (index = 0; index < count; index++) {
        unsigned distance = rx3_label_distance(pixels[index], ground);
        if (distance > best) {
            best = distance;
            ink = pixels[index];
        }
    }
    return ink;
}

static inline void rx3_label_paint(uint16_t *pixels, unsigned width,
                                   unsigned height, uint16_t ground,
                                   uint16_t ink,
                                   const struct rx3_label_mask *mask)
{
    unsigned step = 0x10000u;
    unsigned drawn_w;
    unsigned drawn_h;
    unsigned left;
    unsigned top;
    unsigned x;
    unsigned y;
    if (mask->width > width)
        step = ((unsigned)mask->width << 16) / width + 1u;
    if (mask->height > height &&
        ((unsigned)mask->height << 16) / height + 1u > step)
        step = ((unsigned)mask->height << 16) / height + 1u;
    drawn_w = ((unsigned)mask->width << 16) / step;
    drawn_h = ((unsigned)mask->height << 16) / step;
    left = (width - drawn_w) / 2u;
    top = (height - drawn_h) / 2u;
    for (y = 0; y < drawn_h; y++) {
        unsigned from_y = (y * step) >> 16;
        unsigned to_y = ((y + 1u) * step) >> 16;
        unsigned row;
        if (to_y <= from_y)
            to_y = from_y + 1u;
        for (x = 0; x < drawn_w; x++) {
            unsigned from_x = (x * step) >> 16;
            unsigned to_x = ((x + 1u) * step) >> 16;
            unsigned sum = 0;
            unsigned count = 0;
            unsigned level;
            unsigned column;
            if (to_x <= from_x)
                to_x = from_x + 1u;
            for (row = from_y; row < to_y && row < mask->height; row++)
                for (column = from_x; column < to_x && column < mask->width;
                     column++) {
                    sum += mask->coverage[row * mask->width + column];
                    count++;
                }
            level = count ? (sum + count / 2u) / count : 0u;
            pixels[(top + y) * width + left + x] =
                rx3_label_mix(ground, ink, level);
        }
    }
}

#endif /* RX3_LABEL_PAINT_H */

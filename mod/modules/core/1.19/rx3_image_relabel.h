/* SPDX-License-Identifier: MPL-2.0
 * relabelled copies of stock images, firmware 1.19.
 *
 * a feature that renames something rbp draws as an image (a beat fx caption,
 * a beat number) hands over a coverage mask of its own lettering. the copy
 * takes the stock image's size, ground (its corner pixel) and ink (the pixel
 * farthest from the ground), so it drops into the stock layout in either
 * colour bank. stock images are rgb565 or argb1555, one 16-bit plane. each copy gets its own id in the private table, made once and
 * never rewritten, because rbp caches a surface per id. ui thread only.
 */

#ifndef RX3_IMAGE_RELABEL_H
#define RX3_IMAGE_RELABEL_H

struct rx3_label_mask {
    uint16_t width;
    uint16_t height;
    const uint8_t *coverage;    /* 0..15, row-major */
};

#define RELABEL_FIRST_ID 0x1604u
#define RELABEL_LAST_ID 0x1702u
#define RELABEL_SLOTS (RELABEL_LAST_ID - RELABEL_FIRST_ID + 1u)
#define RELABEL_RECORD_BYTES 44u
#define RELABEL_FORMAT_RGB565 1u
#define RELABEL_FORMAT_ARGB1555 2u
#define RELABEL_KEY_RGB565 0xf81fu

struct relabel_entry {
    uint32_t stock;
    const struct rx3_label_mask *mask;
    uint32_t id;    /* the stock id again when the copy failed */
};

static struct relabel_entry relabel_entries[RELABEL_SLOTS];
static unsigned relabel_count;

/* a pixel in either stock format, green widened to 6 bits. */
struct relabel_colour {
    unsigned alpha;
    unsigned red;
    unsigned green;
    unsigned blue;
};

static struct relabel_colour relabel_split(uint16_t pixel, unsigned format)
{
    struct relabel_colour colour;
    if (format == RELABEL_FORMAT_ARGB1555) {
        colour.alpha = pixel >> 15;
        colour.red = (pixel >> 10) & 0x1fu;
        colour.green = ((pixel >> 5) & 0x1fu) << 1;
    } else {
        colour.alpha = pixel != RELABEL_KEY_RGB565;
        colour.red = pixel >> 11;
        colour.green = (pixel >> 5) & 0x3fu;
    }
    colour.blue = pixel & 0x1fu;
    return colour;
}

static uint16_t relabel_join(struct relabel_colour colour, unsigned format)
{
    if (format == RELABEL_FORMAT_ARGB1555)
        return (uint16_t)((colour.alpha << 15) | (colour.red << 10) |
                          ((colour.green >> 1) << 5) | colour.blue);
    return (uint16_t)((colour.red << 11) | (colour.green << 5) | colour.blue);
}

static unsigned relabel_distance(uint16_t a, uint16_t b, unsigned format)
{
    struct relabel_colour one = relabel_split(a, format);
    struct relabel_colour two = relabel_split(b, format);
    int red = (int)one.red - (int)two.red;
    int green = (int)one.green - (int)two.green;
    int blue = (int)one.blue - (int)two.blue;
    /* against a see-through pixel, the brightest solid one is farthest. */
    if (one.alpha != two.alpha) {
        struct relabel_colour solid = one.alpha ? one : two;
        return 0x10000u + 4u * solid.red * solid.red +
               solid.green * solid.green + 4u * solid.blue * solid.blue;
    }
    return (unsigned)(4 * red * red + green * green + 4 * blue * blue);
}

/* a see-through ground or ink has no colour to blend toward, so the
   lettering is cut hard; two solid colours blend by coverage. */
static uint16_t relabel_mix(uint16_t ground, uint16_t ink, unsigned level,
                            unsigned format)
{
    struct relabel_colour low = relabel_split(ground, format);
    struct relabel_colour high = relabel_split(ink, format);
    struct relabel_colour out;
    if (!low.alpha || !high.alpha)
        return level >= 8u ? ink : ground;
    out.alpha = 1u;
    out.red = (low.red * (15u - level) + high.red * level) / 15u;
    out.green = (low.green * (15u - level) + high.green * level) / 15u;
    out.blue = (low.blue * (15u - level) + high.blue * level) / 15u;
    return relabel_join(out, format);
}

/* the mask, box-averaged down when it is larger than the target, centred. */
static void relabel_paint(uint16_t *pixels, unsigned width, unsigned height,
                          uint16_t ground, uint16_t ink, unsigned format,
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
        if (to_y <= from_y)
            to_y = from_y + 1u;
        for (x = 0; x < drawn_w; x++) {
            unsigned from_x = (x * step) >> 16;
            unsigned to_x = ((x + 1u) * step) >> 16;
            unsigned sum = 0;
            unsigned count = 0;
            unsigned level;
            unsigned row;
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
                relabel_mix(ground, ink, level, format);
        }
    }
}

static uint32_t relabel_build(uint32_t stock, const struct rx3_label_mask *mask)
{
    uint8_t *table = *(uint8_t **)IMAGE_TABLE_POINTER;
    uint8_t *source;
    uint8_t *record;
    const uint16_t *stock_pixels;
    uint16_t *pixels;
    uint16_t width;
    uint16_t height;
    uint16_t ground;
    uint16_t ink;
    uint32_t offset;
    uint32_t no_palette = 0u;
    unsigned format;
    unsigned best = 0;
    unsigned index;
    unsigned count;
    uint32_t id = RELABEL_FIRST_ID + relabel_count;
    if (!tab_assets_ready || !table || stock >= STOCK_IMAGE_COUNT)
        return stock;
    source = table + stock * RELABEL_RECORD_BYTES;
    memcpy(&width, source + 4u, sizeof(width));
    memcpy(&height, source + 6u, sizeof(height));
    memcpy(&offset, source + 0x20u, sizeof(offset));
    format = source[0x18u];
    if ((format != RELABEL_FORMAT_RGB565 &&
         format != RELABEL_FORMAT_ARGB1555) || source[0x19u] || !width ||
        !height) {
        log_number("relabel: unsupported stock image ", stock);
        log_number("relabel:   format ", source[0x18u]);
        log_number("relabel:   palette ", source[0x19u]);
        return stock;
    }
    count = (unsigned)width * height;
    pixels = mmap(0, count * 2u, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (pixels == MAP_FAILED)
        return stock;
    stock_pixels = (const uint16_t *)(table + offset);
    ground = stock_pixels[0];
    ink = ground ^ 0xffffu;
    for (index = 0; index < count; index++) {
        unsigned distance = relabel_distance(stock_pixels[index], ground,
                                             format);
        if (distance > best) {
            best = distance;
            ink = stock_pixels[index];
        }
    }
    for (index = 0; index < count; index++)
        pixels[index] = ground;
    relabel_paint(pixels, width, height, ground, ink, format, mask);

    record = table + id * RELABEL_RECORD_BYTES;
    memcpy(record, source, RELABEL_RECORD_BYTES);
    offset = (uint32_t)(unsigned long)pixels - (uint32_t)(unsigned long)table;
    memcpy(record + 0x20u, &offset, sizeof(offset));
    memcpy(record + 0x24u, &no_palette, sizeof(no_palette));
    __sync_synchronize();
    log_number("relabel: stock image ", stock);
    log_number("relabel:   width ", width);
    log_number("relabel:   height ", height);
    return id;
}

/* the id of stock relabelled with mask, made on first use. */
static uint32_t rx3_relabel_image(uint32_t stock,
                                  const struct rx3_label_mask *mask)
{
    unsigned index;
    uint32_t id;
    for (index = 0; index < relabel_count; index++)
        if (relabel_entries[index].stock == stock &&
            relabel_entries[index].mask == mask)
            return relabel_entries[index].id;
    if (relabel_count >= RELABEL_SLOTS || !tab_assets_ready)
        return stock;
    log_number("relabel: asked for stock image ", stock);
    id = relabel_build(stock, mask);
    if (id == stock)
        log_line("relabel:   kept the stock image");
    else
        log_number("relabel:   drawn as ", id);
    relabel_entries[relabel_count].stock = stock;
    relabel_entries[relabel_count].mask = mask;
    relabel_entries[relabel_count].id = id;
    relabel_count++;
    return id;
}

#endif /* RX3_IMAGE_RELABEL_H */

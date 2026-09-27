/* SPDX-License-Identifier: MPL-2.0
 * relabelled copies of stock images, firmware 1.19.
 *
 * a feature that renames something rbp draws as an image (a beat fx caption,
 * a beat number) hands over a coverage mask of its own lettering. the copy
 * takes the stock image's size, ground (its corner pixel) and ink (the pixel
 * farthest from the ground), so it drops into the stock layout in either
 * colour bank. each copy gets its own id in the private table, made once and
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
#define RELABEL_KEY_RGB565 0xf81fu

struct relabel_entry {
    uint32_t stock;
    const struct rx3_label_mask *mask;
    uint32_t id;    /* the stock id again when the copy failed */
};

static struct relabel_entry relabel_entries[RELABEL_SLOTS];
static unsigned relabel_count;

static unsigned relabel_distance(uint16_t a, uint16_t b)
{
    int red = (int)(a >> 11) - (int)(b >> 11);
    int green = (int)((a >> 5) & 0x3fu) - (int)((b >> 5) & 0x3fu);
    int blue = (int)(a & 0x1fu) - (int)(b & 0x1fu);
    return (unsigned)(4 * red * red + green * green + 4 * blue * blue);
}

static uint16_t relabel_mix(uint16_t ground, uint16_t ink, unsigned level)
{
    unsigned red = ((ground >> 11) * (15u - level) + (ink >> 11) * level) / 15u;
    unsigned green = (((ground >> 5) & 0x3fu) * (15u - level) +
                      ((ink >> 5) & 0x3fu) * level) / 15u;
    unsigned blue = ((ground & 0x1fu) * (15u - level) +
                     (ink & 0x1fu) * level) / 15u;
    return (uint16_t)((red << 11) | (green << 5) | blue);
}

/* the mask, box-averaged down when it is larger than the target, centred. */
static void relabel_paint(uint16_t *pixels, unsigned width, unsigned height,
                          uint16_t ground, uint16_t ink,
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
            /* a keyed ground has no colour to blend toward. */
            if (ground == RELABEL_KEY_RGB565)
                level = level >= 8u ? 15u : 0u;
            pixels[(top + y) * width + left + x] =
                relabel_mix(ground, ink, level);
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
    if (source[0x18u] != RELABEL_FORMAT_RGB565 || source[0x19u] || !width ||
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
        unsigned distance = relabel_distance(stock_pixels[index], ground);
        if (distance > best) {
            best = distance;
            ink = stock_pixels[index];
        }
    }
    for (index = 0; index < count; index++)
        pixels[index] = ground;
    relabel_paint(pixels, width, height, ground, ink, mask);

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
    id = relabel_build(stock, mask);
    relabel_entries[relabel_count].stock = stock;
    relabel_entries[relabel_count].mask = mask;
    relabel_entries[relabel_count].id = id;
    relabel_count++;
    return id;
}

#endif /* RX3_IMAGE_RELABEL_H */

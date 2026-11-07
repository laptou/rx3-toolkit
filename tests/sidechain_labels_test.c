/* SPDX-License-Identifier: MPL-2.0
 * the label paint the module runs, checked on the host.
 *
 * the point of these checks is that nothing about the drawn size is baked
 * in: the mask is placed into whatever target the measured stock record
 * turned out to be, centred, and averaged down when it does not fit.
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mod/modules/sidechain/rx3_label_paint.h"

#define GROUND 0x0000u
#define INK 0xffffu

/* a solid block: the whole mask at level 15. */
static uint8_t block_coverage[8 * 4];
static const struct rx3_label_mask block = { 8u, 4u, block_coverage };

/* a single full pixel in the top-left corner, the rest left alone. */
static uint8_t corner_coverage[8 * 4];
static const struct rx3_label_mask corner = { 8u, 4u, corner_coverage };

struct box {
    unsigned left, right, top, bottom, changed;
};

static struct box painted_box(const uint16_t *pixels, unsigned width,
                              unsigned height)
{
    struct box found = { width, 0u, height, 0u, 0u };
    unsigned x;
    unsigned y;
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            if (pixels[y * width + x] == GROUND)
                continue;
            found.changed++;
            if (x < found.left) found.left = x;
            if (x > found.right) found.right = x;
            if (y < found.top) found.top = y;
            if (y > found.bottom) found.bottom = y;
        }
    return found;
}

static void check_distance(void)
{
    assert(rx3_label_distance(INK, INK) == 0u);
    assert(rx3_label_distance(INK, GROUND) == rx3_label_distance(GROUND, INK));
    assert(rx3_label_distance(INK, GROUND) > rx3_label_distance(0x7befu, GROUND));
}

static void check_mix(void)
{
    assert(rx3_label_mix(GROUND, INK, 0u) == GROUND);
    assert(rx3_label_mix(GROUND, INK, 15u) == INK);
    assert(rx3_label_mix(GROUND, INK, 8u) == 0x8430u);
}

static void check_ink_finds_the_lettering(void)
{
    uint16_t field[8] = { 0x0000u, 0x0000u, 0x0841u, 0x0000u,
                          0xbdefu, 0x0000u, 0x0000u, 0x0000u };
    assert(rx3_label_ink(field, 8u, 0x0000u) == 0xbdefu);
    /* a flat record has no lettering to find; the ground comes back. */
    assert(rx3_label_ink(field, 2u, 0x0000u) == 0x0000u);
}

static void check_fits_at_its_own_size(void)
{
    uint16_t target[160 * 40];
    struct box found;
    memset(target, 0, sizeof(target));
    rx3_label_paint(target, 160u, 40u, GROUND, INK, &block);
    found = painted_box(target, 160u, 40u);
    assert(found.changed == 8u * 4u);
    assert(found.right - found.left + 1u == 8u);
    assert(found.bottom - found.top + 1u == 4u);
    /* centred, give or take the odd pixel. */
    assert(found.left <= 1u || found.left == 160u - 1u - found.right);
    assert(found.top == 18u);
}

static void check_averages_down_into_a_smaller_record(void)
{
    uint16_t target[5 * 3];
    struct box found;
    memset(target, 0, sizeof(target));
    rx3_label_paint(target, 5u, 3u, GROUND, INK, &block);
    found = painted_box(target, 5u, 3u);
    assert(found.changed > 0u && found.changed <= 5u * 3u);
    assert(found.right < 5u && found.bottom < 3u);
}

static void check_one_corner_pixel_stays_in_the_mask(void)
{
    uint16_t target[16 * 8];
    struct box found;
    memset(target, 0, sizeof(target));
    rx3_label_paint(target, 16u, 8u, GROUND, INK, &corner);
    found = painted_box(target, 16u, 8u);
    /* the mask is doubled into a 16-wide record and sits at the left of the
       centred box, never wrapped onto the right edge. */
    assert(found.changed == 1u);
    assert(found.left == 4u);
    assert(found.right == 4u);
    assert(found.top == 2u);
}

int main(void)
{
    memset(block_coverage, 15, sizeof(block_coverage));
    memset(corner_coverage, 0, sizeof(corner_coverage));
    corner_coverage[0] = 15u;
    check_distance();
    check_mix();
    check_ink_finds_the_lettering();
    check_fits_at_its_own_size();
    check_averages_down_into_a_smaller_record();
    check_one_corner_pixel_stays_in_the_mask();
    puts("label paint: ok");
    return 0;
}

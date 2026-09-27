/* SPDX-License-Identifier: MPL-2.0
 * beat jump page 2 as a sprite menu.
 *
 * page 1 (pad mode 3, page byte clear) still beat-jumps. page 2 (page byte
 * set) maps pads 1-8 onto the first eight eligible catalog packs. pressing
 * the selected pad writes "off" and the overlay closes. the choice is kept
 * in RX3_SPRITE_CHOICE_PATH. labels are composed once, before the first
 * image swap, and never rewritten: relabel caches by mask pointer.
 */
#ifndef RX3_SPRITES_PADS_H
#define RX3_SPRITES_PADS_H

#include "rx3_sprites_glyphs.h"

#define SPRITE_LABEL_CHARS 6u
#define SPRITE_LABEL_MAX_W 96u
#define SPRITE_LABEL_MAX_H 16u
#define CHECK_BEAT_JUMP_LED ((unsigned long)0x002fe544)

/* ui::Player::checkBeatJumpLoopMoveLedState: ldr r3,[pc,#imm]; push. */
static const uint8_t beat_jump_led_guard[8] = {
    0x14, 0x37, 0x9f, 0xe5, 0xf0, 0x4f, 0x2d, 0xe9,
};

/* page-2 glyphs, off then on, in pad order. */
static const uint32_t sprite_pad_off_ids[SPRITE_PAD_COUNT] = {
    0x13e8u, 0x13e6u, 0x13eau, 0x13ecu, 0x13f0u, 0x13eeu, 0x13deu, 0x13e0u,
};
static const uint32_t sprite_pad_on_ids[SPRITE_PAD_COUNT] = {
    0x13e9u, 0x13e7u, 0x13ebu, 0x13edu, 0x13f1u, 0x13efu, 0x13dfu, 0x13e1u,
};

struct sprite_led_rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

static uint8_t sprite_pad_coverage[SPRITE_PAD_COUNT]
                                  [SPRITE_LABEL_MAX_W * SPRITE_LABEL_MAX_H];
static struct rx3_label_mask sprite_pad_masks[SPRITE_PAD_COUNT];
static int sprite_pad_ready[SPRITE_PAD_COUNT];
static int sprite_pads_composed;
static struct installed_hook sprite_beat_jump_led_hook;
static check_slip_led_fn original_sprite_beat_jump_led;

static const struct rx3_label_mask *sprite_glyph(char ch)
{
    static const struct rx3_label_mask *const letters[26] = {
        &sp_glyph_a, &sp_glyph_b, &sp_glyph_c, &sp_glyph_d, &sp_glyph_e,
        &sp_glyph_f, &sp_glyph_g, &sp_glyph_h, &sp_glyph_i, &sp_glyph_j,
        &sp_glyph_k, &sp_glyph_l, &sp_glyph_m, &sp_glyph_n, &sp_glyph_o,
        &sp_glyph_p, &sp_glyph_q, &sp_glyph_r, &sp_glyph_s, &sp_glyph_t,
        &sp_glyph_u, &sp_glyph_v, &sp_glyph_w, &sp_glyph_x, &sp_glyph_y,
        &sp_glyph_z,
    };
    static const struct rx3_label_mask *const digits[10] = {
        &sp_glyph_d0, &sp_glyph_d1, &sp_glyph_d2, &sp_glyph_d3, &sp_glyph_d4,
        &sp_glyph_d5, &sp_glyph_d6, &sp_glyph_d7, &sp_glyph_d8, &sp_glyph_d9,
    };
    if (ch >= 'A' && ch <= 'Z')
        return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9')
        return digits[ch - '0'];
    if (ch == ' ')
        return &sp_glyph_sp;
    if (ch == '-')
        return &sp_glyph_hy;
    return 0;
}

/* filename stem, uppercased, other characters dropped, six glyphs at most. */
static void sprite_compose_pad(unsigned pad, const char *filename)
{
    const struct rx3_label_mask *glyphs[SPRITE_LABEL_CHARS];
    unsigned count = 0;
    unsigned width = 0;
    unsigned height = 0;
    unsigned end = 0;
    unsigned index;
    unsigned cursor;
    const char *suffix = ".rx3spr";
    while (filename[end])
        end++;
    if (end >= 8u) {
        unsigned match = 1u;
        for (index = 0; index < 8u; index++)
            if (filename[end - 8u + index] != suffix[index])
                match = 0u;
        if (match)
            end -= 8u;
    }
    for (index = 0; index < end && count < SPRITE_LABEL_CHARS; index++) {
        char ch = filename[index];
        const struct rx3_label_mask *glyph;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        glyph = sprite_glyph(ch);
        if (!glyph || !glyph->width || glyph->height > SPRITE_LABEL_MAX_H)
            continue;
        if (width + glyph->width > SPRITE_LABEL_MAX_W)
            break;
        glyphs[count++] = glyph;
        width += glyph->width;
        if (glyph->height > height)
            height = glyph->height;
    }
    if (!count || !width || !height)
        return;
    memset(sprite_pad_coverage[pad], 0, (size_t)width * height);
    cursor = 0;
    for (index = 0; index < count; index++) {
        unsigned row;
        const struct rx3_label_mask *glyph = glyphs[index];
        for (row = 0; row < glyph->height; row++)
            memcpy(sprite_pad_coverage[pad] + row * width + cursor,
                   glyph->coverage + row * glyph->width, glyph->width);
        cursor += glyph->width;
    }
    sprite_pad_masks[pad].width = (uint16_t)width;
    sprite_pad_masks[pad].height = (uint16_t)height;
    sprite_pad_masks[pad].coverage = sprite_pad_coverage[pad];
    sprite_pad_ready[pad] = 1;
}

/* -1 when the overlay is off, on the per-track pick, or on a pack past pad 8. */
static int sprite_selected_pad(void)
{
    unsigned pad;
    unsigned limit = sprite_catalog_count < SPRITE_PAD_COUNT
                         ? sprite_catalog_count : SPRITE_PAD_COUNT;
    if (sprite_choice_off || !sprite_choice_file[0])
        return -1;
    for (pad = 0; pad < limit; pad++)
        if (sprite_same(sprite_choice_file, sprite_catalog[pad]))
            return (int)pad;
    return -1;
}

static uint32_t sprites_feature_image_swap(uint32_t image_id)
{
    unsigned pad;
    for (pad = 0; pad < SPRITE_PAD_COUNT; pad++) {
        int hit = image_id == sprite_pad_off_ids[pad] ||
                  image_id == sprite_pad_on_ids[pad];
        uint32_t stock;
        if (!hit)
            continue;
        if (pad >= sprite_catalog_count || !sprite_pad_ready[pad])
            return image_id;
        stock = sprite_selected_pad() == (int)pad ? sprite_pad_on_ids[pad]
                                                  : sprite_pad_off_ids[pad];
        return rx3_relabel_image(stock, &sprite_pad_masks[pad]);
    }
    return image_id;
}

static void sprite_choice_save(void)
{
    int fd;
    unsigned length = 0;
    if (!sprite_choice_path || !sprite_choice_path[0])
        return;
    fd = open(sprite_choice_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    if (sprite_choice_off)
        (void)write(fd, "off\n", 4);
    else {
        while (sprite_choice_file[length])
            length++;
        (void)write(fd, sprite_choice_file, length);
        (void)write(fd, "\n", 1);
    }
    (void)fsync(fd);
    close(fd);
}

/* drop any load already in flight, then start the chosen pack on both decks. */
static void sprite_choice_commit(void)
{
    unsigned deck;
    sprite_choice_save();
    for (deck = 0; deck < 2u; deck++) {
        sprite_decks[deck].generation++;
        sprite_decks[deck].live = 0;
    }
    if (sprite_choice_off || !sprite_choice_file[0] || !sprites_dir)
        return;
    sprite_start_load(0u, sprite_choice_file, 1);
    sprite_start_load(1u, sprite_choice_file, 1);
}

static int sprites_feature_on_pad(void *innards, const void *key)
{
    const uint8_t *event = key;
    uint16_t code;
    unsigned operation;
    unsigned index;
    if (!innards || !key)
        return 0;
    if (*(const uint32_t *)((const uint8_t *)innards + 0x74u) != 3u)
        return 0;
    if (!((const uint8_t *)innards)[0x7c])
        return 0;
    code = (uint16_t)event[8] | ((uint16_t)event[9] << 8);
    if (code < 0x4117u || code > 0x411eu)
        return 0;
    operation = event[11] & 0x0fu;
    if (operation == 2u || operation == 3u)
        return 1;
    if (operation != 0u)
        return 0;
    index = (unsigned)(code - 0x4117u);
    if (index >= SPRITE_PAD_COUNT || index >= sprite_catalog_count)
        return 1;
    if (!sprite_choice_off &&
        sprite_same(sprite_choice_file, sprite_catalog[index])) {
        sprite_choice_off = 1;
        sprite_choice_file[0] = 0;
    } else {
        unsigned copy = 0;
        sprite_choice_off = 0;
        while (sprite_catalog[index][copy] &&
               copy + 1u < sizeof(sprite_choice_file)) {
            sprite_choice_file[copy] = sprite_catalog[index][copy];
            copy++;
        }
        sprite_choice_file[copy] = 0;
    }
    sprite_choice_commit();
    return 1;
}

/* stock paints beat-jump availability. on page 2 the selected pad stays on
   and the other seven go off. per-track pick and explicit off leave all eight
   off. leds 0x12-0x19 follow pad order. */
static void hooked_sprite_beat_jump_led(void *player, void *led_stat)
{
    static const struct sprite_led_rgb white = {255u, 255u, 255u};
    uint16_t count;
    uint8_t *entries;
    uint32_t deck_channel;
    int selected;
    uint16_t index;
    if (original_sprite_beat_jump_led)
        original_sprite_beat_jump_led(player, led_stat);
    if (!player || !led_stat)
        return;
    if (!(((const uint8_t *)player)[0x220] & 0x40u))
        return;
    count = *(const uint16_t *)((const uint8_t *)led_stat + 4u);
    entries = *(uint8_t **)((uint8_t *)led_stat + 8u);
    if (!entries || count > 256u)
        return;
    deck_channel = ((const uint8_t *)player)[0x26];
    selected = sprite_selected_pad();
    for (index = 0; index < count; index++) {
        uint8_t *led = entries + (size_t)index * 44u;
        uint32_t id = *(const uint32_t *)led;
        uint32_t channel = *(const uint32_t *)(led + 4u);
        int lit;
        if (id < 0x12u || id > 0x19u)
            continue;
        if (deck_channel && channel != deck_channel)
            continue;
        lit = selected >= 0 && id == 0x12u + (unsigned)selected;
        if (lit)
            ((set_led_color_fn)SET_LED_COLOR)(led, LED_ON, 0, &white);
        else
            ((set_led_state_fn)SET_LED_STATE)(led, 0, 0, 0, 0, 0);
    }
}

static void sprite_pads_install(void)
{
    unsigned pad;
    unsigned limit = sprite_catalog_count < SPRITE_PAD_COUNT
                         ? sprite_catalog_count : SPRITE_PAD_COUNT;
    if (!sprite_pads_composed) {
        for (pad = 0; pad < limit; pad++)
            sprite_compose_pad(pad, sprite_catalog[pad]);
        sprite_pads_composed = 1;
    }
    original_sprite_beat_jump_led = (check_slip_led_fn)install_pc_ldr_hook(
        &sprite_beat_jump_led_hook, CHECK_BEAT_JUMP_LED, beat_jump_led_guard,
        (void *)hooked_sprite_beat_jump_led);
    if (!original_sprite_beat_jump_led)
        log_line("sprites: beat jump leds stay stock");
    if (!original_on_key_pad)
        log_line("sprites: beat jump page 2 stays stock");
}

static void sprite_pads_remove(void)
{
    uninstall_hook(&sprite_beat_jump_led_hook);
    original_sprite_beat_jump_led = 0;
}

#endif /* RX3_SPRITES_PADS_H */

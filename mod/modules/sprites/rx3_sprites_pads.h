/* SPDX-License-Identifier: MPL-2.0
 * beat jump page 2 as a sprite menu, firmware 1.19.
 *
 * page 1 (pad mode 3, page byte clear) still beat-jumps. page 2 (page byte
 * set) maps pads 1-8 onto the first eight eligible catalog packs. pressing
 * the lit pad again turns the overlay off. the choice is kept in
 * RX3_SPRITE_CHOICE_PATH. the input service owns the pad hook and the beat
 * jump light refresh; the pads keep no hook of their own.
 *
 * the pad faces keep the stock glyphs in this port: relabelling them needs
 * the stock glyph records' measured sizes, which only a player or an
 * imagedata dump can give. the lit led is the selection marker until then.
 */

#ifndef RX3_SPRITES_PADS_H
#define RX3_SPRITES_PADS_H

/* the eight performance pads, in the order the key codes give them. */
#define SPRITE_PAD_CODE_FIRST 0x4117u
#define SPRITE_PAD_CODE_LAST 0x411eu
/* the player's beat jump pad mode. */
#define SPRITE_PAD_MODE_BEAT_JUMP 3u

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

/* beat jump page 2 only. release events are swallowed so the overlay stays
   as it is; a press picks the pad's pack, or turns the overlay off when it
   is the pack already shown. */
static int sprite_pad_event(const struct rx3_pad_event *event)
{
    unsigned index;
    if (event->pad_mode != SPRITE_PAD_MODE_BEAT_JUMP || !event->page)
        return 0;
    if (event->code < SPRITE_PAD_CODE_FIRST ||
        event->code > SPRITE_PAD_CODE_LAST)
        return 0;
    if (event->operation == 2u || event->operation == 3u)
        return 1;
    if (event->operation != 0u)
        return 0;
    index = event->code - SPRITE_PAD_CODE_FIRST;
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

/* the beat jump page 2 light: the chosen pad lit white, the rest dark.
   outside a choice every pad answers dark, which is what stock page 2
   shows when nothing is selected. */
static void sprite_pad_light(unsigned deck, unsigned control,
                             struct rx3_light *light)
{
    int selected = sprite_selected_pad();
    (void)deck;
    if (selected >= 0 && (unsigned)selected == control) {
        light->state = RX3_LIGHT_ON;
        light->rgb = 0xffffffu;
    } else {
        light->state = RX3_LIGHT_OFF;
    }
}

/* a refused registration is a degraded module, not a dead one: the overlay
   keeps drawing and only the pad menu stays stock. */
static int sprite_pads_install(void)
{
    if (!framework->input->register_pad(&sprite_owner, 15u, sprite_pad_event))
        framework->log_line("sprites: beat jump page 2 stays stock");
    if (!framework->input->register_lights(&sprite_owner, RX3_LIGHTS_BEAT_JUMP,
                                            sprite_pad_light))
        framework->log_line("sprites: beat jump leds stay stock");
    return 1;
}

static void sprite_pads_remove(void)
{
    framework->input->unregister_owner(&sprite_owner);
}

#endif /* RX3_SPRITES_PADS_H */

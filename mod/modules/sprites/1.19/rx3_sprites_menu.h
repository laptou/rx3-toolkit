/* SPDX-License-Identifier: MPL-2.0
 * the sprite pulse switch for firmware 1.19.
 * the UTILITY menu has no spare row, so the ON AIR DISPLAY row becomes
 * SPRITE PULSE while sprites run. its draw hook rewrites the label and shows
 * the pulse state; its commit hook stores the pulse instead of on-air. the
 * stock on-air setting is held ON the whole time, since nothing else can
 * reach it. the pulse choice lives in a one-byte file on the usb stick.
 */

#ifndef RX3_SPRITES_MENU_H
#define RX3_SPRITES_MENU_H

extern int fsync(int);

#define UTIL_SET_LINE_ON_AIR ((unsigned long)0x0013bb90)
#define UTIL_ENTER_MENU_ON_AIR ((unsigned long)0x0013c714)
#define UI_SET_UTIL_ON_AIR ((unsigned long)0x000fe924)
#define SPRITE_ON_AIR_MS 1000u

/* menu item: +0x04 value strings, +0x08 value count, +0x0c committed index,
   +0x10 index being edited or -1. a row is two cells, label then value;
   a cell holds a utf-16 length at +0x20 and the text at +0x22. */
#define UTIL_ITEM_VALUES 0x04u
#define UTIL_ITEM_COUNT 0x08u
#define UTIL_ITEM_COMMITTED 0x0cu
#define UTIL_ITEM_EDITING 0x10u
#define UTIL_CELL_LENGTH 0x20u
#define UTIL_CELL_TEXT 0x22u
#define UTIL_CELL_CHARS 0x100u
/* the item's values are "OFF" then "ON". */
#define UTIL_VALUE_ON 1

typedef int (*util_set_line_fn)(void *item, void **row);
typedef int (*util_enter_menu_fn)(void *item);
typedef void (*ui_set_util_on_air_fn)(int on);

static const uint8_t util_set_line_on_air_guard[8] = {
    0xf8, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1,
};
static const uint8_t util_enter_menu_on_air_guard[8] = {
    0x08, 0x40, 0x2d, 0xe9, 0x10, 0x30, 0x90, 0xe5,
};

static struct installed_hook util_set_line_on_air_hook;
static struct installed_hook util_enter_menu_on_air_hook;
static util_set_line_fn original_util_set_line_on_air;
static const char *sprite_pulse_path;
static unsigned sprite_on_air_ms;

static void sprite_pulse_load(void)
{
    char value = 0;
    int fd;
    sprite_pulse_path = getenv("RX3_SPRITE_PULSE_PATH");
    if (sprite_pulse_path && !sprite_pulse_path[0])
        sprite_pulse_path = 0;
    if (!sprite_pulse_path)
        return;
    fd = open(sprite_pulse_path, O_RDONLY);
    if (fd < 0)
        return;
    if (read(fd, &value, 1) == 1)
        sprite_pulse_on = value == '1';
    close(fd);
}

static void sprite_pulse_save(void)
{
    int fd;
    if (!sprite_pulse_path)
        return;
    fd = open(sprite_pulse_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    (void)write(fd, sprite_pulse_on ? "1\n" : "0\n", 2);
    (void)fsync(fd);
    close(fd);
}

static void util_cell_set(uint8_t *cell, unsigned start, const uint16_t *text)
{
    uint16_t *chars = (uint16_t *)(cell + UTIL_CELL_TEXT);
    unsigned length = start;
    while (*text && length < UTIL_CELL_CHARS)
        chars[length++] = *text++;
    *(uint16_t *)(cell + UTIL_CELL_LENGTH) = (uint16_t)length;
}

static int sprites_util_set_line(void *item, void **row)
{
    static const uint16_t label[] = {
        'S', 'P', 'R', 'I', 'T', 'E', ' ', 'P', 'U', 'L', 'S', 'E', 0,
    };
    uint8_t *fields = (uint8_t *)item;
    uint8_t *cell = (uint8_t *)row[0];
    const uint16_t *chars = (const uint16_t *)(cell + UTIL_CELL_TEXT);
    unsigned length;
    unsigned lead = 0;
    int result = original_util_set_line_on_air(item, row);
    /* keep the stock label's indent so the row lines up with the others. */
    length = *(const uint16_t *)(cell + UTIL_CELL_LENGTH);
    while (lead < length && chars[lead] == ' ')
        lead++;
    util_cell_set(cell, lead, label);
    /* outside editing the stock row shows the on-air getter; show the pulse
       instead, and make it the committed index the commit compares with. */
    if (*(int *)(fields + UTIL_ITEM_EDITING) == -1) {
        const uint16_t *const *values =
            *(const uint16_t *const **)(fields + UTIL_ITEM_VALUES);
        int index = sprite_pulse_on ? UTIL_VALUE_ON : 0;
        *(int *)(fields + UTIL_ITEM_COMMITTED) = index;
        util_cell_set((uint8_t *)row[1], 0u, values[index]);
    }
    return result;
}

/* mirrors the stock commit, but stores the pulse and leaves on-air alone. */
static int sprites_util_enter_menu(void *item)
{
    uint8_t *fields = (uint8_t *)item;
    int chosen = *(int *)(fields + UTIL_ITEM_EDITING);
    if ((unsigned)(uint16_t)chosen >= *(uint16_t *)(fields + UTIL_ITEM_COUNT))
        return 0;
    if (chosen == *(int *)(fields + UTIL_ITEM_COMMITTED))
        return 0;
    *(int *)(fields + UTIL_ITEM_COMMITTED) = chosen;
    sprite_pulse_on = chosen == UTIL_VALUE_ON;
    sprite_pulse_save();
    return 1;
}

/* ui thread. the setter only writes the setting when it changes, so this is
   cheap, and it wins back a USB "my settings" load that turned on-air off. */
static void sprite_on_air_hold(unsigned now)
{
    if (!original_util_set_line_on_air)
        return;
    if (now - sprite_on_air_ms < SPRITE_ON_AIR_MS)
        return;
    sprite_on_air_ms = now;
    ((ui_set_util_on_air_fn)UI_SET_UTIL_ON_AIR)(1);
}

static void sprite_menu_install(void)
{
    sprite_pulse_load();
    if (original_util_set_line_on_air)
        return;
    original_util_set_line_on_air = (util_set_line_fn)install_hook(
        &util_set_line_on_air_hook, UTIL_SET_LINE_ON_AIR,
        util_set_line_on_air_guard, (void *)sprites_util_set_line);
    if (!original_util_set_line_on_air) {
        log_line("sprites: on air display row unavailable, pulse switch off");
        return;
    }
    if (!install_hook(&util_enter_menu_on_air_hook, UTIL_ENTER_MENU_ON_AIR,
                      util_enter_menu_on_air_guard,
                      (void *)sprites_util_enter_menu)) {
        uninstall_hook(&util_set_line_on_air_hook);
        original_util_set_line_on_air = 0;
        log_line("sprites: on air display commit unavailable, pulse switch off");
    }
}

static void sprite_menu_remove(void)
{
    if (!original_util_set_line_on_air)
        return;
    uninstall_hook(&util_enter_menu_on_air_hook);
    uninstall_hook(&util_set_line_on_air_hook);
    original_util_set_line_on_air = 0;
}

#endif /* RX3_SPRITES_MENU_H */

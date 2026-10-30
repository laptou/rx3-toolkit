/* SPDX-License-Identifier: MPL-2.0 */
/* Overlay windows over rbp's own layers, through its ds_gr api, firmware 1.19.
 *
 * The player draws the scrolling waveforms into a private window that its
 * compositor keeps above every ns window, so a module that needs pixels there
 * creates a window with a lower z, exactly as ui_PLAYMODE_Set does. This
 * service owns the slots, the colour key and the destruction; a module owns
 * the contents.
 *
 * A lower z composites later, so the slots are ordered top first: the first
 * entry is the topmost. Claims from each end keep a caller's stacking
 * independent of the order modules start in.
 */
#include "rx3_overlays.h"
#include "rx3_log.h"

#define DS_GR_CREATE_WINDOW     ((unsigned long)0x001a2634)
#define DS_GR_DESTROY_WINDOW    ((unsigned long)0x001a2990)
#define DS_GR_LOCK_WINDOW       ((unsigned long)0x001a1c48)
#define DS_GR_UNLOCK_WINDOW     ((unsigned long)0x001a1cb0)
#define DS_GR_SET_WINDOW_OPTION ((unsigned long)0x001a061c)
#define DS_GR_SET_WINDOW_COLOR  ((unsigned long)0x001a0680)

#define DS_GR_FORMAT_RGB565 9u
#define DS_GR_OPTION_SET 1u
#define DS_GR_OPTION_VISIBLE 8u
#define DS_GR_COLOR_KEY 2u
#define DS_GR_Z_TAKEN 4
/* magenta, keyed out of every overlay window. */
#define DS_GR_KEY_ARGB 0xffff00ffu

/* free z slots under rbp's waveform (0x4f) and counter windows (0x41-0x4e). */
static const uint32_t overlay_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };
#define OVERLAY_SLOT_COUNT (sizeof(overlay_z_slots) / sizeof(overlay_z_slots[0]))

/* rbp's scrolling waveform window: both decks in one window, deck 0 in the
   top strip. The handle is zero whenever the waveform screen is not built.
   The per-deck words are non-zero while that deck has waveform data. */
#define WAVE_WINDOW_ID ((unsigned long)0x02498d44)
#define WAVE_DECK0_DATA ((unsigned long)0x05902198)
#define WAVE_DECK1_DATA ((unsigned long)0x05930fb4)
static const unsigned long overlay_wave_data[2] = {WAVE_DECK0_DATA, WAVE_DECK1_DATA};

typedef int (*ds_gr_create_window_fn)(void **window, uint32_t *info);
typedef int (*ds_gr_window_fn)(void *window);
typedef int (*ds_gr_lock_window_fn)(void *window, void **pixels, int *pitch);
typedef int (*ds_gr_set_option_fn)(void *window, uint32_t mode, uint32_t bits);
typedef int (*ds_gr_set_color_fn)(void *window, uint32_t kind, uint32_t colour);

struct overlay_window {
    void *handle;
    const void *owner;
    struct rx3_overlay_rect rect;
};

static struct overlay_window overlay_windows[OVERLAY_SLOT_COUNT];
static struct { const void *owner; void (*watcher)(unsigned int); } overlay_watchers[OVERLAY_SLOT_COUNT];

static int overlay_claim(const void *owner, const struct rx3_overlay_rect *rect,
                         int on_top, struct rx3_overlay *overlay)
{
    if (!owner || !rect || !overlay || !rect->width || !rect->height)
        return 0;
    if (overlay->handle && overlay->slot < OVERLAY_SLOT_COUNT &&
        overlay_windows[overlay->slot].handle == overlay->handle)
        return 0;
    for (unsigned int tried = 0; tried < OVERLAY_SLOT_COUNT; tried++) {
        unsigned int slot = on_top == RX3_OVERLAY_ON_TOP
                                ? OVERLAY_SLOT_COUNT - 1u - tried
                                : tried;
        if (overlay_windows[slot].handle)
            continue;
        uint32_t info[13];
        void *handle = 0;
        memset(info, 0, sizeof(info));
        info[2] = (uint32_t)rect->width | ((uint32_t)rect->height << 16);
        info[3] = DS_GR_FORMAT_RGB565;
        info[4] = 0u;
        info[5] = overlay_z_slots[slot];
        info[7] = (uint32_t)rect->x;
        info[8] = (uint32_t)rect->y;
        int result = ((ds_gr_create_window_fn)DS_GR_CREATE_WINDOW)(&handle, info);
        if (result == DS_GR_Z_TAKEN)
            continue;
        if (result || !handle) {
            log_line("overlay refused: the player would not create the window");
            return 0;
        }
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET,
                                                       DS_GR_OPTION_VISIBLE);
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET, 1u);
        ((ds_gr_set_color_fn)DS_GR_SET_WINDOW_COLOR)(handle, DS_GR_COLOR_KEY,
                                                     DS_GR_KEY_ARGB);
        overlay_windows[slot].handle = handle;
        overlay_windows[slot].owner = owner;
        overlay_windows[slot].rect = *rect;
        overlay->slot = slot;
        overlay->handle = handle;
        overlay->rect = *rect;
        return 1;
    }
    log_line("overlay refused: every z slot is taken");
    return 0;
}

/* The handle the caller holds must still be the window the table holds:
   after release() the caller's struct is stale, and touching a destroyed
   window would reach the player with a dead pointer. */
static int overlay_live(const struct rx3_overlay *overlay)
{
    return overlay && overlay->handle && overlay->slot < OVERLAY_SLOT_COUNT &&
           overlay_windows[overlay->slot].handle == overlay->handle;
}

static int overlay_lock(struct rx3_overlay *overlay, uint16_t **pixels,
                        unsigned int *pitch)
{
    if (!overlay_live(overlay) || !pixels || !pitch)
        return 0;
    int bytes_per_row = 0;
    uint8_t *raw = 0;
    if (((ds_gr_lock_window_fn)DS_GR_LOCK_WINDOW)(overlay->handle, (void **)&raw,
                                                  &bytes_per_row))
        return 0;
    if (!raw || bytes_per_row < (int)(overlay->rect.width * 2u)) {
        ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(overlay->handle);
        return 0;
    }
    *pixels = (uint16_t *)raw;
    *pitch = (unsigned int)bytes_per_row / 2u;
    return 1;
}

static void overlay_present(struct rx3_overlay *overlay)
{
    if (!overlay_live(overlay))
        return;
    /* Setting the visible option again is what marks the window dirty for
       rbp's compositor. */
    ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(overlay->handle,
                                                   DS_GR_OPTION_SET,
                                                   DS_GR_OPTION_VISIBLE);
    ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(overlay->handle);
}

static void overlay_discard(struct rx3_overlay *overlay)
{
    if (!overlay_live(overlay))
        return;
    ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(overlay->handle);
}

static void overlay_destroy(unsigned int slot)
{
    void *handle = overlay_windows[slot].handle;
    if (handle)
        ((ds_gr_window_fn)DS_GR_DESTROY_WINDOW)(handle);
    memset(&overlay_windows[slot], 0, sizeof(overlay_windows[slot]));
}

static void overlay_release(const void *owner)
{
    if (!owner)
        return;
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++) {
        if (overlay_windows[slot].handle && overlay_windows[slot].owner == owner)
            overlay_destroy(slot);
        if (overlay_watchers[slot].owner == owner)
            memset(&overlay_watchers[slot], 0, sizeof(overlay_watchers[slot]));
    }
}

static int overlay_deck_rect(unsigned int deck, struct rx3_overlay_rect *rect)
{
    if (!rect || deck > 1u)
        return 0;
    rect->x = RX3_OVERLAY_WAVE_X;
    rect->y = RX3_OVERLAY_WAVE_Y + (int)(deck * RX3_OVERLAY_WAVE_STRIDE);
    rect->width = RX3_OVERLAY_WAVE_WIDTH;
    rect->height = RX3_OVERLAY_WAVE_STRIDE;
    return 1;
}

static int overlay_wave_up(void)
{
    return *(void *const volatile *)WAVE_WINDOW_ID != 0;
}

static int overlay_wave_deck_loaded(unsigned int deck)
{
    return deck < 2u &&
           *(volatile int *)(const void *)overlay_wave_data[deck] != 0;
}

static int overlay_watch_draw(const void *owner, void (*watcher)(unsigned int))
{
    if (!owner || !watcher)
        return 0;
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++)
        if (overlay_watchers[slot].owner == owner)
            return overlay_watchers[slot].watcher == watcher;
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++) {
        if (overlay_watchers[slot].owner)
            continue;
        overlay_watchers[slot].owner = owner;
        overlay_watchers[slot].watcher = watcher;
        return 1;
    }
    log_line("overlay draw watcher refused: every watcher slot is taken");
    return 0;
}

static void overlay_unwatch_draw(const void *owner)
{
    if (!owner)
        return;
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++)
        if (overlay_watchers[slot].owner == owner)
            memset(&overlay_watchers[slot], 0, sizeof(overlay_watchers[slot]));
}

void rx3_overlays_drawn(unsigned int image)
{
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++) {
        void (*watcher)(unsigned int) = overlay_watchers[slot].watcher;
        if (watcher && overlay_watchers[slot].owner)
            watcher(image);
    }
}

unsigned int rx3_overlay_count(void)
{
    unsigned int count = 0;
    for (unsigned int slot = 0; slot < OVERLAY_SLOT_COUNT; slot++)
        count += overlay_windows[slot].handle != 0;
    return count;
}

const struct rx3_overlay_service rx3_overlays = {
    overlay_claim, overlay_lock, overlay_present, overlay_discard,
    overlay_release, overlay_deck_rect, overlay_wave_up,
    overlay_wave_deck_loaded, overlay_watch_draw, overlay_unwatch_draw
};

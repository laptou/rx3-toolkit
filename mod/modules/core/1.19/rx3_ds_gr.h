/* SPDX-License-Identifier: MPL-2.0
 * overlay windows through rbp's own ds_gr api, firmware 1.19.
 *
 * rbp draws the scrolling waveforms into a private ds_gr window, and its
 * compositor keeps that window above every ns window, so nothing drawn
 * through pal_draw_image shows over it. a feature that needs to draw there
 * opens a small rgb565 window of its own with a z above the waveform, the
 * same setup ui_PLAYMODE_Set uses. call these only from rbp's ui thread.
 */

#ifndef RX3_DS_GR_H
#define RX3_DS_GR_H

#define DS_GR_CREATE_WINDOW ((unsigned long)0x001a2634)
#define DS_GR_DESTROY_WINDOW ((unsigned long)0x001a2990)
#define DS_GR_LOCK_WINDOW ((unsigned long)0x001a1c48)
#define DS_GR_UNLOCK_WINDOW ((unsigned long)0x001a1cb0)
#define DS_GR_SET_WINDOW_OPTION ((unsigned long)0x001a061c)
#define DS_GR_SET_WINDOW_COLOR ((unsigned long)0x001a0680)
#define DS_GR_FORMAT_RGB565 9u
#define DS_GR_OPTION_SET 1u
#define DS_GR_OPTION_VISIBLE 8u
#define DS_GR_COLOR_KEY 2u
#define DS_GR_Z_TAKEN 4
/* magenta, keyed out of every overlay window. */
#define DS_GR_KEY_RGB565 0xf81fu
#define DS_GR_KEY_ARGB 0xffff00ffu

/* rbp's scrolling waveform window: both decks in one 892x444 window at
   (190, 47), deck 0 in the top 222 rows. the handle is 0 whenever the
   waveform screen is not built. the per-deck words are non-zero while that
   deck has waveform data. */
#define WAVE_WINDOW_ID ((unsigned long)0x02498d44)
#define WAVE_DECK0_DATA ((unsigned long)0x05902198)
#define WAVE_DECK1_DATA ((unsigned long)0x05930fb4)
#define WAVE_LEFT 190
#define WAVE_TOP 47
#define WAVE_WIDTH 892
#define WAVE_STRIP 222

typedef int (*ds_gr_create_window_fn)(void **window, uint32_t *info);
typedef int (*ds_gr_window_fn)(void *window);
typedef int (*ds_gr_lock_window_fn)(void *window, void **pixels, int *pitch);
typedef int (*ds_gr_set_option_fn)(void *window, uint32_t mode, uint32_t bits);
typedef int (*ds_gr_set_color_fn)(void *window, uint32_t kind, uint32_t colour);

/* free z slots under rbp's waveform (0x4f) and counter windows (0x41-0x4e).
   a lower z composites later, so on top. */
static const uint32_t rx3_gr_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };

struct rx3_gr_window {
    void *handle;
    int x;
    int y;
    unsigned width;
    unsigned height;
};

static int rx3_wave_screen_up(void)
{
    return *(void *volatile *)WAVE_WINDOW_ID != 0;
}

static int rx3_wave_deck_loaded(unsigned deck)
{
    return *(volatile int *)(deck ? WAVE_DECK1_DATA : WAVE_DECK0_DATA) != 0;
}

static void rx3_gr_close(struct rx3_gr_window *window)
{
    if (window->handle)
        ((ds_gr_window_fn)DS_GR_DESTROY_WINDOW)(window->handle);
    window->handle = 0;
}

#define RX3_GR_Z_SLOT_COUNT (sizeof(rx3_gr_z_slots) / sizeof(rx3_gr_z_slots[0]))

/* which end of the free slots a window takes. windows opened on top always
   stack above those opened below, whatever order they open in. */
enum rx3_gr_stack { RX3_GR_BELOW, RX3_GR_ON_TOP };

/* rgb565 on plane 0, visible, magenta keyed out. tries each free z, from
   the stack's end, until one is not taken. */
static int rx3_gr_open(struct rx3_gr_window *window, int x, int y,
                       unsigned width, unsigned height,
                       enum rx3_gr_stack stack)
{
    unsigned tried;
    for (tried = 0; tried < RX3_GR_Z_SLOT_COUNT; tried++) {
        unsigned slot = stack == RX3_GR_ON_TOP
                            ? RX3_GR_Z_SLOT_COUNT - 1u - tried
                            : tried;
        uint32_t info[13];
        void *handle = 0;
        int result;
        memset(info, 0, sizeof(info));
        info[2] = (uint32_t)width | ((uint32_t)height << 16);
        info[3] = DS_GR_FORMAT_RGB565;
        info[4] = 0u;
        info[5] = rx3_gr_z_slots[slot];
        info[7] = (uint32_t)x;
        info[8] = (uint32_t)y;
        result = ((ds_gr_create_window_fn)DS_GR_CREATE_WINDOW)(&handle, info);
        if (result == DS_GR_Z_TAKEN)
            continue;
        if (result || !handle)
            return -1;
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET,
                                                       DS_GR_OPTION_VISIBLE);
        ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(handle, DS_GR_OPTION_SET,
                                                       1u);
        ((ds_gr_set_color_fn)DS_GR_SET_WINDOW_COLOR)(handle, DS_GR_COLOR_KEY,
                                                     DS_GR_KEY_ARGB);
        window->handle = handle;
        window->x = x;
        window->y = y;
        window->width = width;
        window->height = height;
        return 0;
    }
    return -1;
}

/* the window's pixels, or 0. pair every success with rx3_gr_present. */
static uint8_t *rx3_gr_lock(struct rx3_gr_window *window, int *pitch)
{
    uint8_t *pixels = 0;
    *pitch = 0;
    if (((ds_gr_lock_window_fn)DS_GR_LOCK_WINDOW)(window->handle,
                                                  (void **)&pixels, pitch))
        return 0;
    if (!pixels || *pitch < (int)(window->width * 2u)) {
        ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(window->handle);
        return 0;
    }
    return pixels;
}

/* setting the visible option again is what marks the window dirty for
   rbp's compositor. */
static void rx3_gr_present(struct rx3_gr_window *window)
{
    ((ds_gr_set_option_fn)DS_GR_SET_WINDOW_OPTION)(
        window->handle, DS_GR_OPTION_SET, DS_GR_OPTION_VISIBLE);
    ((ds_gr_window_fn)DS_GR_UNLOCK_WINDOW)(window->handle);
}

#endif /* RX3_DS_GR_H */

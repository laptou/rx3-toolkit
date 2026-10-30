/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_OVERLAY_API_H
#define RX3_OVERLAY_API_H
#include "rx3_platform.h"
/* A window of the module's own pixels above the player's own layers.
 *
 * rbp draws the scrolling waveforms into a private ds_gr window, and its
 * compositor keeps that window above every ns window, so nothing drawn
 * through the native image tables reaches those 892x444 pixels. A module that
 * needs to paint there -- a sprite over a deck's waveform, a meter between
 * them -- opens a ds_gr window of its own with a lower z, the way the player's
 * own play mode does, and writes RGB565 into it.
 *
 * The core owns the z slots, the colour key, the window creation and its
 * destruction. A module owns the contents and the lifetime of the windows it
 * claims. Nothing here allocates, performs I/O or waits.
 *
 * Threading. claim(), lock(), present(), discard(), deck_rect(),
 * watch_draw() and unwatch_draw() run on the render thread: the first paint
 * of a window belongs after the player has drawn, which is what the watcher
 * exists to say. release() runs at stop, after the module's callbacks have
 * drained, and never from a callback.
 */

struct rx3_overlay_rect { int x, y; unsigned int width, height; };

/* Filled in by claim(). Treat as read-only apart from the pixels lock()
   returns; one handle belongs to one window for its lifetime. */
struct rx3_overlay {
    unsigned int slot;
    void *handle;
    struct rx3_overlay_rect rect;
};

/* Magenta, keyed out of every overlay window. A module writes this where the
   player's own pixels must show through. */
#define RX3_OVERLAY_KEY 0xf81fu

/* The waveform area: both decks in one window, deck 0 in the top strip. A
   module positions against px_deck_rect() rather than these, which are here
   so a reader can see what a strip is. */
#define RX3_OVERLAY_WAVE_X      190
#define RX3_OVERLAY_WAVE_Y      47
#define RX3_OVERLAY_WAVE_WIDTH  892u
#define RX3_OVERLAY_WAVE_STRIDE 222u

/* Stacking. A window claimed on top composites above every window claimed
   below it, whatever order the claims arrive in. Claim below for pixels the
   player's own overlays should cover. */
#define RX3_OVERLAY_BELOW 0
#define RX3_OVERLAY_ON_TOP 1

struct rx3_overlay_service {
    /* 0 when no z slot is free or the window cannot be created. `overlay` is
       written only on success. A claim on a handle that is already claimed
       fails without freeing it. */
    int (*claim)(const void *owner, const struct rx3_overlay_rect *rect,
                 int on_top, struct rx3_overlay *overlay);
    /* The window's pixels and its pitch in pixels, or 0. Pair every success
       with present(), or with discard() when nothing should be shown. */
    int (*lock)(struct rx3_overlay *overlay, uint16_t **pixels,
                unsigned int *pitch);
    /* Hands the pixels back and marks the window for the compositor. */
    void (*present)(struct rx3_overlay *overlay);
    /* Hands the pixels back without showing them. */
    void (*discard)(struct rx3_overlay *overlay);
    /* Destroys every window this owner claimed. Idempotent. */
    void (*release)(const void *owner);
    /* The strip one deck's waveform occupies, in screen coordinates. */
    int (*deck_rect)(unsigned int deck, struct rx3_overlay_rect *rect);
    /* Called on the render thread after the player drew `image`, which is
       where a module paints: the scrolling waveform is 0x159a. One watcher
       per owner; the second registration fails without replacing the first. */
    int (*watch_draw)(const void *owner, void (*watcher)(unsigned int image));
    void (*unwatch_draw)(const void *owner);
};
extern const struct rx3_overlay_service rx3_overlays;
#endif

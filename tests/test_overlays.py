# SPDX-License-Identifier: MPL-2.0
"""Overlay windows: geometry, refusal and the render-thread draw watcher.

Window creation itself goes to rbp's ds_gr api at a firmware address, so it is
not exercised here; what a module depends on and can be checked on the host is
the deck geometry it positions against, the refusals that must not reach the
player, and the watcher registry.
"""
import unittest

from tests import test_framework

HARNESS = r'''
#include "core/services/rx3_overlays.c"
static const char owner_a, owner_b;
static unsigned int draws, last_image;
static void watcher_a(unsigned int image) { draws++; last_image = image; }
static void watcher_b(unsigned int image) { (void)image; }
static const struct rx3_overlay_rect empty = { 0, 0, 0, 0 };
static const struct rx3_overlay_rect box = { 8, 9, 64, 32 };
'''


class OverlayTests(unittest.TestCase):
    run_units = test_framework.FrameworkTests.run_units

    def run_body(self, body):
        self.run_units(HARNESS + '\nint main(void){\n' + body + '\nreturn 0;}\n',
                       [], flags=('-Wno-unused-function', '-Wno-unused-const-variable'))

    def test_deck_strips_follow_the_waveform(self):
        self.run_body(r'''
    struct rx3_overlay_rect rect;
    assert(rx3_overlays.deck_rect(0, &rect));
    assert(rect.x == RX3_OVERLAY_WAVE_X && rect.y == RX3_OVERLAY_WAVE_Y);
    assert(rect.width == RX3_OVERLAY_WAVE_WIDTH);
    assert(rect.height == RX3_OVERLAY_WAVE_STRIDE);
    assert(rx3_overlays.deck_rect(1, &rect));
    assert(rect.y == RX3_OVERLAY_WAVE_Y + (int)RX3_OVERLAY_WAVE_STRIDE);
    assert(!rx3_overlays.deck_rect(2, &rect));
    assert(!rx3_overlays.deck_rect(0, 0));
''')

    def test_a_refused_claim_never_reaches_the_player(self):
        self.run_body(r'''
    struct rx3_overlay overlay;
    memset(&overlay, 0, sizeof(overlay));
    /* No owner, no rectangle, or no pixels to draw in: refused here, so the
       firmware address is never called. */
    assert(!rx3_overlays.claim(0, &box, RX3_OVERLAY_ON_TOP, &overlay));
    assert(!rx3_overlays.claim(&owner_a, 0, RX3_OVERLAY_ON_TOP, &overlay));
    assert(!rx3_overlays.claim(&owner_a, &empty, RX3_OVERLAY_ON_TOP, &overlay));
    assert(!rx3_overlays.claim(&owner_a, &box, RX3_OVERLAY_ON_TOP, 0));
    assert(rx3_overlay_count() == 0u);
''')

    def test_one_watcher_per_owner_and_dispatch(self):
        self.run_body(r'''
    assert(rx3_overlays.watch_draw(&owner_a, watcher_a));
    /* The same registration again is accepted and changes nothing. */
    assert(rx3_overlays.watch_draw(&owner_a, watcher_a));
    /* A different callback for the same owner does not replace the first. */
    assert(!rx3_overlays.watch_draw(&owner_a, watcher_b));
    assert(rx3_overlays.watch_draw(&owner_b, watcher_b));
    rx3_overlays_drawn(0x159au);
    assert(draws == 1u && last_image == 0x159au);
    rx3_overlays.unwatch_draw(&owner_a);
    rx3_overlays_drawn(0x159au);
    assert(draws == 1u);
    rx3_overlays.unwatch_draw(&owner_b);
    rx3_overlays_drawn(0x159au);
    assert(draws == 1u);
''')

    def test_release_drops_the_owners_watcher(self):
        self.run_body(r'''
    assert(rx3_overlays.watch_draw(&owner_a, watcher_a));
    rx3_overlays.release(&owner_a);
    rx3_overlays_drawn(0x159au);
    assert(draws == 0u);
    /* Releasing nothing, or twice, is not an error. */
    rx3_overlays.release(&owner_a);
    rx3_overlays.release(0);
''')

    def test_a_locked_window_needs_a_real_handle(self):
        self.run_body(r'''
    struct rx3_overlay overlay;
    uint16_t *pixels = 0;
    unsigned int pitch = 0;
    memset(&overlay, 0, sizeof(overlay));
    assert(!rx3_overlays.lock(&overlay, &pixels, &pitch));
    assert(!rx3_overlays.lock(0, &pixels, &pitch));
    /* Presenting or discarding an unclaimed window must not call the player. */
    rx3_overlays.present(&overlay);
    rx3_overlays.discard(&overlay);
    rx3_overlays.present(0);
    rx3_overlays.discard(0);
''')

    def test_watcher_slots_are_bounded(self):
        self.run_body(r'''
    static const char owners[5];
    for (unsigned int i = 0; i < 4; i++)
        assert(rx3_overlays.watch_draw(&owners[i], watcher_b));
    /* Every slot taken: the fifth owner is refused, and nobody is replaced. */
    assert(!rx3_overlays.watch_draw(&owners[4], watcher_b));
    for (unsigned int i = 0; i < 4; i++)
        rx3_overlays.unwatch_draw(&owners[i]);
    assert(rx3_overlays.watch_draw(&owners[4], watcher_b));
''')


if __name__ == "__main__":
    unittest.main()

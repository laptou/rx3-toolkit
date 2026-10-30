# SPDX-License-Identifier: MPL-2.0
"""Static guards for the sprite module, ported from the pre-split branch.

The sprite draws into an overlay window the core owns, is told when to paint
by the core's draw watcher, and reaches the player only through the public
services. These checks pin that architecture the way the module's own
regression file did before the split.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class SpriteModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = MODULES / "sprites"
        cls.module_sh = (base / "module.sh").read_text()
        cls.feature = (base / "rx3_sprites_feature.h").read_text()
        cls.menu = (base / "rx3_sprites_menu.h").read_text()
        cls.pads = (base / "rx3_sprites_pads.h").read_text()
        cls.decl = (base / "rx3_sprites_decl.h").read_text()
        cls.manifest = json.loads((base / "manifest.json").read_text())
        cls.overlays = (MODULES / "core/services/rx3_overlays.c").read_text()
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads(
            (MODULES / "core/manifest.json").read_text())

    def test_prepare_publishes_a_stable_sprite_directory(self):
        text = self.module_sh
        for needle in (
            "register_prepare_hook sprites_prepare",
            'export RX3_SPRITES_DIR="$SPRITES_LINK"',
            "RX3_SPRITE_NONCE",
            'status_path="$USB/RX3_RUNTIME/sprite-status.log"',
            'export RX3_SPRITE_STATUS_PATH="$status_path"',
            'export RX3_SPRITE_PULSE_PATH="$USB/RX3_RUNTIME/sprite-pulse"',
            'export RX3_SPRITE_CHOICE_PATH="$USB/RX3_RUNTIME/sprite-choice"',
            "request_rbp_restart",
            "Sprites need a restart so the overlay can draw",
            "module_disabled_by_switch sprites",
        ):
            self.assertIn(needle, text)
        for gone in ("RX3_SPRITE_TRACE", "anchors.proof", "register_rollback_hook"):
            self.assertNotIn(gone, text)

    def test_a_stick_without_packs_lets_the_other_modules_run(self):
        self.assertIn('[ -d "$USB/RX3_SPRITES" ]', self.module_sh)
        self.assertIn("return 0", self.module_sh)

    def test_the_sprite_uses_the_core_overlay_service_only(self):
        for needle in (
            "framework->overlays->claim(&sprite_owner, &rect,",
            "framework->overlays->lock(&window->overlay, &pixels, &pitch)",
            "framework->overlays->present(&window->overlay)",
            "framework->overlays->watch_draw(&sprite_owner, sprites_drawn)",
            "framework->overlays->wave_up()",
            "framework->overlays->wave_deck_loaded(deck)",
            "framework->overlays->release(&sprite_owner)",
        ):
            self.assertIn(needle, self.feature)
        for gone in ("DS_GR_CREATE_WINDOW", "rx3_gr_open", "original_draw_image",
                     "IMAGE_TABLE_POINTER", "pal_draw_image", "dlsym"):
            self.assertNotIn(gone, self.feature)

    def test_the_overlay_sits_above_the_waveform_and_skips_taken_slots(self):
        self.assertIn(
            "static const uint32_t overlay_z_slots[] = { 0x3eu, 0x3du, 0x3cu, 0x3bu };",
            self.overlays)
        self.assertIn("result == DS_GR_Z_TAKEN", self.overlays)
        self.assertIn("DS_GR_KEY_ARGB 0xffff00ffu", self.overlays)
        self.assertIn("WAVE_WINDOW_ID ((unsigned long)0x02498d44)", self.overlays)

    def test_one_window_over_the_whole_view_not_one_per_deck(self):
        self.assertIn("static struct sprite_window sprite_window =", self.feature)
        self.assertNotIn("sprite_windows[", self.feature)
        self.assertIn(
            "*y = RX3_OVERLAY_WAVE_Y + (int)RX3_OVERLAY_WAVE_STRIDE - (int)*height / 2;",
            self.feature)

    def test_pack_limits_match_the_host_tools(self):
        for needle in (
            "#define SPRITE_EDGE_LIMIT 256u",
            "#define SPRITE_FRAMES_LIMIT 256u",
            "#define SPRITE_FILE_LIMIT (4u * 1024u * 1024u)",
            "#define SPRITE_BASE_HEIGHT ((RX3_OVERLAY_WAVE_STRIDE * 9) / 10)",
        ):
            self.assertIn(needle, self.feature)

    def test_frames_stay_8_bit_and_scale_as_drawn(self):
        self.assertNotIn("sprite_expand", self.feature)
        self.assertIn("state->palette[source_row[along >> 16]]", self.feature)

    def test_only_the_8_bit_indexed_pack_encoding_is_accepted(self):
        for needle in (
            "header->format_byte != 2u",
            "header->bpp != 8u",
            "sprites disabled: pack failed validation",
        ):
            self.assertIn(needle, self.feature)
        self.assertNotIn("format.proof", self.feature)

    def test_the_overlay_closes_with_the_waveform_screen(self):
        for needle in (
            "int lead = screen_up ? sprite_pick_lead() : -1;",
            "sprite_window_close(window);",
        ):
            self.assertIn(needle, self.feature)

    def test_beat_jump_page_2_owns_pads_1_to_8(self):
        for needle in (
            "SPRITE_PAD_MODE_BEAT_JUMP 3u",
            "SPRITE_PAD_CODE_FIRST 0x4117u",
            "SPRITE_PAD_CODE_LAST 0x411eu",
            "event->pad_mode != SPRITE_PAD_MODE_BEAT_JUMP || !event->page",
            "framework->input->register_pad(&sprite_owner, 15u, sprite_pad_event)",
        ):
            self.assertIn(needle, self.pads)

    def test_page_2_lights_are_declarative_not_hooked(self):
        self.assertIn(
            "framework->input->register_lights(&sprite_owner, RX3_LIGHTS_BEAT_JUMP,",
            self.pads)
        self.assertIn("light->state = RX3_LIGHT_OFF;", self.pads)
        self.assertIn("light->state = RX3_LIGHT_ON;", self.pads)
        for gone in ("CHECK_BEAT_JUMP_LED", "SET_LED_COLOR", "install_pc_ldr_hook"):
            self.assertNotIn(gone, self.pads)

    def test_the_choice_survives_a_restart(self):
        self.assertIn('sprite_choice_path = getenv("RX3_SPRITE_CHOICE_PATH")',
                      self.feature)
        self.assertIn("sprite_choice_save();", self.pads)
        self.assertIn("sprite_choice_commit();", self.pads)

    def test_the_pulse_switch_is_the_on_air_row(self):
        for needle in (
            '#include "rx3_sprites_menu.h"',
            "sprite_menu_install()",
            "sprite_menu_remove()",
            "sprite_on_air_hold(now);",
        ):
            self.assertIn(needle, self.feature)
        for needle in (
            "0xf8, 0x40, 0x2d, 0xe9, 0x00, 0x50, 0xa0, 0xe1",
            "0x08, 0x40, 0x2d, 0xe9, 0x10, 0x30, 0x90, 0xe5",
            "UI_SET_UTIL_ON_AIR ((unsigned long)0x000fe924)",
            "((ui_set_util_on_air_fn)UI_SET_UTIL_ON_AIR)(1);",
            "sprite_pulse_save();",
        ):
            self.assertIn(needle, self.menu)
        # the stock enter hook is never called: committing on-air would undo
        # the hold.
        self.assertNotIn("original_util_enter_menu(item)", self.menu)

    def test_the_beat_grid_is_walked_from_the_deck_reader(self):
        for needle in (
            "SPRITE_BEAT_GRID 0x69cu",
            "sprite_readers[deck] = reader;",
            "sprite_deck_beat(deck, position, &beat)",
        ):
            self.assertIn(needle, self.decl + self.feature)

    def test_audio_stages_and_watcher_are_claimed_and_released(self):
        for needle in (
            "claim_deck_stream(&sprite_owner, sprites_deck_stream)",
            "claim_master(&sprite_owner, sprites_master)",
            "release_deck_stream(&sprite_owner)",
            "release_master(&sprite_owner)",
            "unwatch_draw(&sprite_owner)",
        ):
            self.assertIn(needle, self.feature)

    def test_the_module_is_composed_and_packaged(self):
        self.assertIn("&rx3_sprites_module", self.composition)
        self.assertIn("sprites/rx3_sprites_module.c",
                      self.core_manifest["arm_hook"]["sources"])
        self.assertIn("rx3_sprites_module.c", self.manifest["build_files"])
        self.assertIn("rx3_sprites_decl.h", self.manifest["build_files"])
        self.assertEqual(self.manifest["requires"], ["core"])
        self.assertEqual(self.manifest["category"], "screen")


if __name__ == "__main__":
    unittest.main()

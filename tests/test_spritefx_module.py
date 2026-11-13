# SPDX-License-Identifier: MPL-2.0
"""Static guards for the spritefx module and the two packs it serves.

spritefx owns the pack and the drawing; the core owns the beat fx operate
slot it claims and the overlay window it draws into. charizard2 and hector2
are shell-only: they name the pack and ask for a restart.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class SpritefxModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = MODULES / "spritefx"
        cls.feature = (base / "rx3_spritefx_feature.h").read_text()
        cls.logic = (base / "rx3_spritefx_logic.h").read_text()
        cls.manifest = json.loads((base / "manifest.json").read_text())
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads(
            (MODULES / "core/manifest.json").read_text())

    def test_the_module_is_composed_and_packaged(self):
        self.assertIn("&rx3_spritefx_module", self.composition)
        self.assertIn("spritefx/rx3_spritefx_module.c",
                      self.core_manifest["arm_hook"]["sources"])
        self.assertEqual(self.manifest["requires"], ["core"])

    def test_the_beat_fx_service_owns_the_trans_slot(self):
        self.assertIn("framework->beatfx->claim_operate(&sfx_owner, 20u, sfx_operate)",
                      self.feature)
        self.assertIn("rx3_beatfx_owned(fields, BEAT_FX_TRANS)", self.feature)
        self.assertIn("rx3_beatfx_on(fields, BEAT_FX_TRANS)", self.feature)
        self.assertIn("framework->beatfx->unregister_owner(&sfx_owner)",
                      self.feature)
        self.assertNotIn("OPERATE_BEAT_EFFECT", self.feature)

    def test_the_sprite_uses_the_core_overlay_service(self):
        for needle in (
            "framework->overlays->claim(&sfx_owner, &rect, RX3_OVERLAY_ON_TOP,",
            "framework->overlays->lock(&sfx_window.overlay, &pixels, &pitch)",
            "framework->overlays->present(&sfx_window.overlay)",
            "framework->overlays->wave_up()",
            "framework->overlays->watch_draw(&sfx_owner, sfx_drawn)",
            "framework->overlays->release(&sfx_owner)",
        ):
            self.assertIn(needle, self.feature)
        for gone in ("rx3_gr_open", "DS_GR_CREATE_WINDOW", "pal_draw_image"):
            self.assertNotIn(gone, self.feature)

    def test_the_label_artwork_is_dropped_not_faked(self):
        for gone in ("rx3_relabel_image", "sfx_compose_label", "rx3_label_mask",
                     "TRANS_CAPTION_FIRST", "rx3_sprites_glyphs.h"):
            self.assertNotIn(gone, self.feature)

    def test_frames_stay_8_bit_and_scale_as_drawn(self):
        self.assertNotIn("sfx_expand", self.feature)
        self.assertIn("sfx_pack.palette[source_row[along >> 16]]", self.feature)

    def test_the_shared_logic_is_a_local_copy(self):
        self.assertIn("rx3_sprite_checksum", self.logic)
        self.assertIn("rx3_sprite_fps_frame", self.logic)
        # no cross-module include
        self.assertNotIn("../sprites/", self.feature)
        self.assertIn("rx3_spritefx_logic.h", self.manifest["build_files"])


class PackModulesTests(unittest.TestCase):
    """The shell-only modules that choose a pack and name it."""

    def test_they_require_spritefx_and_exclude_sprites(self):
        for mid, pack, label in (("charizard2", "charizard.rx3spr", "CHARIZARD"),
                                 ("hector2", "hector.rx3spr", "HECTOR")):
            with self.subTest(module=mid):
                base = MODULES / mid
                manifest = json.loads((base / "manifest.json").read_text())
                text = (base / "module.sh").read_text()
                self.assertEqual(manifest["requires"], ["spritefx"])
                self.assertIn("sprites", manifest["conflicts"])
                # shell only: no compilation unit of its own
                self.assertNotIn("build_files", manifest)
                self.assertIn(f"module_begin {mid} {mid}", text)
                self.assertIn(f"module_disabled_by_switch {mid}", text)
                self.assertIn(f"RX3_SPRITE_FX_PACK", text)
                self.assertIn(f"RX3_SPRITE_FX_NAME={label}", text)
                self.assertIn(f"RX3_SPRITES/{pack}", text)
                self.assertIn("request_rbp_restart", text)


if __name__ == "__main__":
    unittest.main()

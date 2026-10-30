# SPDX-License-Identifier: MPL-2.0
"""Static guards for the swang module, ported from the pre-split branch.

Swang owns the warp maths and the waveform tick scan; the core owns the beat
fx hooks, the deck stream it reads positions from, and the literal-relocating
installer its halfbeat hook goes in through.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class SwangModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = MODULES / "swang"
        cls.module_sh = (base / "module.sh").read_text()
        cls.feature = (base / "rx3_swang_feature.h").read_text()
        cls.dsp = (base / "rx3_swang_dsp.h").read_text()
        cls.manifest = json.loads((base / "manifest.json").read_text())
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads(
            (MODULES / "core/manifest.json").read_text())

    def test_the_module_declares_itself(self):
        self.assertIn("module_begin swang swang", self.module_sh)
        self.assertEqual(self.manifest["runtime_directory"], "swang")
        self.assertIn('[ -r "$CORE_OBJECT" ]', self.module_sh)
        self.assertIn("module_disabled_by_switch swang", self.module_sh)
        self.assertIn('getenv("RX3_SWANG")', self.feature)
        self.assertIn("&rx3_swang_module", self.composition)
        self.assertIn("swang/rx3_swang_module.c",
                      self.core_manifest["arm_hook"]["sources"])

    def test_the_beat_fx_service_owns_the_hooks(self):
        for needle in (
            "framework->beatfx->claim_operate(&sw_owner, 10u, sw_operate)",
            "framework->beatfx->claim_beat(&sw_owner, 10u, sw_beat)",
            "framework->beatfx->claim_time(&sw_owner, 10u, sw_time)",
            "framework->beatfx->claim_info(&sw_owner, sw_info)",
            "framework->beatfx->unregister_owner(&sw_owner)",
        ):
            self.assertIn(needle, self.feature)
        for gone in ("OPERATE_BEAT_EFFECT", "install_pc_ldr_hook(\n        &sw_halfbeat_hook, SW_HALFBEAT, sw_halfbeat_guard,\n        (void *)swang_halfbeat, 12u)"):
            self.assertNotIn(gone, self.feature)

    def test_spiral_types_and_the_ownership_rule(self):
        for needle in (
            "#define BEAT_FX_SPIRAL 7",
            "#define BEAT_FX_UI_SPIRAL 3",
            "#define BEAT_FX_DISPLAY_SPIRAL 5",
            "rx3_beatfx_owned(fields, BEAT_FX_SPIRAL)",
            "rx3_beatfx_on(fields, BEAT_FX_SPIRAL)",
        ):
            self.assertIn(needle, self.feature)

    def test_the_halfbeat_hook_goes_in_through_the_literal_installer(self):
        self.assertIn(
            "framework->install_pc_ldr_hook(\n        &sw_halfbeat_hook, SW_HALFBEAT, sw_halfbeat_guard,\n        (void *)swang_halfbeat)",
            self.feature)
        self.assertIn("SW_HALFBEAT ((unsigned long)0x0024b72c)", self.feature)
        # softfp float work must not run before the hard-float original
        self.assertIn("__attribute__((noinline))", self.feature)
        # and the removal drains before releasing
        self.assertIn("sw_halfbeat_callbacks", self.feature)
        self.assertIn("framework->detach_hook(&sw_halfbeat_hook)", self.feature)

    def test_positions_come_from_the_deck_stream(self):
        self.assertIn("framework->audio->claim_deck_stream(&sw_owner, sw_stream)",
                      self.feature)
        self.assertIn("framework->audio->release_deck_stream(&sw_owner)",
                      self.feature)
        self.assertIn("sw_positions[deck] = position;", self.feature)

    def test_the_clock_walks_the_core_grid(self):
        self.assertIn("framework->audio->beat_at((unsigned)deck, position, &now)",
                      self.feature)
        self.assertNotIn("0x69cu", self.feature)

    def test_the_hook_links_no_libm(self):
        self.assertNotIn("#include <math.h>", self.feature)
        self.assertNotIn("#include <math.h>", self.dsp)

    def test_the_relabelled_artwork_is_dropped_not_faked(self):
        for gone in ("rx3_relabel_image", "SPIRAL_CAPTION_FIRST",
                     "rx3_label_mask", "rx3_swang_labels.h"):
            self.assertNotIn(gone, self.feature)
        self.assertNotIn("rx3_swang_labels.h", self.manifest["build_files"])

    def test_build_inputs_are_declared(self):
        self.assertEqual(
            self.manifest["build_files"],
            ["rx3_swang_dsp.h", "rx3_swang_feature.h", "rx3_swang_module.c"])
        self.assertEqual(self.manifest["category"], "beatjump")


if __name__ == "__main__":
    unittest.main()

# SPDX-License-Identifier: MPL-2.0
"""Static guards for the sidechain module, ported from the pre-split branch.

Sidechain owns the compression maths and the meter; the core owns the four
beat fx hooks it listens through and the window the meter draws into. These
checks pin that architecture and the measured facts the maths rely on.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class SidechainModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = MODULES / "sidechain"
        cls.module_sh = (base / "module.sh").read_text()
        cls.feature = (base / "rx3_sidechain_feature.h").read_text()
        cls.meter = (base / "rx3_sidechain_meter.h").read_text()
        cls.dsp = (base / "rx3_sidechain_dsp.h").read_text()
        cls.manifest = json.loads((base / "manifest.json").read_text())
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads(
            (MODULES / "core/manifest.json").read_text())

    def test_the_module_declares_itself(self):
        self.assertIn("module_begin sidechain sidechain", self.module_sh)
        self.assertEqual(self.manifest["runtime_directory"], "sidechain")
        self.assertIn('[ -r "$CORE_OBJECT" ]', self.module_sh)
        self.assertIn("module_disabled_by_switch sidechain", self.module_sh)
        self.assertIn('getenv("RX3_SIDECHAIN")', self.feature)
        self.assertIn("&rx3_sidechain_module", self.composition)
        self.assertIn("sidechain/rx3_sidechain_module.c",
                      self.core_manifest["arm_hook"]["sources"])

    def test_the_beat_fx_service_owns_the_hooks(self):
        for needle in (
            "framework->beatfx->claim_operate(&sc_owner, 10u, sc_operate)",
            "framework->beatfx->claim_beat(&sc_owner, 10u, sc_beat)",
            "framework->beatfx->claim_info(&sc_owner, sc_info)",
            "framework->beatfx->unregister_owner(&sc_owner)",
        ):
            self.assertIn(needle, self.feature)
        for gone in ("OPERATE_BEAT_EFFECT", "install_hook(", "original_operate"):
            self.assertNotIn(gone, self.feature)
        # the module does not start without the audio and operate paths
        self.assertIn('log_line("sidechain refused: the channel stage is taken")',
                      self.feature)

    def test_helix_types_and_the_ownership_rule(self):
        for needle in (
            "#define BEAT_FX_HELIX 14",
            "#define BEAT_FX_UI_HELIX 4",
            "#define BEAT_FX_DISPLAY_HELIX 13",
            "rx3_beatfx_owned(manager, BEAT_FX_HELIX)",
            "rx3_beatfx_on(manager, BEAT_FX_HELIX)",
        ):
            self.assertIn(needle, self.feature)

    def test_the_pending_depth_and_the_effects_own_time(self):
        self.assertIn("(manager + 0xe8u)", self.feature)
        self.assertIn("(effect + 0x24u)", self.feature)
        self.assertNotIn("(effect + 0x3cu)", self.feature)

    def test_dub_echo_is_not_the_slot(self):
        self.assertNotIn("color_fx", self.feature.lower())
        self.assertNotIn("COLOR_FX", self.feature)

    def test_the_hook_links_no_libm(self):
        self.assertNotIn("#include <math.h>", self.feature)
        self.assertNotIn("#include <math.h>", self.dsp)

    def test_the_audio_path_never_touches_the_compositor(self):
        audio_half = self.feature.split("sc_block_peak_l2")[1].split("sc_drawn")[0]
        self.assertNotIn("overlays->", audio_half)

    def test_the_meter_lives_on_the_ui_thread(self):
        self.assertIn('#include "rx3_sidechain_meter.h"', self.feature)
        self.assertIn("sc_meter_tick();", self.feature)
        self.assertIn("framework->overlays->wave_up()", self.meter)
        self.assertIn("SC_METER_IDLE_MS", self.meter)
        self.assertIn("RX3_OVERLAY_BELOW", self.meter)

    def test_the_relabelled_artwork_is_dropped_not_faked(self):
        for gone in ("rx3_relabel_image", "HELIX_CAPTION_FIRST",
                     "BEAT_NUMBER_IMAGES", "rx3_label_mask"):
            self.assertNotIn(gone, self.feature)
        self.assertIn("stock artwork in this port",
                      (MODULES / "sidechain/README.md").read_text())

    def test_build_inputs_are_declared(self):
        self.assertEqual(
            self.manifest["build_files"],
            ["rx3_sidechain_dsp.h", "rx3_sidechain_meter.h",
             "rx3_sidechain_feature.h", "rx3_sidechain_module.c"])
        self.assertEqual(self.manifest["category"], "beatjump")


if __name__ == "__main__":
    unittest.main()

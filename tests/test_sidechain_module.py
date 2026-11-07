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
CORE = MODULES / "core"


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

    def test_the_caption_is_measured_not_guessed(self):
        labels = (MODULES / "sidechain/rx3_sidechain_labels.h").read_text()
        # the paint happens core-side, on the stock record as it is copied:
        # a module cannot read a record while it starts, because the player
        # has not built its table yet. that is the whole reason for it.
        self.assertIn("replace_masked", labels)
        self.assertNotIn("replace_native", labels)
        self.assertNotIn("stock_image", labels)
        self.assertIn("SC_CAPTION_FIRST_ID 0x1449u", labels)
        self.assertIn("SC_CAPTION_COUNT 4u", labels)
        # the mask's own rasterisation is the one size that is fixed; the
        # record's width and height decide the size that is drawn.
        self.assertIn("sc_label_caption_coverage[1856]", labels)
        self.assertIn("#define SC_LABEL_WIDTH 116u", labels)
        self.assertIn("#define SC_LABEL_HEIGHT 16u", labels)
        for guessed in ("160u", "40u", "0xf81f", "0xf81fu"):
            self.assertNotIn(guessed, labels)
        self.assertIn("sc_labels_install();", self.feature)
        self.assertIn("sc_labels_release();", self.feature)

    def test_the_paint_reads_the_record_it_stands_in_for(self):
        images = (CORE / "services/rx3_images.c").read_text()
        # the first code ever to read a stock record in place: log every field
        # so one boot answers what the format and palette bytes really are.
        for field in ("masked image record = ", "masked image width = ",
                      "masked image height = ", "masked image format = ",
                      "masked image palette = "):
            self.assertIn(field, images)
        # a record with no bitmap, or an index image whose palette would have
        # to be translated, is refused instead of painted over with nonsense.
        self.assertIn("if (!width || !height) return 0;", images)
        self.assertIn("if (paletted) return 0;", images)
        self.assertIn("masked image refused = ", images)
        self.assertIn("masked image painted = ", images)

    def test_the_paint_keeps_the_records_own_ground(self):
        images = (CORE / "services/rx3_images.c").read_text()
        # the mask covers only the lettering: every other pixel keeps the
        # ground the player drew, so a blue plate stays blue.
        self.assertIn("pixels[i] = ground;", images)
        self.assertIn("rx3_label_paint(pixels, width, height, ground, ink, &mask)",
                      images)
        # a mask fills every pixel, so the record is plain rgb565; a keyed
        # replacement keeps the colour-key format.
        self.assertIn("record[0x18u] = r->coverage ? 1u : 2u;", images)

    def test_the_registration_is_taken_out_and_the_pixels_left_alone(self):
        labels = (MODULES / "sidechain/rx3_sidechain_labels.h").read_text()
        # once the tables publish the pixels are the table's: the release
        # only takes the registration out, it never frees them.
        self.assertIn("framework->images->release_native(&sc_owner)", labels)
        self.assertNotIn("munmap", labels)

    def test_the_tiles_take_the_same_mask(self):
        labels = (MODULES / "sidechain/rx3_sidechain_labels.h").read_text()
        self.assertIn("SC_TILE_FIRST_ID 0x15aau", labels)
        self.assertIn("SC_TILE_COUNT 3u", labels)
        self.assertEqual(labels.count("replace_masked("), 2)
        self.assertIn("sidechain: labels relabelled = ", labels)

    def test_build_inputs_are_declared(self):
        self.assertEqual(
            self.manifest["build_files"],
            ["rx3_sidechain_dsp.h", "rx3_sidechain_meter.h",
             "rx3_sidechain_labels.h",
             "rx3_sidechain_feature.h", "rx3_sidechain_module.c"])
        self.assertEqual(self.manifest["category"], "beatjump")


if __name__ == "__main__":
    unittest.main()

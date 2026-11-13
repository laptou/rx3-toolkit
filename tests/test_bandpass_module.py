# SPDX-License-Identifier: MPL-2.0
"""Static guards for the bandpass module, ported from the pre-split branch.

Bandpass owns the DUB ECHO slot itself and installs its one hook through the
core's installer; the maths is shared with the host test.
"""
import json
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class BandpassModuleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base = MODULES / "bandpass"
        cls.module_sh = (base / "module.sh").read_text()
        cls.feature = (base / "rx3_bandpass_feature.h").read_text()
        cls.dsp = (base / "rx3_bandpass_dsp.h").read_text()
        cls.manifest = json.loads((base / "manifest.json").read_text())
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads(
            (MODULES / "core/manifest.json").read_text())

    def test_the_module_declares_itself(self):
        self.assertIn("module_begin bandpass bandpass", self.module_sh)
        self.assertEqual(self.manifest["runtime_directory"], "bandpass")
        self.assertIn('[ -r "$CORE_OBJECT" ]', self.module_sh)
        self.assertIn("module_disabled_by_switch bandpass", self.module_sh)
        self.assertIn('getenv("RX3_BANDPASS")', self.feature)
        self.assertIn("&rx3_bandpass_module", self.composition)
        self.assertIn("bandpass/rx3_bandpass_module.c",
                      self.core_manifest["arm_hook"]["sources"])

    def test_the_module_owns_its_one_hook_through_the_installer(self):
        self.assertIn("framework->install_hook(\n        &color_fx_operate_hook,"
                      " COLOR_FX_OPERATE, color_fx_operate_guard,",
                      self.feature)
        self.assertIn("framework->detach_hook(&color_fx_operate_hook)",
                      self.feature)
        self.assertIn("framework->release_hook(&color_fx_operate_hook)",
                      self.feature)
        self.assertIn("bp_callbacks", self.feature)

    def test_the_dub_echo_slot_and_its_knobs(self):
        for needle in (
            "COLOR_FX_OPERATE ((unsigned long)0x000bbc68)",
            "#define COLOR_FX_DUB_ECHO 4",
            "*(const int *)(fields + 0x24u) != COLOR_FX_DUB_ECHO",
            "effect + 0x1cu",
            "effect + 0x20u",
            "channel + 0x14u",
        ):
            self.assertIn(needle, self.feature)

    def test_the_hook_links_no_libm(self):
        self.assertNotIn("#include <math.h>", self.feature)
        self.assertNotIn("#include <math.h>", self.dsp)

    def test_build_inputs_are_declared(self):
        self.assertEqual(
            self.manifest["build_files"],
            ["rx3_bandpass_dsp.h", "rx3_bandpass_feature.h",
             "rx3_bandpass_module.c"])


if __name__ == "__main__":
    unittest.main()

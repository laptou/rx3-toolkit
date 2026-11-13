# SPDX-License-Identifier: MPL-2.0
"""Contract and host encoder tests for the experimental beat-telemetry module."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULES = ROOT / "mod/modules"


class BeatTelemetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.base = MODULES / "beat-telemetry"
        cls.module_sh = (cls.base / "module.sh").read_text()
        cls.feature = (cls.base / "rx3_beat_telemetry_feature.h").read_text()
        cls.packet = (cls.base / "rx3_beat_telemetry_packet.h").read_text()
        cls.manifest = json.loads((cls.base / "manifest.json").read_text())
        cls.composition = (MODULES / "core/runtime/rx3_composition.c").read_text()
        cls.core_manifest = json.loads((MODULES / "core/manifest.json").read_text())

    def test_module_is_optional_and_composed(self):
        self.assertIn("module_begin beat-telemetry beat_telemetry", self.module_sh)
        self.assertIn("module_disabled_by_switch beat-telemetry", self.module_sh)
        self.assertIn('getenv("RX3_BEAT_TELEMETRY")', self.feature)
        self.assertIn("&rx3_beat_telemetry_module", self.composition)
        self.assertIn("beat-telemetry/rx3_beat_telemetry_module.c",
                      self.core_manifest["arm_hook"]["sources"])
        self.assertFalse(self.manifest["default"])
        self.assertEqual(self.manifest["requires"], ["core"])
        self.assertEqual(self.manifest["build_files"], [
            "rx3_beat_telemetry_packet.h", "rx3_beat_telemetry_decl.h",
            "rx3_beat_telemetry_feature.h", "rx3_beat_telemetry_module.c",
        ])

    def test_realtime_callback_only_publishes_a_bounded_sample(self):
        callback = self.feature.split("static void rx3_bt_stream", 1)[1].split(
            "static int rx3_bt_sample_read", 1)[0]
        for forbidden in ("beat_at", "sendto(", "malloc(", "usleep(", "socket("):
            self.assertNotIn(forbidden, callback)
        self.assertIn("claim_deck_stream(&rx3_bt_owner, rx3_bt_stream)", self.feature)
        self.assertIn("release_deck_stream(&rx3_bt_owner)", self.feature)
        module_source = (self.base / "rx3_beat_telemetry_module.c").read_text()
        self.assertIn("rx3_bt_track_did_load", module_source)
        self.assertIn("rx3_bt_track_will_load", module_source)

    def test_packet_encoder_matches_v1_layout(self):
        header = (self.base / "rx3_beat_telemetry_packet.h").resolve()
        program = f'''#include <stdio.h>\n#include <inttypes.h>\n#include "{header}"\nint main(void) {{
    struct rx3_bt_packet p = {{2, 0x0f, UINT64_C(0x0102030405060708),
        UINT32_C(0x11223344), UINT32_C(0x55667788),
        UINT64_C(0xffeeddccbbaa9988), UINT32_C(128000), UINT32_C(250), 3}};
    uint8_t out[RX3_BT_PACKET_LEN]; rx3_bt_encode(&p, out);
    for (unsigned i = 0; i < sizeof(out); i++) printf("%02x", out[i]);
    putchar('\\n'); return 0;
}}\n'''
        expected = (
            "4243423101020f00"
            "0807060504030201"
            "44332211"
            "88776655"
            "8899aabbccddeeff"
            "00f40100"
            "fa000000"
            "03"
            "00000000000000"
        )
        with tempfile.TemporaryDirectory() as temp:
            source = pathlib.Path(temp) / "packet_test.c"
            binary = pathlib.Path(temp) / "packet_test"
            source.write_text(program)
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            str(source), "-o", str(binary)], check=True)
            actual = subprocess.run([str(binary)], check=True, text=True,
                                     capture_output=True).stdout.strip()
        self.assertEqual(actual, expected)

    def test_grid_phase_split_handles_bar_edges(self):
        packet = (self.base / "rx3_beat_telemetry_packet.h").resolve()
        program = f'''#include <stdio.h>\n#include "{packet}"\nint main(void) {{
    const double beats[] = {{0.0, 0.25, 1.999, 3.5, 4.125, 128.75}};
    for (unsigned i = 0; i < sizeof(beats) / sizeof(beats[0]); i++) {{
        uint32_t phase; uint8_t bar;
        rx3_bt_phase_and_bar(beats[i], &phase, &bar);
        printf("%u,%u\\n", phase, (unsigned)bar);
    }}
    return 0;
}}\n'''
        with tempfile.TemporaryDirectory() as temp:
            source = pathlib.Path(temp) / "phase_test.c"
            binary = pathlib.Path(temp) / "phase_test"
            source.write_text(program)
            subprocess.run(["cc", "-std=c11", "-ffunction-sections",
                            "-fdata-sections", "-Wno-unused-function",
                            str(source),
                            "-Wl,-dead_strip" if sys.platform == "darwin"
                            else "-Wl,--gc-sections", "-o", str(binary)], check=True)
            actual = subprocess.run([str(binary)], check=True, text=True,
                                     capture_output=True).stdout.splitlines()
        self.assertEqual(actual, ["0,1", "250,1", "999,2", "500,4",
                                  "125,1", "750,1"])

    def test_seek_boundary_reanchors_generation(self):
        self.assertIn("frame_delta < 0", self.feature)
        self.assertIn("frame_delta > max_frames", self.feature)
        self.assertIn("beat_delta < -0.001", self.feature)
        self.assertIn("rx3_bt_send(deck, invalid_flags, 0, 0, 0, generation)",
                      self.feature)
        self.assertIn("RX3_BT_FLAG_GRID_VALID", self.feature)
        self.assertIn("RX3_BT_FLAG_ON_AIR", self.feature)


if __name__ == "__main__":
    unittest.main()

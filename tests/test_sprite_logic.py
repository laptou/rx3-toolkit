# SPDX-License-Identifier: MPL-2.0
"""The C sprite decisions and the Python tools must print the same answers."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path

from tools.rx3_sprites.select import choose
from tools.rx3_sprites.timing import fps_frame, phase_frame
from tools.rx3_stems.rekordbox import export_stem


ROOT = Path(__file__).resolve().parents[1]


class SpriteLogicTests(unittest.TestCase):
    def test_host_binary_matches_python(self):
        binary = ROOT / "build" / "sprite_logic_test"
        binary.parent.mkdir(exist_ok=True)
        compiled = subprocess.run(
            [
                "clang", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                "-o", str(binary), str(ROOT / "tests" / "sprite_logic_test.c"),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        self.assertEqual(ran.returncode, 0, ran.stderr)
        lines = dict(line.split(" ", 1) for line in ran.stdout.splitlines() if " " in line)
        self.assertEqual(lines["stem"], export_stem("Artist - Title"))
        self.assertEqual(int(lines["fps"]), fps_frame(500, 10, 1, 4))
        self.assertEqual(int(lines["phase-zero"]), phase_frame(0, 1, 1, 4))
        self.assertEqual(int(lines["phase-seek"]), phase_frame(2500, 1, 1, 4))
        self.assertEqual(int(lines["phase-loop"]), phase_frame(3500, 2, 4, 8))
        self.assertEqual(phase_frame(3500, 2, 4, 8), 7)
        self.assertIsNone(phase_frame(None, 1, 1, 4))
        self.assertEqual(lines["phase-missing"], "1")
        names = ["a.rx3spr", "b.rx3spr"]
        first = choose(names, "demo", 0, 1, None)
        self.assertEqual(names[int(lines["pick"])], first)
        self.assertEqual(names[int(lines["pick-next"])], choose(names, "demo", 0, 1, first))
        self.assertEqual(lines["long-stem"], "x" * 44)
        self.assertTrue(ran.stdout.endswith("sprite logic: OK\n"))


if __name__ == "__main__":
    unittest.main()

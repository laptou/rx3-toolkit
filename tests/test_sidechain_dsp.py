# SPDX-License-Identifier: MPL-2.0
"""The sidechain maths the player runs, checked against libm on the host."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SidechainDspTests(unittest.TestCase):
    def test_host_binary_passes(self):
        binary = ROOT / "build" / "sidechain_dsp_test"
        binary.parent.mkdir(exist_ok=True)
        compiled = subprocess.run(
            [
                "clang", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                "-o", str(binary), str(ROOT / "tests" / "sidechain_dsp_test.c"),
                "-lm",
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()

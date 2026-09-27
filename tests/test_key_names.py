# SPDX-License-Identifier: MPL-2.0
"""The sidebar key spelling the player uses."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class KeyNameTests(unittest.TestCase):
    def test_shift_keeps_the_notation(self):
        binary = ROOT / "build" / "key_names_test"
        binary.parent.mkdir(exist_ok=True)
        compiled = subprocess.run(
            [
                "clang", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                "-o", str(binary), str(ROOT / "tests" / "key_names_test.c"),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(compiled.returncode, 0, compiled.stderr)
        ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn("key names: OK", ran.stdout)

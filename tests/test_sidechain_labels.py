# SPDX-License-Identifier: MPL-2.0
"""The caption paint the module runs, checked against a plain host build."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class SidechainLabelTests(unittest.TestCase):
    def test_host_binary_passes(self):
        binary = ROOT / "build" / "sidechain_labels_test"
        binary.parent.mkdir(exist_ok=True)
        compiled = subprocess.run(
            [
                "clang", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                "-o", str(binary), str(ROOT / "tests" / "sidechain_labels_test.c"),
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

# SPDX-License-Identifier: MPL-2.0
"""The runtime module command is what writes autoexec.bin."""

import subprocess
import sys
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[1]


def run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "tools.rx3_runtime", *args],
        cwd=REPOSITORY,
        text=True,
        capture_output=True,
        check=False,
    )


class RuntimeCliTests(unittest.TestCase):
    def test_list_names_the_selectable_modules(self):
        result = run_cli("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("sprites", result.stdout)
        self.assertIn("optional", result.stdout)

    def test_build_help_writes_autoexec(self):
        result = run_cli("build", "--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("autoexec.bin", result.stdout)
        self.assertIn("--key", result.stdout)
        self.assertIn("--output", result.stdout)
        self.assertIn("--patch", result.stdout)

    def test_build_requires_a_key_and_an_output_directory(self):
        result = run_cli("build")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("--key", result.stderr)


if __name__ == "__main__":
    unittest.main()

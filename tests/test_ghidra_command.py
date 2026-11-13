#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The first Ghidra run imports and analyses rbp; later runs only decompile."""

from __future__ import annotations

import pathlib
import tempfile
import unittest

from tools.rx3_ghidra.cli import PROJECT_NAME, headless_command


class HeadlessCommandTests(unittest.TestCase):
    def test_a_new_project_imports_as_armv7(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = pathlib.Path(directory)
            command = headless_command(pathlib.Path("/g/analyzeHeadless"), project,
                                       pathlib.Path("/s/rbp-abc.rbp"), project / "out",
                                       ["0x1d3284"])
            self.assertIn("-import", command)
            self.assertEqual(command[command.index("-processor") + 1], "ARM:LE:32:v7")
            self.assertEqual(command[-2:], [str(project / "out"), "0x1d3284"])

    def test_an_existing_project_is_reused_without_analysis(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            project = pathlib.Path(directory)
            (project / f"{PROJECT_NAME}.gpr").write_text("")
            command = headless_command(pathlib.Path("/g/analyzeHeadless"), project,
                                       pathlib.Path("/s/rbp-abc.rbp"), project / "out",
                                       ["str:wave"])
            self.assertNotIn("-import", command)
            self.assertEqual(command[command.index("-process") + 1], "rbp-abc.rbp")
            self.assertIn("-noanalysis", command)


if __name__ == "__main__":
    unittest.main()

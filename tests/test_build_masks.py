# SPDX-License-Identifier: MPL-2.0
"""The mask generator writes one coverage mask per label, on one baseline."""

import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
TOOL = REPOSITORY / "tools/rx3_assets/build_masks.py"


class BuildMasksTests(unittest.TestCase):
    def test_header_shape(self):
        try:
            import PIL  # noqa: F401
        except ImportError:
            self.skipTest("Pillow is not installed")
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / "labels.h"
            subprocess.run(
                [sys.executable, str(TOOL), "--output", str(output),
                 "--prefix", "t", "wide=SIDECHAIN", "narrow=2:1"],
                check=True, capture_output=True,
            )
            text = output.read_text()
        sizes = re.findall(
            r"static const struct rx3_label_mask t_(\w+) = \{ (\d+)u, (\d+)u,", text)
        self.assertEqual([name for name, _, _ in sizes], ["wide", "narrow"])
        (_, wide_w, wide_h), (_, narrow_w, narrow_h) = sizes
        self.assertEqual(wide_h, narrow_h)
        self.assertGreater(int(wide_w), int(narrow_w))
        for name, width, height in sizes:
            body = re.search(
                rf"t_{name}_coverage\[(\d+)\] = \{{(.*?)\}};", text, re.S)
            values = [int(value) for value in re.findall(r"\d+", body.group(2))]
            self.assertEqual(int(body.group(1)), int(width) * int(height))
            self.assertEqual(len(values), int(width) * int(height))
            self.assertLessEqual(max(values), 15)
            self.assertGreater(max(values), 0)


if __name__ == "__main__":
    unittest.main()

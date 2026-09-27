# SPDX-License-Identifier: MPL-2.0
"""Sprite packs and the rules the player copies."""

from __future__ import annotations

import io
import json
import tempfile
import unittest
from unittest.mock import patch
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from tools.rx3_sprites.capacity import report
from tools.rx3_sprites.census import CensusError, histogram, summarize
from tools.rx3_sprites.cli import main as sprites_main
from tools.rx3_sprites.fixture import checker, proof_8
from tools.rx3_sprites.limits import MEMORY_CEILING
from tools.rx3_sprites.pack import PackError, decode, encode
from tools.rx3_sprites.provision import write_tree
from tools.rx3_sprites.quantize import QuantizeError, quantize
from tools.rx3_sprites.select import choose
from tools.rx3_sprites.source import SourceError, compile_source, load_source
from tools.rx3_sprites.timing import fps_frame, phase_frame


def _png(path: Path, width: int, height: int, frame: int) -> None:
    from PIL import Image

    image = Image.new("RGBA", (width, height))
    image.putdata(checker(width, height, frame))
    image.save(path)


class TimingTests(unittest.TestCase):
    def test_fixed_rate_skips_ahead(self):
        self.assertEqual(fps_frame(500, 10, 1, 4), 1)
        self.assertEqual(fps_frame(0, 12, 1, 8), 0)

    def test_phase_zero_seek_and_missing_clock(self):
        self.assertEqual(phase_frame(0, 1, 1, 4), 0)
        self.assertEqual(phase_frame(2500, 1, 1, 4), 2)
        self.assertIsNone(phase_frame(None, 1, 1, 4))


class PackTests(unittest.TestCase):
    def test_round_trip_and_hard_mask(self):
        frames = [checker(8, 8, 0), checker(8, 8, 1)]
        palette, packed = quantize(frames, proof_8())
        self.assertEqual(palette[:2], b"\x00\x00")
        self.assertEqual(packed[0][0], 1)
        self.assertEqual(packed[0][4], 0)
        from tools.rx3_sprites.pack import Pack

        pack = Pack(
            name="squares",
            width=8,
            height=8,
            mode=1,
            fps_num=10,
            fps_den=1,
            frames_per_beat=0,
            beats_per_loop=0,
            anchor=1,
            proof=proof_8(),
            palette=palette,
            frames=tuple(packed),
        )
        blob = encode(pack)
        again = decode(blob)
        self.assertEqual(again.frames, pack.frames)
        self.assertGreater(len(blob), 64)
        broken = bytearray(blob)
        broken[-1] ^= 0xFF
        with self.assertRaises(PackError):
            decode(bytes(broken))
        with self.assertRaises(PackError):
            decode(blob + b"\x00")

    def test_alpha_cutoff(self):
        faint = [(255, 0, 0, 127)]
        solid = [(255, 0, 0, 128)]
        _, faint_frames = quantize([faint], proof_8())
        _, solid_frames = quantize([solid], proof_8())
        self.assertEqual(faint_frames[0], b"\x00")
        self.assertEqual(solid_frames[0], b"\x01")
        from dataclasses import replace
        with self.assertRaises(QuantizeError):
            quantize([solid], replace(proof_8(), bpp=4))

    def test_too_many_colours_share_one_palette(self):
        pixels = [(index % 32 * 8, (index // 32) % 32 * 8, 0, 255) for index in range(300)]
        palette, packed = quantize([pixels], proof_8())
        self.assertLessEqual(len(palette) // 2, 256)
        self.assertEqual(len(packed[0]), 300)
        self.assertTrue(all(pixel != 0 for pixel in packed[0]))

    def test_source_sheet_and_bool_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _png(root / "b.png", 8, 8, 1)
            _png(root / "a.png", 8, 8, 0)
            manifest = {
                "schema": 1,
                "name": "squares",
                "layout": "sequence",
                "width": 8,
                "height": 8,
                "frame_count": 2,
                "mode": "fps",
                "fps_num": 8,
                "fps_den": 1,
                "frames_per_beat": 0,
                "beats_per_loop": 0,
                "anchor": "center",
            }
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            pack = compile_source(load_source(root), proof_8())
            self.assertEqual(pack.frame_count, 2)
            sheet = root / "sheet"
            sheet.mkdir()
            from PIL import Image
            image = Image.new("RGBA", (16, 8))
            image.paste(Image.open(root / "a.png"), (0, 0))
            image.paste(Image.open(root / "b.png"), (8, 0))
            image.save(sheet / "sheet.png")
            sheet_manifest = dict(manifest, layout="sheet", columns=2, rows=1)
            (sheet / "manifest.json").write_text(json.dumps(sheet_manifest), encoding="utf-8")
            self.assertEqual(compile_source(load_source(sheet), proof_8()).frame_count, 2)
            manifest["fps_num"] = True
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaises(SourceError):
                load_source(root)


class ProvisionTests(unittest.TestCase):
    def test_usb_write_is_atomic_and_leaves_siblings_alone(self):
        from tools.rx3_sprites.pack import Pack

        frames = [checker(8, 8, 0)]
        palette, packed = quantize(frames, proof_8())
        pack = Pack(
            name="squares", width=8, height=8, mode=2, fps_num=0, fps_den=0,
            frames_per_beat=1, beats_per_loop=1, anchor=2, proof=proof_8(),
            palette=palette, frames=tuple(packed),
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "PIONEER").mkdir()
            (root / "PIONEER" / "keep.txt").write_text("leave me", encoding="utf-8")
            written = write_tree(root, [("b.rx3spr", pack), ("a.rx3spr", pack)])
            catalog = (written / "catalog.txt").read_text(encoding="utf-8")
            self.assertEqual(catalog, "a.rx3spr\nb.rx3spr\n")
            self.assertFalse((written / "tracks.rx3idx").exists())
            self.assertFalse((written / "format.proof").exists())
            self.assertFalse((written / "anchors.proof").exists())
            self.assertEqual((root / "PIONEER" / "keep.txt").read_text(encoding="utf-8"), "leave me")
            self.assertIn("exceed", report(MEMORY_CEILING + 1, 10))
            # macOS deletes the AppleDouble sidecar when the real file goes.
            (written / "._catalog.txt").write_bytes(b"resource")
            real_unlink = Path.unlink

            def unlink(self, *args, **kwargs):
                shadow = None
                if not self.name.startswith("._"):
                    shadow = self.with_name("._" + self.name)
                real_unlink(self, *args, **kwargs)
                if shadow is not None and shadow.exists():
                    real_unlink(shadow)

            with patch.object(Path, "unlink", unlink):
                again = write_tree(root, [("a.rx3spr", pack)])
            self.assertEqual(
                (again / "catalog.txt").read_text(encoding="utf-8"),
                "a.rx3spr\n",
            )
            self.assertFalse(any(child.name.startswith(".rx3-sprites-") for child in root.iterdir()))

    def test_choice_does_not_repeat_when_another_pack_exists(self):
        names = ["a.rx3spr", "b.rx3spr"]
        first = choose(names, "demo", 0, 1, None)
        second = choose(names, "demo", 0, 1, first)
        self.assertNotEqual(first, second)
        self.assertEqual(choose(["only.rx3spr"], "demo", 1, 1, "only.rx3spr"), "only.rx3spr")


def _source(root: Path) -> None:
    _png(root / "a.png", 8, 8, 0)
    manifest = {
        "schema": 1,
        "name": "squares",
        "layout": "sequence",
        "width": 8,
        "height": 8,
        "frame_count": 1,
        "mode": "fps",
        "fps_num": 8,
        "fps_den": 1,
        "frames_per_beat": 0,
        "beats_per_loop": 0,
        "anchor": "center",
    }
    (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")


class CliTests(unittest.TestCase):
    def test_missing_source_exits_2(self):
        stderr = io.StringIO()
        with redirect_stderr(stderr):
            self.assertEqual(sprites_main(["preview"]), 2)
            self.assertEqual(sprites_main(["write", "--dest", "/tmp"]), 2)

    def test_preview_and_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "art"
            source.mkdir()
            _source(source)
            stdout = io.StringIO()
            stderr = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                preview = sprites_main(["preview", "--source", str(source), "-vv"])
            self.assertEqual(preview, 0)
            self.assertEqual(stdout.getvalue(), "")
            self.assertIn("INFO rx3.sprites: squares fps 8x8 1 frames", stderr.getvalue())
            self.assertIn("DEBUG rx3.sprites: a.png", stderr.getvalue())
            self.assertFalse((root / "RX3_SPRITES").exists())
            stdout = io.StringIO()
            stderr = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                written = sprites_main([
                    "write", "--source", str(source), "--dest", str(root),
                ])
            self.assertEqual(written, 0)
            self.assertEqual(stdout.getvalue(), "")
            text = stderr.getvalue()
            tree = root / "RX3_SPRITES"
            self.assertNotIn("unproven", text)
            self.assertNotIn("anchor proof", text)
            self.assertTrue((tree / "squares.rx3spr").is_file())
            self.assertTrue((tree / "catalog.txt").is_file())
            self.assertFalse((tree / "tracks.rx3idx").exists())
            self.assertFalse((tree / "format.proof").exists())
            self.assertFalse((tree / "anchors.proof").exists())


class ToolboxTests(unittest.TestCase):
    def test_sprite_pane_self_test(self):
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "apps" / "rx3-toolbox"))
        import sprite_studio

        sprite_studio.self_test()


class CensusTests(unittest.TestCase):
    def test_structural_summary_only(self):
        record = bytearray(44)
        record[4:6] = (8).to_bytes(2, "little")
        record[6:8] = (8).to_bytes(2, "little")
        record[0x18] = 2
        record[0x19] = 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "table.bin"
            path.write_bytes(bytes(record) * 2)
            rows = summarize(path)
            self.assertEqual(histogram(rows), [(2, 1, 2)])
            path.write_bytes(b"\x00" * 10)
            with self.assertRaises(CensusError):
                summarize(path)


if __name__ == "__main__":
    unittest.main()

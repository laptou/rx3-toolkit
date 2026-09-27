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
from tools.rx3_sprites.fixture import checker, probe_blob, proof_8
from tools.rx3_sprites.limits import MEMORY_CEILING
from tools.rx3_sprites.pack import PackError, decode, encode
from tools.rx3_sprites.proof import (
    Anchor,
    ProofError,
    anchors_from_trace,
    parse_anchors,
    parse_format,
    trace_fit_edge,
)
from tools.rx3_sprites.provision import write_tree
from tools.rx3_sprites.quantize import quantize
from tools.rx3_sprites.select import choose
from tools.rx3_sprites.source import SourceError, compile_source, fit_source, load_source
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
        with self.assertRaises(ProofError):
            parse_format(
                "RX3FMT1\nformat 2\npalette 1\nbpp 4\ntransparent 0\nentry 2\n"
            )

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


class ProofAndProvisionTests(unittest.TestCase):
    def test_trace_keeps_only_a_stable_corner(self):
        text = "\n".join((
            "100 1 0 0 400 80",
            "100 1 20 20 420 100",
            "200 2 800 400 1270 480",
            "200 2 0 400 400 480",
            "300 3 700 0 1100 80",
            "400 4 0 400 500 460",
            "500 5 0 400 500 460",
            "600 6 10 10 30 30",
        ))
        self.assertEqual(
            anchors_from_trace(text),
            (Anchor(100, 1, 0),),
        )
        tied = "\n".join((
            "1 10 800 400 1200 480",
            "1 11 800 400 1200 480",
        ))
        self.assertEqual(anchors_from_trace(tied), ())
        measured = "257 3449 756 532 1260 592\n257 3462 754 117 806 136\n"
        self.assertEqual(trace_fit_edge(measured), 60)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _png(root / "a.png", 64, 64, 0)
            manifest = {
                "schema": 1, "name": "big", "layout": "sequence",
                "width": 64, "height": 64, "frame_count": 1, "mode": "fps",
                "fps_num": 8, "fps_den": 1, "frames_per_beat": 0,
                "beats_per_loop": 0, "anchor": "center",
            }
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            fitted = fit_source(load_source(root), 60)
        self.assertEqual(fitted.frames[0].size, (60, 60))

    def test_ambiguous_anchor_is_rejected(self):
        text = "RX3ANC1\nlayer 1 id 2 deck 0\nlayer 1 id 2 deck 1\n"
        with self.assertRaises(ProofError):
            parse_anchors(text)
        proof = parse_format(proof_8().text())
        self.assertEqual(proof.bpp, 8)

    def test_probe_corners_are_transparent(self):
        blob = probe_blob()
        palette = 4
        self.assertEqual(blob[0], 0)
        self.assertEqual(blob[palette], 0)
        self.assertEqual(blob[palette + 7], 0)
        self.assertEqual(blob[palette + 56], 0)
        self.assertEqual(blob[palette + 63], 0)
        self.assertNotEqual(blob[palette + 27], 0)

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
            written = write_tree(
                root,
                [("b.rx3spr", pack), ("a.rx3spr", pack)],
                format_proof=proof_8().text(),
                anchor_proof="RX3ANC1\nlayer 3 id 9 deck 0\nlayer 4 id 10 deck 1\n",
            )
            catalog = (written / "catalog.txt").read_text(encoding="utf-8")
            self.assertEqual(catalog, "a.rx3spr\nb.rx3spr\n")
            self.assertFalse((written / "tracks.rx3idx").exists())
            self.assertEqual((root / "PIONEER" / "keep.txt").read_text(encoding="utf-8"), "leave me")
            self.assertIn("exceed", report(MEMORY_CEILING + 1, 10))
            # macOS deletes the AppleDouble sidecar when the real file goes.
            (written / "._format.proof").write_bytes(b"resource")
            real_unlink = Path.unlink

            def unlink(self, *args, **kwargs):
                shadow = None
                if not self.name.startswith("._"):
                    shadow = self.with_name("._" + self.name)
                real_unlink(self, *args, **kwargs)
                if shadow is not None and shadow.exists():
                    real_unlink(shadow)

            with patch.object(Path, "unlink", unlink):
                again = write_tree(
                    root,
                    [("a.rx3spr", pack)],
                    format_proof=proof_8().text(),
                )
            self.assertEqual(
                (again / "format.proof").read_text(encoding="utf-8"),
                proof_8().text(),
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
            self.assertEqual(
                (tree / "format.proof").read_text(encoding="utf-8"),
                "RX3FMT1\nformat 2\npalette 1\nbpp 8\ntransparent 0\nentry 2\n",
            )
            self.assertIn(
                "WARNING rx3.sprites: no anchor proof; the player will load the pack and draw nothing",
                text,
            )
            self.assertTrue((tree / "squares.rx3spr").is_file())
            self.assertTrue((tree / "catalog.txt").is_file())
            self.assertFalse((tree / "tracks.rx3idx").exists())
            self.assertFalse((tree / "anchors.proof").exists())

    def test_write_reads_waveform_anchors_from_the_stick_trace(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "art"
            source.mkdir()
            _source(source)
            runtime = root / "RX3_RUNTIME"
            runtime.mkdir()
            # the measured log: icons, one lower-right waveform, no upper-left one.
            (runtime / "sprite-trace.log").write_text(
                "257 3462 754 117 806 136\n"
                "257 3449 756 532 1260 592\n"
                "257 4294967295 2 102 0 0\n"
                "513 3041 511 65 611 125\n"
                "1025 3245 510 14 610 74\n",
                encoding="utf-8",
            )
            stderr = io.StringIO()
            with redirect_stderr(stderr):
                written = sprites_main([
                    "write", "--source", str(source), "--dest", str(root),
                ])
            self.assertEqual(written, 0)
            proof = (root / "RX3_SPRITES" / "anchors.proof").read_text(encoding="utf-8")
            self.assertEqual(proof, "RX3ANC1\nlayer 257 id 3449 deck 1\n")
            self.assertIn("anchor from sprite trace: layer 257 id 3449 deck 1", stderr.getvalue())
            self.assertIn("no waveform for deck 0", stderr.getvalue())


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

# SPDX-License-Identifier: MPL-2.0
"""Sprite packs, beat indexes, and the rules the player copies."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from pyrekordbox.anlz.structs import AnlzFileHeader, AnlzTag

from tools.rx3_sprites.beatgrid import BeatGridError, grid_from_anlz, grids_from_tree
from tools.rx3_sprites.capacity import report
from tools.rx3_sprites.census import CensusError, histogram, summarize
from tools.rx3_sprites.fixture import checker, probe_blob, proof_8
from tools.rx3_sprites.index import IndexError, decode_index, encode_index
from tools.rx3_sprites.limits import MEMORY_CEILING
from tools.rx3_sprites.pack import PackError, decode, encode
from tools.rx3_sprites.proof import FormatProof, ProofError, parse_anchors, parse_format
from tools.rx3_sprites.provision import write_tree
from tools.rx3_sprites.quantize import QuantizeError, quantize
from tools.rx3_sprites.select import choose
from tools.rx3_sprites.source import SourceError, compile_source, load_source
from tools.rx3_sprites.timing import Segment, beat_frame, fps_frame
from tools.rx3_stems.rekordbox import export_stem


def _anlz(path_text: str, beats: list[tuple[int, int, int]]) -> bytes:
    encoded = path_text.encode("utf-16-be")
    length = len(encoded) + 2
    ppth = AnlzTag.build({
        "type": "PPTH",
        "len_header": 16,
        "len_tag": 16 + length,
        "content": {"len_path": length, "path": path_text},
    })
    pqtz = AnlzTag.build({
        "type": "PQTZ",
        "len_header": 24,
        "len_tag": 24 + 8 * len(beats),
        "content": {
            "u2": 0x80000,
            "entry_count": len(beats),
            "entries": [
                {"beat": beat, "tempo": tempo, "time": time_ms}
                for beat, tempo, time_ms in beats
            ],
        },
    })
    body = ppth + pqtz
    header = AnlzFileHeader.build({
        "type": "PMAI",
        "len_header": 28,
        "len_file": 28 + len(body),
        "u1": 0,
        "u2": 0,
        "u3": 0,
        "u4": 0,
    })
    return header + body


def _png(path: Path, width: int, height: int, frame: int) -> None:
    from PIL import Image

    image = Image.new("RGBA", (width, height))
    image.putdata(checker(width, height, frame))
    image.save(path)


class TimingTests(unittest.TestCase):
    def test_fixed_rate_skips_ahead(self):
        self.assertEqual(fps_frame(500, 10, 1, 4), 1)
        self.assertEqual(fps_frame(0, 12, 1, 8), 0)

    def test_seek_uses_absolute_phase(self):
        segments = (Segment(0, 120000),)
        self.assertEqual(beat_frame(-1, segments, 4, 1), 0)
        self.assertEqual(beat_frame(11025, segments, 4, 1), 2)

    def test_variable_grid_interpolates_then_extrapolates(self):
        segments = (Segment(0, 120000), Segment(22050, 60000))
        self.assertEqual(beat_frame(11025, segments, 4, 1), 2)
        self.assertEqual(beat_frame(44100, segments, 4, 1), 2)


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

    def test_alpha_cutoff_and_four_bit_limit(self):
        faint = [(255, 0, 0, 127)]
        solid = [(255, 0, 0, 128)]
        _, faint_frames = quantize([faint], proof_8())
        _, solid_frames = quantize([solid], proof_8())
        self.assertEqual(faint_frames[0], b"\x00")
        self.assertEqual(solid_frames[0], b"\x01")
        many = [[(index * 16, 0, 0, 255) for index in range(16)]]
        with self.assertRaises(QuantizeError):
            quantize(many, FormatProof(2, 1, 4, 0, 2))

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


class BeatIndexTests(unittest.TestCase):
    def test_anlz_round_trip_and_collision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "ANLZ0001.DAT"
            second = root / "nested" / "ANLZ0002.EXT"
            second.parent.mkdir()
            first.write_bytes(_anlz(
                "C:/music/Square Pulse.mp3",
                [(1, 12000, 0), (2, 6000, 500)],
            ))
            grid = grid_from_anlz(first)
            self.assertEqual(grid.identity, "Square Pulse")
            self.assertEqual(grid.segments[1].start_samples, 22050)
            blob = encode_index((grid,))
            decoded = decode_index(blob)
            self.assertEqual(decoded[0].segments, grid.segments)
            broken = bytearray(blob)
            broken[-1] ^= 1
            with self.assertRaises(IndexError):
                decode_index(bytes(broken))
            second.write_bytes(_anlz("D:/other/Square Pulse.wav", [(1, 10000, 0)]))
            with self.assertRaises(BeatGridError):
                grids_from_tree(root)
            second.write_bytes(_anlz("D:/other/Other Pulse.wav", [(1, 10000, 0)]))
            found = grids_from_tree(root)
            self.assertEqual([item.identity for item in found], ["Other Pulse", "Square Pulse"])

    def test_long_export_name_matches_stems(self):
        raw = "x" * 46
        self.assertEqual(export_stem(raw), "x" * 44)


class ProofAndProvisionTests(unittest.TestCase):
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
            self.assertTrue((written / "tracks.rx3idx").is_file())
            self.assertEqual((root / "PIONEER" / "keep.txt").read_text(encoding="utf-8"), "leave me")
            self.assertIn("exceed", report(MEMORY_CEILING + 1, 10))

    def test_choice_does_not_repeat_when_another_pack_exists(self):
        names = ["a.rx3spr", "b.rx3spr"]
        first = choose(names, "demo", 0, 1, None)
        second = choose(names, "demo", 0, 1, first)
        self.assertNotEqual(first, second)
        self.assertEqual(choose(["only.rx3spr"], "demo", 1, 1, "only.rx3spr"), "only.rx3spr")


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

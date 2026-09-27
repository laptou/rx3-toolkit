# SPDX-License-Identifier: MPL-2.0
"""Animated sprites pane: validate, preview, and write RX3_SPRITES."""

from __future__ import annotations

import json
import pathlib
import sys
import tempfile
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

if __package__ in (None, ""):
    _HERE = pathlib.Path(__file__).resolve().parent
    sys.path.insert(0, str(_HERE))
    sys.path.insert(0, str(_HERE.parents[1]))

from tools.rx3_sprites.capacity import directory_bytes, report
from tools.rx3_sprites.fixture import proof_8
from tools.rx3_sprites.proof import ProofError, parse_anchors, parse_format
from tools.rx3_sprites.provision import ProvisionError, scan_grids, write_tree
from tools.rx3_sprites.source import SourceError, compile_source, load_source

import theme


class SpriteStudioPane(ttk.Frame):
    """One source folder in, one RX3_SPRITES directory out."""

    def __init__(self, master: tk.Misc):
        super().__init__(master, padding=16)
        self.source = tk.StringVar()
        self.usb = tk.StringVar()
        self.destination = tk.StringVar()
        self.format_text = tk.StringVar(value=proof_8().text().strip())
        self.anchor_text = tk.StringVar()
        self.status = tk.StringVar(value="Choose a sprite folder.")
        self._rows()

    def restyle(self, palette: theme.Palette) -> None:
        del palette

    def _rows(self) -> None:
        fields = (
            ("Sprite folder", self.source, self._pick_source),
            ("Rekordbox USB", self.usb, self._pick_usb),
            ("Destination", self.destination, self._pick_destination),
        )
        for row, (label, variable, command) in enumerate(fields):
            ttk.Label(self, text=label, width=16).grid(row=row, column=0, sticky="w", pady=4)
            ttk.Entry(self, textvariable=variable).grid(row=row, column=1, sticky="ew", pady=4)
            ttk.Button(self, text="Browse", command=command).grid(row=row, column=2, padx=(8, 0))
        ttk.Label(self, text="Format proof").grid(row=3, column=0, sticky="nw", pady=4)
        self.format_box = tk.Text(self, height=6, width=40)
        self.format_box.grid(row=3, column=1, columnspan=2, sticky="ew", pady=4)
        self.format_box.insert("1.0", self.format_text.get())
        ttk.Label(self, text="Anchor proof").grid(row=4, column=0, sticky="nw", pady=4)
        self.anchor_box = tk.Text(self, height=5, width=40)
        self.anchor_box.grid(row=4, column=1, columnspan=2, sticky="ew", pady=4)
        buttons = ttk.Frame(self)
        buttons.grid(row=5, column=0, columnspan=3, sticky="w", pady=8)
        ttk.Button(buttons, text="Preview", command=self.preview).pack(side="left")
        ttk.Button(buttons, text="Write USB folder", command=self.write).pack(side="left", padx=8)
        ttk.Label(self, textvariable=self.status, wraplength=640).grid(
            row=6, column=0, columnspan=3, sticky="w"
        )
        self.columnconfigure(1, weight=1)

    def _pick_source(self) -> None:
        chosen = filedialog.askdirectory(title="Sprite source folder")
        if chosen:
            self.source.set(chosen)

    def _pick_usb(self) -> None:
        chosen = filedialog.askdirectory(title="Rekordbox USB root")
        if chosen:
            self.usb.set(chosen)

    def _pick_destination(self) -> None:
        chosen = filedialog.askdirectory(title="Destination USB root")
        if chosen:
            self.destination.set(chosen)

    def preview(self) -> None:
        try:
            summary = preview_summary(
                pathlib.Path(self.source.get()),
                self.format_box.get("1.0", "end"),
            )
        except (SourceError, ProofError, OSError) as error:
            self.status.set(str(error))
            return
        self.status.set(summary)

    def write(self) -> None:
        try:
            message = provision(
                pathlib.Path(self.source.get()),
                pathlib.Path(self.usb.get()) if self.usb.get() else None,
                pathlib.Path(self.destination.get()),
                self.format_box.get("1.0", "end"),
                self.anchor_box.get("1.0", "end"),
            )
        except (SourceError, ProofError, ProvisionError, OSError) as error:
            self.status.set(str(error))
            messagebox.showerror("Sprites", str(error))
            return
        self.status.set(message)


def preview_summary(source: pathlib.Path, format_text: str) -> str:
    proof = parse_format(format_text)
    loaded = load_source(source)
    pack = compile_source(loaded, proof)
    mode = "fixed rate" if pack.mode == 1 else "beat grid"
    return (
        f"{pack.name}: {pack.width}x{pack.height}, {pack.frame_count} frames, "
        f"{mode}, palette {len(pack.palette)} bytes"
    )


def provision(
    source: pathlib.Path,
    usb: pathlib.Path | None,
    destination: pathlib.Path,
    format_text: str,
    anchor_text: str,
) -> str:
    proof = parse_format(format_text)
    anchors = anchor_text.strip()
    if anchors:
        parse_anchors(anchors if anchors.endswith("\n") else anchors + "\n")
    loaded = load_source(source)
    pack = compile_source(loaded, proof)
    grids = scan_grids(usb) if usb is not None and str(usb) else ()
    missing = ""
    if pack.mode == 2 and not grids:
        missing = " No beat grid was found, so this beat-mode pack will not play."
    root = write_tree(
        destination,
        [(f"{pack.name}.rx3spr", pack)],
        grids,
        format_proof=proof.text(),
        anchor_proof=(
            None if not anchors
            else anchors if anchors.endswith("\n") else anchors + "\n"
        ),
    )
    sprite_bytes = (root / f"{pack.name}.rx3spr").stat().st_size
    stem_bytes = directory_bytes(destination / "RX3_STEMS", ".rx3stem")
    return report(sprite_bytes, stem_bytes) + missing


def self_test() -> None:
    from PIL import Image

    with tempfile.TemporaryDirectory(prefix="rx3-sprite-studio-self-test-") as directory:
        root = pathlib.Path(directory)
        source = root / "source"
        source.mkdir()
        image = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        image.putpixel((3, 3), (255, 255, 255, 255))
        image.save(source / "frame.png")
        (source / "manifest.json").write_text(json.dumps({
            "schema": 1,
            "name": "dot",
            "layout": "sequence",
            "width": 8,
            "height": 8,
            "frame_count": 1,
            "mode": "fps",
            "fps_num": 4,
            "fps_den": 1,
            "frames_per_beat": 0,
            "beats_per_loop": 0,
            "anchor": "bottom-center",
        }), encoding="utf-8")
        message = provision(
            source, None, root / "usb", proof_8().text(),
            "RX3ANC1\nlayer 1 id 2 deck 0\n",
        )
        if "sprites" not in message:
            raise RuntimeError(message)
        if not (root / "usb" / "RX3_SPRITES" / "dot.rx3spr").is_file():
            raise RuntimeError("sprite self-test did not write a pack")

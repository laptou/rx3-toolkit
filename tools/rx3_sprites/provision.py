# SPDX-License-Identifier: MPL-2.0
"""Write an RX3_SPRITES directory without touching Rekordbox media."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path

from tools.rx3_sprites.beatgrid import TrackGrid, grids_from_tree
from tools.rx3_sprites.index import encode_index
from tools.rx3_sprites.pack import Pack, encode


class ProvisionError(ValueError):
    """The destination could not be updated."""


def write_tree(
    destination: Path,
    packs: list[tuple[str, Pack]],
    grids: tuple[TrackGrid, ...] = (),
    format_proof: str | None = None,
    anchor_proof: str | None = None,
) -> Path:
    """Atomically replace RX3_SPRITES under `destination`.

    `destination` is the USB root or any folder that should contain RX3_SPRITES.
    Existing Rekordbox files outside that directory are not opened for writing.
    """
    root = destination / "RX3_SPRITES"
    if root.exists() and not root.is_dir():
        raise ProvisionError("RX3_SPRITES exists and is not a directory")
    names = [name for name, _pack in packs]
    if any(not _safe_name(name) for name in names):
        raise ProvisionError("sprite filename is unsafe")
    if len(names) != len(set(names)):
        raise ProvisionError("sprite filenames collide")
    parent = destination
    parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".rx3-sprites-", dir=parent))
    try:
        catalog = "".join(f"{name}\n" for name in sorted(names))
        _write(temporary / "catalog.txt", catalog.encode("utf-8"))
        _write(temporary / "tracks.rx3idx", encode_index(grids))
        if format_proof is not None:
            _write(temporary / "format.proof", format_proof.encode("utf-8"))
        if anchor_proof is not None:
            _write(temporary / "anchors.proof", anchor_proof.encode("utf-8"))
        for name, pack in packs:
            _write(temporary / name, encode(pack))
        if root.exists():
            retired = Path(tempfile.mkdtemp(prefix=".rx3-sprites-old-", dir=parent))
            os.replace(root, retired / "old")
            os.replace(temporary, root)
            _remove_tree(retired)
        else:
            os.replace(temporary, root)
    except Exception:
        _remove_tree(temporary)
        raise
    return root


def scan_grids(usb_root: Path) -> tuple[TrackGrid, ...]:
    if not usb_root.is_dir():
        raise ProvisionError("Rekordbox USB root is not a directory")
    return grids_from_tree(usb_root)


def _safe_name(name: str) -> bool:
    return (
        name.endswith(".rx3spr")
        and not name.startswith(".")
        and "/" not in name
        and "\\" not in name
        and name == Path(name).name
    )


def _write(path: Path, data: bytes) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_bytes(data)
    os.replace(temporary, path)


def _remove_tree(path: Path) -> None:
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), reverse=True):
        if child.is_file() or child.is_symlink():
            child.unlink()
        else:
            child.rmdir()
    path.rmdir()

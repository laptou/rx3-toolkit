# SPDX-License-Identifier: MPL-2.0
"""Write an RX3_SPRITES directory without reading Rekordbox analysis files."""

from __future__ import annotations

import logging
import os
import tempfile
from pathlib import Path

from tools.rx3_sprites.pack import Pack, encode
from tools.rx3_sprites.proof import parse_anchors, parse_format
from tools.rx3_sprites.source import compile_source, load_source


log = logging.getLogger("rx3.sprites")


class ProvisionError(ValueError):
    """The destination could not be updated."""


def write_tree(
    destination: Path,
    packs: list[tuple[str, Pack]],
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
    written_bytes = 0
    try:
        catalog = "".join(f"{name}\n" for name in sorted(names))
        _write(temporary / "catalog.txt", catalog.encode("utf-8"))
        log.debug("catalog.txt")
        if format_proof is not None:
            _write(temporary / "format.proof", format_proof.encode("utf-8"))
            log.debug("format.proof")
        if anchor_proof is not None:
            _write(temporary / "anchors.proof", anchor_proof.encode("utf-8"))
            log.debug("anchors.proof")
        for name, pack in packs:
            blob = encode(pack)
            _write(temporary / name, blob)
            log.debug("%s", name)
            written_bytes += len(blob)
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
    log.info("wrote %s (%s bytes)", root, written_bytes)
    return root


def provision(
    source: Path,
    destination: Path,
    format_text: str,
    anchor_text: str,
) -> str:
    """Compile one source folder and write RX3_SPRITES. No analysis scan."""
    from tools.rx3_sprites.capacity import directory_bytes, report

    proof = parse_format(format_text)
    anchors = anchor_text.strip()
    if anchors:
        parse_anchors(anchors if anchors.endswith("\n") else anchors + "\n")
    else:
        log.warning("no anchor proof; the player will load the pack and draw nothing")
    loaded = load_source(source)
    pack = compile_source(loaded, proof)
    root = write_tree(
        destination,
        [(f"{pack.name}.rx3spr", pack)],
        format_proof=proof.text(),
        anchor_proof=(
            None if not anchors
            else anchors if anchors.endswith("\n") else anchors + "\n"
        ),
    )
    sprite_bytes = (root / f"{pack.name}.rx3spr").stat().st_size
    stem_bytes = directory_bytes(destination / "RX3_STEMS", ".rx3stem")
    return report(sprite_bytes, stem_bytes)


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

# SPDX-License-Identifier: MPL-2.0
"""Write an RX3_SPRITES directory without reading Rekordbox analysis files."""

from __future__ import annotations

import logging
import os
import tempfile
from pathlib import Path

from tools.rx3_sprites.pack import Pack, PackError, decode, encode
from tools.rx3_sprites.proof import proven_format
from tools.rx3_sprites.source import collect_sources, compile_source, load_source


log = logging.getLogger("rx3.sprites")


class ProvisionError(ValueError):
    """The destination could not be updated."""


def write_tree(destination: Path, packs: list[tuple[str, Pack]]) -> Path:
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
    _sweep_leftovers(parent)
    temporary = Path(tempfile.mkdtemp(prefix=".rx3-sprites-", dir=parent))
    written_bytes = 0
    published = False
    try:
        catalog = "".join(f"{name}\n" for name in sorted(names))
        _write(temporary / "catalog.txt", catalog.encode("utf-8"))
        log.debug("catalog.txt")
        for name, pack in packs:
            blob = encode(pack)
            _write(temporary / name, blob)
            log.debug("%s", name)
            written_bytes += len(blob)
        if root.exists():
            retired = Path(tempfile.mkdtemp(prefix=".rx3-sprites-old-", dir=parent))
            os.replace(root, retired / "old")
            os.replace(temporary, root)
            published = True
            _remove_tree(retired)
        else:
            os.replace(temporary, root)
            published = True
    except Exception:
        if not published:
            _remove_tree(temporary)
        raise
    log.info("wrote %s (%s bytes)", root, written_bytes)
    return root


def provision(sources: list[Path], destination: Path) -> str:
    """Compile sprite folders and merge them into RX3_SPRITES.

    Packs already stored under `destination` stay. A compiled pack replaces
    the stored pack with the same filename. No analysis scan.
    """
    from tools.rx3_sprites.capacity import directory_bytes, report

    compiled: list[tuple[str, Pack]] = []
    for folder in collect_sources(sources):
        pack = compile_source(load_source(folder), proven_format())
        compiled.append((f"{pack.name}.rx3spr", pack))
    names = [name for name, _pack in compiled]
    if len(names) != len(set(names)):
        raise ProvisionError("sprite filenames collide")
    merged = dict(_existing_packs(destination))
    for name, pack in compiled:
        merged[name] = pack
    root = write_tree(destination, list(merged.items()))
    sizes = [(root / name).stat().st_size for name in merged]
    stem_bytes = directory_bytes(destination / "RX3_STEMS", ".rx3stem")
    return report(sizes, stem_bytes)


def _existing_packs(destination: Path) -> list[tuple[str, Pack]]:
    """Read packs already published. A bad pack aborts before anything is replaced."""
    root = destination / "RX3_SPRITES"
    if not root.exists():
        return []
    if not root.is_dir():
        raise ProvisionError("RX3_SPRITES exists and is not a directory")
    found: list[tuple[str, Pack]] = []
    for child in sorted(root.iterdir()):
        if (
            not child.is_file()
            or child.name.startswith(".")
            or not child.name.endswith(".rx3spr")
        ):
            continue
        if not _safe_name(child.name):
            raise ProvisionError("sprite filename is unsafe")
        try:
            pack = decode(child.read_bytes())
        except PackError as error:
            raise ProvisionError(f"{child.name}: {error}") from error
        found.append((child.name, pack))
    return found


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


def _sweep_leftovers(parent: Path) -> None:
    """Drop folders left by a write that died while retiring the previous tree."""
    for child in parent.iterdir():
        if child.name.startswith(".rx3-sprites-") and child.is_dir():
            _remove_tree(child)


def _remove_tree(path: Path) -> None:
    """Delete a directory this command created.

    On a FAT stick, macOS stores an AppleDouble file named `._name` beside
    each file it writes. Unlinking the real file removes that sidecar too, so
    a name the walk already listed can be gone by the time we reach it.
    """
    if not path.exists():
        return
    for child in sorted(path.rglob("*"), reverse=True):
        try:
            if child.is_symlink() or child.is_file():
                child.unlink(missing_ok=True)
            elif child.exists():
                child.rmdir()
        except FileNotFoundError:
            continue
    path.rmdir()

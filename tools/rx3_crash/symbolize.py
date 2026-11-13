# SPDX-License-Identifier: MPL-2.0
"""Name the addresses in a crash report.

Addresses inside the performance core go through llvm-symbolizer against the
library that shipped in autoexec.bin, which carries its DWARF. rbp is stripped
and not position independent, so its addresses are printed as they are, the
same numbers the hook's own constants use. Anything else is file plus offset.
"""

from __future__ import annotations

import io
import pathlib
import shutil
import subprocess

from tools.rx3_crash.report import Crash, Mapping


HOOK_NAME = "librx3_core.so"
HOOK_ISO_PATH = "/modules/core/librx3_core.so"
SYMBOLIZER_FALLBACK = pathlib.Path("/opt/homebrew/opt/llvm/bin/llvm-symbolizer")


def hook_from_autoexec(autoexec: pathlib.Path, key: pathlib.Path) -> bytes:
    """The performance core exactly as the stick ships it."""
    import pycdlib

    from tools.rx3_firmware import firmware_image

    plain = firmware_image.read_autoexec(autoexec, key)
    iso = pycdlib.PyCdlib()
    iso.open_fp(io.BytesIO(plain))
    try:
        output = io.BytesIO()
        iso.get_file_from_iso_fp(output, rr_path=HOOK_ISO_PATH)
    finally:
        iso.close()
    return output.getvalue()


def find_symbolizer() -> str | None:
    found = shutil.which("llvm-symbolizer")
    if found:
        return found
    return str(SYMBOLIZER_FALLBACK) if SYMBOLIZER_FALLBACK.is_file() else None


class Symbolizer:
    def __init__(self, hook: pathlib.Path | None, symbolizer: str | None):
        self.hook = hook
        self.symbolizer = symbolizer

    def describe(self, crash: Crash, address: int) -> str:
        mapping = crash.mapping_for(address)
        if mapping is None:
            return "unmapped"
        name = pathlib.PurePosixPath(mapping.path).name if mapping.path else "[anonymous]"
        if name == "rbp":
            return "rbp"
        if name == HOOK_NAME:
            offset = address - _load_base(crash, mapping)
            where = self._hook_line(offset)
            return f"{HOOK_NAME}+{offset:#x}" + (f" {where}" if where else "")
        offset = address - mapping.start + mapping.offset
        return f"{name}+{offset:#x}"

    def _hook_line(self, offset: int) -> str:
        if not self.hook or not self.symbolizer:
            return ""
        result = subprocess.run(
            [self.symbolizer, f"--obj={self.hook}", "--functions=linkage",
             "--inlining", f"{offset:#x}"],
            capture_output=True, text=True, check=False,
        )
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        frames = []
        for function, location in zip(lines[0::2], lines[1::2]):
            if function == "??":
                continue
            frames.append(f"{function} {pathlib.PurePosixPath(location).name}")
        return " <- ".join(frames)


def _load_base(crash: Crash, mapping: Mapping) -> int:
    """Start of the file's offset-0 mapping, where ELF address 0 sits."""
    for other in crash.maps:
        if other.path == mapping.path and other.offset == 0:
            return other.start
    return mapping.start - mapping.offset


def executable(crash: Crash, address: int) -> bool:
    mapping = crash.mapping_for(address)
    return bool(mapping and "x" in mapping.permissions)

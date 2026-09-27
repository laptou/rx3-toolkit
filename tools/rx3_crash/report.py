# SPDX-License-Identifier: MPL-2.0
"""Parse the crash reports the performance core appends to RX3_RUNTIME/crash-NNNN.txt."""

from __future__ import annotations

from dataclasses import dataclass, field


REGISTERS = (
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10",
    "fp", "ip", "sp", "lr", "pc", "cpsr",
)


class ReportError(ValueError):
    """The text is not a crash report."""


@dataclass(frozen=True)
class Mapping:
    start: int
    end: int
    permissions: str
    offset: int
    path: str

    def contains(self, address: int) -> bool:
        return self.start <= address < self.end


@dataclass
class Crash:
    signal: str = ""
    code: int = 0
    address: int = 0
    registers: dict[str, int] = field(default_factory=dict)
    maps: list[Mapping] = field(default_factory=list)
    stack: list[tuple[int, int]] = field(default_factory=list)

    def mapping_for(self, address: int) -> Mapping | None:
        for mapping in self.maps:
            if mapping.contains(address):
                return mapping
        return None


def parse(text: str) -> list[Crash]:
    """Every complete or truncated report in the file, oldest first."""
    crashes: list[Crash] = []
    current: Crash | None = None
    section = "header"
    for line in text.splitlines():
        if line == "=== rx3 crash ===":
            current = Crash()
            crashes.append(current)
            section = "header"
            continue
        if current is None:
            continue
        if line == "=== end ===":
            current = None
            continue
        if line == "--- maps ---":
            section = "maps"
            continue
        if line == "--- stack ---":
            section = "stack"
            continue
        if section == "header":
            _header(current, line)
        elif section == "maps":
            mapping = _mapping(line)
            if mapping:
                current.maps.append(mapping)
        else:
            parts = line.split()
            if len(parts) == 2:
                current.stack.append((int(parts[0], 16), int(parts[1], 16)))
    if not crashes:
        raise ReportError("no '=== rx3 crash ===' block in the file")
    return crashes


def _header(crash: Crash, line: str) -> None:
    parts = line.split()
    if len(parts) != 2:
        return
    name, value = parts
    if name == "signal":
        crash.signal = value
    elif name == "code":
        crash.code = int(value, 16)
    elif name == "addr":
        crash.address = int(value, 16)
    elif name in REGISTERS:
        crash.registers[name] = int(value, 16)


def _mapping(line: str) -> Mapping | None:
    # 00008000-005c8000 r-xp 00000000 00:01 1234   /root/pdj/rbp
    parts = line.split(None, 5)
    if len(parts) < 5 or "-" not in parts[0]:
        return None
    start, end = (int(item, 16) for item in parts[0].split("-", 1))
    path = parts[5].strip() if len(parts) == 6 else ""
    return Mapping(start, end, parts[1], int(parts[2], 16), path)

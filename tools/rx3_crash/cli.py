# SPDX-License-Identifier: MPL-2.0
"""Print a symbolised crash report from RX3_RUNTIME/crash-NNNN.txt."""

from __future__ import annotations

import argparse
import pathlib
import sys
import tempfile

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.rx3_crash.report import REGISTERS, Crash, ReportError, parse
from tools.rx3_crash.symbolize import Symbolizer, executable, find_symbolizer, hook_from_autoexec

# words on the stack that point into code. arm keeps return addresses there,
# so these are the likely callers, youngest first, with some stale noise.
STACK_CANDIDATES = 32


def render(crash: Crash, symbols: Symbolizer) -> str:
    lines = [f"{crash.signal or 'signal ?'} code {crash.code} at {crash.address:#010x}"]
    for name in ("pc", "lr"):
        if name in crash.registers:
            value = crash.registers[name]
            lines.append(f"  {name:>4} {value:#010x} {symbols.describe(crash, value)}")
    lines.append("registers:")
    row = []
    for name in REGISTERS:
        if name in crash.registers:
            row.append(f"{name}={crash.registers[name]:#010x}")
        if len(row) == 4:
            lines.append("  " + " ".join(row))
            row = []
    if row:
        lines.append("  " + " ".join(row))
    lines.append("code addresses on the stack (likely callers):")
    shown = 0
    for address, word in crash.stack:
        if not executable(crash, word):
            continue
        lines.append(f"  [{address:#010x}] {word:#010x} {symbols.describe(crash, word)}")
        shown += 1
        if shown == STACK_CANDIDATES:
            break
    if not shown:
        lines.append("  none")
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=pathlib.Path)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--hook", type=pathlib.Path,
                        help="librx3_core.so with its DWARF, e.g. build/librx3_core.so")
    source.add_argument("--autoexec", type=pathlib.Path,
                        help="the stick's autoexec.bin; needs --key")
    parser.add_argument("--key", type=pathlib.Path)
    args = parser.parse_args(argv)
    try:
        crashes = parse(args.report.read_text(encoding="utf-8", errors="replace"))
    except (OSError, ReportError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    with tempfile.TemporaryDirectory(prefix="rx3-crash-") as directory:
        hook = args.hook
        if args.autoexec:
            if not args.key:
                print("error: --autoexec needs --key", file=sys.stderr)
                return 2
            hook = pathlib.Path(directory) / "librx3_core.so"
            hook.write_bytes(hook_from_autoexec(args.autoexec, args.key))
        symbolizer = find_symbolizer()
        if hook and not symbolizer:
            print("warning: llvm-symbolizer not found; hook addresses stay offsets",
                  file=sys.stderr)
        symbols = Symbolizer(hook, symbolizer)
        for index, crash in enumerate(crashes, 1):
            print(f"=== crash {index} of {len(crashes)} ===")
            print(render(crash, symbols))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

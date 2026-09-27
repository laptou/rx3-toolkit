#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""A crash report must name the hook function it stopped in."""

from __future__ import annotations

import pathlib
import subprocess
import unittest

from tools.rx3_crash.cli import render
from tools.rx3_crash.report import parse
from tools.rx3_crash.symbolize import Symbolizer, find_symbolizer


REPOSITORY = pathlib.Path(__file__).resolve().parent.parent
HOOK = REPOSITORY / "build/librx3_core.so"
BASE = 0x40100000


def symbol_address(name: str) -> int:
    readelf = pathlib.Path(find_symbolizer() or "").with_name("llvm-readelf")
    output = subprocess.run(
        [str(readelf), "-s", "-W", str(HOOK)], capture_output=True, text=True, check=True
    ).stdout
    for line in output.splitlines():
        parts = line.split()
        if len(parts) >= 8 and parts[7] == name:
            return int(parts[1], 16)
    raise AssertionError(f"{name} is not in {HOOK}")


def report(pc: int, stack_word: int) -> str:
    registers = "\n".join(f"{name} 0x00000000" for name in (
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "fp", "ip"))
    return (
        "boot 7 id 1\n"
        "=== rx3 crash ===\n"
        "signal SIGSEGV\n"
        "code 0x00000001\n"
        "addr 0x00000004\n"
        "fault 0x00000004\n"
        f"{registers}\n"
        "sp 0xbe000000\n"
        f"lr 0x{stack_word:08x}\n"
        f"pc 0x{pc:08x}\n"
        "cpsr 0x60000010\n"
        "--- maps ---\n"
        "00008000-005c8000 r-xp 00000000 00:01 11 /root/pdj/rbp\n"
        f"{BASE:08x}-{BASE + 0x40000:08x} r-xp 00000000 00:01 12 /root/pdj/librx3_core.so\n"
        "be000000-be100000 rw-p 00000000 00:00 0 [stack]\n"
        "--- stack ---\n"
        "be000000 00000010\n"
        f"be000004 {stack_word:08x}\n"
        "be000008 00038ff4\n"
        "=== end ===\n"
    )


@unittest.skipUnless(HOOK.is_file() and find_symbolizer(), "needs make hook and llvm-symbolizer")
class CrashReportTests(unittest.TestCase):
    def test_hook_addresses_resolve_to_functions(self) -> None:
        handler = symbol_address("crash_handler")
        loader = symbol_address("sprite_loader")
        crashes = parse(report(BASE + handler + 8, BASE + loader + 4))
        self.assertEqual(len(crashes), 1)
        text = render(crashes[0], Symbolizer(HOOK, find_symbolizer()))
        self.assertIn("crash_handler", text)
        self.assertIn("sprite_loader", text)
        self.assertIn("0x00038ff4 rbp", text)


if __name__ == "__main__":
    unittest.main()

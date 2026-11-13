# SPDX-License-Identifier: MPL-2.0
"""Decompile rbp functions with headless Ghidra into local/ghidra/decompiled.

The first run imports and analyses the binary, which takes a while. Later runs
reuse that project and only decompile. Everything stays under /local/, which
git ignores: neither the binary nor what Ghidra makes of it is published.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import subprocess
import sys

REPOSITORY = pathlib.Path(__file__).resolve().parents[2]
SCRIPTS = pathlib.Path(__file__).resolve().parent
PROJECT_NAME = "rbp"
PROCESSOR = "ARM:LE:32:v7"
HOMEBREW = pathlib.Path("/opt/homebrew/opt/ghidra/libexec")


def find_headless(ghidra: pathlib.Path | None) -> pathlib.Path | None:
    """analyzeHeadless from --ghidra, GHIDRA_INSTALL_DIR, or homebrew."""
    roots: list[pathlib.Path] = []
    if ghidra:
        roots.append(ghidra)
    if os.environ.get("GHIDRA_INSTALL_DIR"):
        roots.append(pathlib.Path(os.environ["GHIDRA_INSTALL_DIR"]))
    roots.append(HOMEBREW)
    for root in roots:
        headless = root / "support/analyzeHeadless"
        if headless.is_file():
            return headless
    return None


def headless_command(
    headless: pathlib.Path,
    project: pathlib.Path,
    rbp: pathlib.Path,
    output: pathlib.Path,
    targets: list[str],
) -> list[str]:
    command = [str(headless), str(project), PROJECT_NAME]
    if (project / f"{PROJECT_NAME}.gpr").is_file():
        command += ["-process", rbp.name, "-noanalysis"]
    else:
        command += ["-import", str(rbp), "-processor", PROCESSOR]
    return command + [
        "-scriptPath", str(SCRIPTS),
        "-postScript", "ExportDecompiled.java", str(output), *targets,
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rbp", required=True, type=pathlib.Path,
                        help="the stock copy, RX3_RUNTIME/rbp-<sha1>.rbp")
    parser.add_argument("--ghidra", type=pathlib.Path, help="Ghidra install directory")
    parser.add_argument("--project", type=pathlib.Path,
                        default=REPOSITORY / "local/ghidra/project")
    parser.add_argument("--out", type=pathlib.Path,
                        default=REPOSITORY / "local/ghidra/decompiled")
    parser.add_argument("targets", nargs="+", help="0xADDRESS, a function name, or str:TEXT")
    args = parser.parse_args(argv)
    if not args.rbp.is_file():
        print(f"error: {args.rbp} does not exist", file=sys.stderr)
        return 2
    headless = find_headless(args.ghidra)
    if headless is None:
        print("error: Ghidra not found; pass --ghidra or set GHIDRA_INSTALL_DIR",
              file=sys.stderr)
        return 2
    args.project.mkdir(parents=True, exist_ok=True)
    args.out.mkdir(parents=True, exist_ok=True)
    command = headless_command(headless, args.project, args.rbp.resolve(),
                               args.out.resolve(), args.targets)
    return subprocess.run(command, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())

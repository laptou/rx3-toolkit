# SPDX-License-Identifier: MPL-2.0
"""Compile sprite packs and write an RX3_SPRITES directory."""

from __future__ import annotations

import argparse
import logging
import pathlib
import sys

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2]))

from tools.rx3_sprites.log import configure
from tools.rx3_sprites.proof import proven_format
from tools.rx3_sprites.provision import ProvisionError, provision
from tools.rx3_sprites.source import SourceError, compile_source, load_source


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, help_text in (
        ("preview", "compile in memory and log the summary"),
        ("write", "compile and write RX3_SPRITES"),
    ):
        command = commands.add_parser(name, help=help_text)
        command.add_argument("--source", required=True, type=pathlib.Path)
        if name == "write":
            command.add_argument("--dest", required=True, type=pathlib.Path)
        command.add_argument("-v", "--verbose", action="count", default=0)
        command.add_argument("-q", "--quiet", action="count", default=0)
        command.add_argument(
            "--log-level", choices=("debug", "info", "warning", "error")
        )
    try:
        args = parser.parse_args(argv)
    except SystemExit as exit_error:
        code = exit_error.code
        return code if isinstance(code, int) else 2
    configure(_level(args))
    try:
        if args.command == "preview":
            loaded = load_source(args.source)
            pack = compile_source(loaded, proven_format())
            mode = "fixed rate" if pack.mode == 1 else "beat clock"
            logging.getLogger("rx3.sprites").info(
                "%s: %sx%s, %s frames, %s, palette %s bytes",
                pack.name, pack.width, pack.height, pack.frame_count, mode, len(pack.palette),
            )
            return 0
        provision(args.source, args.dest)
    except (OSError, SourceError, ProvisionError) as error:
        logging.getLogger("rx3.sprites").error("%s", error)
        return 2
    return 0


def _level(args: argparse.Namespace) -> int:
    if args.log_level:
        return getattr(logging, args.log_level.upper())
    if args.quiet >= 2:
        return logging.CRITICAL
    if args.quiet == 1:
        return logging.ERROR
    if args.verbose >= 2:
        return logging.DEBUG
    if args.verbose == 1:
        return logging.INFO
    return logging.WARNING


if __name__ == "__main__":
    raise SystemExit(main())

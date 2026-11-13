# SPDX-License-Identifier: MPL-2.0
"""stderr log for the host sprite tools."""

from __future__ import annotations

import logging
import sys


def configure(level: int) -> None:
    logging.basicConfig(
        level=level,
        format="%(levelname)s %(name)s: %(message)s",
        stream=sys.stderr,
        force=True,
    )

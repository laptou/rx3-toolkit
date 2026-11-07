# SPDX-License-Identifier: MPL-2.0
"""Executable tests for the guards that decide whether to stop the player.

They are run here against a process tree written by the test, which is what
PROC_ROOT in the orchestrator exists for.
"""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUTOEXEC = ROOT / "mod/autoexec.sh"


def function(name: str) -> str:
    """Lift one function out of the orchestrator.

    The file runs work at the top level, so it cannot be sourced. Reading the
    function out of it keeps the test against the shipped text rather than a
    copy that can drift away from it.
    """
    source = AUTOEXEC.read_text()
    match = re.search(rf"(?m)^{name}\(\)\n\{{\n.*?^\}}\n", source, re.DOTALL)
    assert match, f"{name} is not defined in {AUTOEXEC.name}"
    return match.group(0)


PREAMBLE = "say() { :; }\n" + function("rbp_alive")


def run_shell(body: str, proc_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["sh", "-s"],
        input=f'PROC_ROOT="{proc_root}"\n' + PREAMBLE + body,
        text=True, capture_output=True, check=False,
    )


class ZombieTests(unittest.TestCase):
    def alive(self, pid: str, exe: bool, state: str) -> int:
        with tempfile.TemporaryDirectory() as directory:
            proc = Path(directory) / pid
            proc.mkdir()
            if exe:
                (proc / "exe").write_text("")
            (proc / "stat").write_text(f"{pid} (rbp) {state} 1 1 0\n")
            result = run_shell(
                f'rbp_alive "{pid}"\necho "verdict=$?"\n', Path(directory))
            self.assertEqual(result.returncode, 0, result.stderr)
            return int(re.search(r"verdict=(\d+)", result.stdout).group(1))

    def test_a_running_player_is_alive(self):
        self.assertEqual(self.alive("101", exe=True, state="S"), 0)

    def test_a_zombie_is_not_alive(self):
        """It keeps its directory and loses its mapping, and the mapping is the
        only thing that makes writing rbp dangerous. Testing for the directory
        alone waits ten seconds for a process that has already gone."""
        self.assertEqual(self.alive("102", exe=False, state="Z"), 1)

    def test_the_guard_would_actually_catch_a_regression(self):
        """A directory test passes the zombie the real one rejects."""
        with tempfile.TemporaryDirectory() as directory:
            proc = Path(directory) / "103"
            proc.mkdir()
            (proc / "stat").write_text("103 (rbp) Z 1 1 0\n")
            weakened = run_shell(
                f'[ -d "$PROC_ROOT/103" ]\necho "verdict=$?"\n', Path(directory))
            self.assertIn("verdict=0", weakened.stdout)
            self.assertEqual(self.alive("103", exe=False, state="Z"), 1)

class MediaDeferralTests(unittest.TestCase):
    """The mods run whatever else is mounted.

    A deferral here is not a safeguard, it is a lockout: the player mounts
    every readable partition, so a stick with an EFI partition beside its data
    partition presented two mounts and the mod deferred on every insertion
    forever. A second stick had the same effect. Neither can interrupt the
    player in a way the mods have to respect, and the drive the mods run from
    is the one the launcher passed, not a mount the guard picked.
    """

    def test_nothing_defers_on_what_else_is_mounted(self):
        source = AUTOEXEC.read_text()
        for gone in ("defer_for_unsafe_media", "media_topology_is_unsafe",
                     "mount_base_device", "mod deferred", "safe-load"):
            self.assertNotIn(gone, source)

    def test_the_run_still_checks_it_is_our_player(self):
        source = AUTOEXEC.read_text()
        for kept in ("rbp_executable_matches",
                     "expected one live rbp",
                     "effective root is not RAM-backed"):
            self.assertIn(kept, source)


if __name__ == "__main__":
    unittest.main()


class PlayerLoggingTests(unittest.TestCase):
    def test_continuous_output_is_only_on_usb_when_explicitly_requested(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            modules = root / "modules"
            modules.mkdir()
            for verbose in (False, True):
                if verbose:
                    (modules / "logging-verbose").mkdir()
                result = subprocess.run(["sh", "-s"], input=(
                    f'OUT="{root}/USB"\n' + function("configure_player_logs") +
                    f'configure_player_logs "{modules}"\n' +
                    'printf "%s\n%s\n" "$RBP_OUTPUT" "$RBP_RESTORE_OUTPUT"\n'
                ), text=True, capture_output=True, check=True)
                paths = result.stdout.splitlines()
                expected = str(root / "USB") if verbose else "/tmp"
                self.assertTrue(all(str(Path(path).parent) == expected for path in paths))

    def test_ram_logs_rotate_in_ram_without_writing_usb(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ram = root / "ram"
            ram.mkdir()
            output = ram / "stdout.txt"
            output.write_text("previous session")
            result = subprocess.run(["sh", "-s"], input=(
                f'OUT="{root}/USB"\nRBP_OUTPUT="{output}"\n'
                f'RBP_RESTORE_OUTPUT="{ram}/restore.txt"\n'
                'LOGGING=1\nPLAYER_LOGS_ROTATED=0\n' + function("rotate_player_logs") +
                'rotate_player_logs\n'
            ), text=True, capture_output=True, check=True)
            self.assertFalse((root / "USB").exists())
            self.assertEqual((ram / "stdout-previous.txt").read_text(), "previous session")

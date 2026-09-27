#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Session diagnostics on the drive. The orchestrator has already answered this
# module's only question - is it in the image? - before the first line could be
# written, so what is left here is to say what having said yes costs.

module_begin logging logging

# a crash inside rbp appends a report to crash-NNNN.txt, named for the boot that
# launched that rbp, and the kernel then writes a core file beside it. neither
# changes what rbp runs, so neither asks for a restart.
logging_prepare()
{
    export RX3_CRASH_PATH="$OUT/crash-${BOOT_TAG:-0000}.txt"
    if ulimit -c unlimited 2>/dev/null &&
       echo "$OUT/core-%e-%p" > /proc/sys/kernel/core_pattern 2>/dev/null; then
        say "Crash reports: RX3_RUNTIME/crash-${BOOT_TAG:-0000}.txt and core-<program>-<pid>"
    else
        say "WARNING: core files are unavailable; crash reports still go to crash-${BOOT_TAG:-0000}.txt"
    fi
    return 0
}

logging_report()
{
    say ""
    say "Session logging is on. Eject the drive from the RX3 rather than"
    say "pulling it out: rbp keeps this file open for as long as it plays."
}

register_prepare_hook logging_prepare
register_report_hook logging_report

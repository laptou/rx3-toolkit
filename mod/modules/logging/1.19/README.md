<!-- SPDX-License-Identifier: MPL-2.0 -->
# 📝 Session logging

Writes down what happened, onto the stick, so a bug report has something in it.

Off by default. Tick it when something went wrong and you want to know why — or when you are about to open an issue.

Each boot writes a new file. The player has no clock, so a file's date does not tell you whether it is new. Read `RX3_RUNTIME/latest.txt` first. It names `session-0007.txt` and a random id. The session file's first line is that same `boot 7 id ...`. A boot that did not write leaves `latest.txt` unchanged. The last line of a good run is:

```text
=== complete ===
```

| Line | What it means |
| --- | --- |
| `=== complete ===` | The run finished. This is what you want to see. |
| `OK: rbp active` | The player was restarted and came back. |
| `nothing to apply: ...` | Everything you picked was already running. Not an error. |
| `STOP: ...` | Something was not as expected, so **nothing was touched**. |
| `FAILED: ...` | Something went wrong partway, and the previous state was put back. |

Older boots stay on the stick as `session-0001.txt`, `session-0002.txt`, and so on. `boot.seq` is the count. The player's own output is `rbp-stdout-0007.txt` for the same boot.

> [!WARNING]
> **Eject the drive from the RX3. Do not just pull it out.**
> While this module is on, the player keeps that log file open for as long as it is playing. Yanking a FAT stick mid-write is how you lose a folder — and the folder you lose might be your music. This is the entire reason the module is off by default.

Leave it off for normal use and you never have to think about any of that.

---

On `STOP:` or `FAILED:`, delete `autoexec.bin` from the stick before using the player again, then see [Troubleshooting](../../../../docs/troubleshooting.md#the-session-log-says-stop-or-failed).

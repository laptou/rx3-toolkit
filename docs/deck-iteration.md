<!-- SPDX-License-Identifier: MPL-2.0 -->
# Iterating on the deck without wasting boots

A field guide for agents working on this tree. Every boot costs the user a walk
to the XDJ, a USB write and a power cycle. One wrong guess costs a whole round
trip; a wrong *diagnosis* costs several. These notes exist so the next agent
spends its flashes on new questions instead of relearning old ones.

Read this before touching a module that "does not show up on the deck".

## The loop, in order

1. Read the deck's own logs first. Do not ask the user to paste them.
2. Find the line that already exists and contradicts the theory.
3. Fix the cause, not the symptom.
4. Build, then **prove** the fix is inside the image (see
   [Verifying a build](#verifying-a-build)). A build that "finished" proves
   nothing.
5. Add the log line you will need next time, bounded.
6. Give the user the exact `cp` command, both files.
7. After the boot, read the logs again.

## Reading the deck's own logs

The prepared drive carries its own logging. Read it directly:

```
/Volumes/IBIDJ64-1/RX3_RUNTIME/
  mod.txt              module log, appended after each line
  mod-previous.txt     the boot before
  session.txt          autoexec's own log
  latest.txt           the newest rbp stdout
  rbp-stdout-NNNN.txt  per-run stdout, numbered by boot
  boot.seq             boot counter
  sprite-status.log    sprites module
```

```bash
grep -a "sidechain:\|swang:" /Volumes/IBIDJ64-1/RX3_RUNTIME/mod.txt | tail -40
grep -a "===" /Volumes/IBIDJ64-1/RX3_RUNTIME/session.txt | tail -5
```

`mod.txt` is opened and closed per line, so it is complete even if the deck is
powered off mid-session. `latest.txt` is a copy, not a link; the numbered
`rbp-stdout-NNNN.txt` is how you order boots.

If a read is refused, ask for a sandbox grant on `/Volumes/IBIDJ64-1` rather
than asking the user to retype logs. The logs are the cheapest evidence
available and the user should not have to be the transport.

## Building and installing

`nix develop` is required: the ARM core needs the pinned clang + lld. Plain host
clang fails on `-fuse-ld=lld`.

```bash
cd ~/projects/scratch/xdj-rx3-mods && mkdir -p build/re/<name>
nix develop -c .venv/bin/python -m app.runtime.cli build --firmware 1.19 \
  --key build/rx3-home/rx3-deck-bin --output build/re/<name> \
  --patch decoder-sleep --patch beatjump-32bars --patch keyshift \
  --patch stems --patch logging --patch swang --patch sidechain \
  --patch bandpass --patch sprites --patch now-playing
```

Gotchas that have each cost a cycle:

- **The output folder must already exist.** `mkdir -p` first, or the build
  exits with `Select an existing output folder or mounted USB drive`.
- **The key is not named `*.key`.** `pi-permission-system` denies those paths.
  It lives at `build/rx3-home/rx3-deck-bin`.
- **Both files must be installed**, `autoexec.bin` *and*
  `rx3-mod-manifest.json`. Copying only the image leaves a stale manifest and
  the deck can load the previous module set.

```bash
cp ~/projects/scratch/xdj-rx3-mods/build/re/<name>/autoexec.bin \
   ~/projects/scratch/xdj-rx3-mods/build/re/<name>/rx3-mod-manifest.json \
   /Volumes/IBIDJ64-1/
```

- **The XDJ has no RTC.** File times and log mtimes are meaningless and cannot
  order two runs. The build stamp is the only freshness proof:
  `manifest["build"]` plus the `RX3_BUILD` line at the top of `session.txt`.
  A drive still carrying `@@RX3_BUILD@@` was staged by hand.

## Verifying a build

`autoexec.bin` is AES-256-CBC encrypted per 512-byte sector. **Plaintext
`grep`/`strings` on it is meaningless.**

```bash
nix develop -c .venv/bin/python - <<'PY'
from app.firmware.firmware_image import read_autoexec
plain = read_autoexec("build/re/<name>/autoexec.bin", "build/rx3-home/rx3-deck-bin")
for s in (b"sidechain: labels relabelled = ", b"swang: ticks repaint after the waveform draw"):
    print(("HIT " if plain.find(s) != -1 else "MISS"), s.decode())
PY
```

Search for **log strings**, not symbol names. Symbol names survive in the ELF
string table, so `plain.find(b"stock_image")` hits `stock_image_info_guard` — an
unrelated hook guard — long after the function was deleted. A symbol-name hit
or miss is not evidence about code.

Also print the image size and SHA-256 and keep them in the report. They are how
a later session tells two builds apart.

## ARM hooking rules

The hook layer lives in `mod/modules/core/services/rx3_hooks.c`; the surface is
`struct rx3_framework` in `mod/modules/core/api/rx3_module_api.h`.

- **Guards are memory order, not instruction order.** ARM32 is little-endian.
  The words at `0x0024fcd8` are `ECBD8B04` (`vpop {d8,d9}`) and `E8BD8FF0`
  (`ldmia sp!,{r4-r11,pc}`), so the bytes are `04 8b bd ec | f0 8f bd e8`.
  A guard written `... e8 bd 8f f0` reads like the instruction and fails every
  time. This exact transposition silently disabled SWANG on the deck for a
  whole round trip.
  `install_return_hook` now takes two decoded **words**, so that mistake cannot
  be expressed. Do the same for any new guard API.
- **A stolen instruction must be replayable.** `install_raw_return_hook`
  rejects a first instruction that is PC-relative (`ldr rX,[pc,…]`) or a branch,
  and a tail that does not clear bit14 and set bit15 (`ldmia sp!,{…,pc}`).
  Those rejections are correct: patching there would corrupt the caller.
- **A return cannot be hooked with the stock installer.** The instruction being
  stolen *is* the return, and an 8-byte patch has nowhere safe to land. That is
  why `install_return_hook` exists: it replays the preceding instruction with
  `pc` redirected to `lr`, calls the module, then jumps to the real tail.
- **Return-tail hooks are the only post-frame turn.** See
  [Frames](#frames-and-overwrite-order).
- Limits: `RX3_HOOK_LIMIT 48` installed hooks; overlapping patches within 8
  bytes of another live hook are refused.

## The image table

`stock_table` and the global at `RX3_IMAGE_TABLE_POINTER 0x05a14f60`.
`RX3_NATIVE_IMAGE_COUNT 0x15cd` stock records, `EXTENDED_IMAGE_COUNT 0x16a6`
after the mod extends it, **44 bytes per record**, record `image` at
`table + image * 44`.

Record layout (verified by the relabel and key-tab work):

| offset | field |
|---|---|
| `+0x04` | `uint16` width |
| `+0x06` | `uint16` height |
| `+0x18` | format (1 = plain rgb565, 2 = colour key honoured) |
| `+0x19` | paletted flag |
| `+0x20` | `uint32` pixel offset, **relative to the table base** |
| `+0x24` | `uint32` palette offset |

Two facts that caused whole round trips:

- **The stock table does not exist when modules initialize.** The player builds
  it later. A module that measures a stock record in its install path reads a
  zero pointer and concludes "no such image". This is why
  `sidechain: captions relabelled = 0` with `refusal = 1` (no record).
- **The only readable moment is inside the table build.** Replacement is
  therefore split: the module hands over *what to paint*, the core paints it
  while it copies the stock record. `replace_masked(owner, image, mask_w,
  mask_h, coverage)` takes no size and no colours — the record supplies width,
  height, ground (`pixels[0]`) and ink (the pixel furthest from the ground),
  so dim/white and black/blue variants keep their own colouring and no variant
  order has to be decided.
- **After `tables_published`, the offsets are live.** Never free replacement
  pixels then; `release_native` only removes the registration once published.
  Freeing leaves the live table pointing at released memory.
- The light table is rebased from the dark one, so a replacement written for
  the dark table needs no separate light artwork.

## Frames and overwrite order

`ui_PLAYMODE_Set 0x0024fb80` paints one frame of the play waveform, single exit
at `0x0024fcdc`. In order:

1. per-column loops (this is where most overlay hooks land),
2. row mirror, rows 0..110 copied to 111..221,
3. cue merge, rows 0..19 and 201..220,
4. playhead needle, two columns of `0xffff`/`0xf800` at row 222,
5. epilogue `0x0024fcdc`, `vpop {d8,d9}` then `ldmia sp!,{r4-r11,pc}`.

Anything painted in step 1 is destroyed by steps 2-4. That is why SWANG ticks
flickered: the mirror overwrote them each frame, deterministically, and no
amount of repainting earlier helped. The fix is a return-tail hook at
`0x0024fcd8` that repaints after all four steps.

Consequence worth keeping: a "sometimes it shows" symptom in the waveform is
almost never timing. Here it is paint order — read the order before adding
retries.

## Meters and clocks

`watch_draw` is **not** a frame clock on the play screen. It fires while a
screen is being drawn, not on a fixed cadence, so a meter ticked from it
appears frozen. The reliable cadence is the play screen's own refresh, reached
from `sc_info()` in the beat fx display block, on the UI thread.

Rule: tick periodic UI work from something that provably runs every refresh,
and treat a second source as a fallback, not a substitute.

## Guards: no speculative safeguards

A guard that defers the whole mod on ambiguous evidence is not a safeguard, it
is a lockout. The media-topology guard (`media_topology_is_unsafe`,
`defer_for_unsafe_media`, `mount_base_device`) deferred the mod whenever the
partition layout looked odd or a second USB was mounted. It never once
prevented a real problem and it blocked normal use.

It was deleted, not softened (`0f1bd4c`). The drive comes from `$1`; it does
not need to be discovered by a guard.

What *is* legitimate in `autoexec.sh`, and should stay:

- exactly one live `rbp`,
- `rbp_executable_matches` (the binary is the expected one),
- effective root is RAM-backed,
- `/root/pdj` is not a separate mount,
- patch-word verification (`verify_words`),
- the `/tmp` lock directory.

The test that replaced the guard asserts those are still present and that the
deferral code is gone. Keep that shape: when you remove a guard, pin its
absence so it cannot creep back.

## Diagnostics that pay for themselves

- **Every failure line carries a number and a reason code.** "captions
  relabelled = 0" says nothing; "the caption refusal was = 1" (no record) says
  everything. The refusal codes are 1 no record, 2 not rgb565, 3 service
  refused, 4 no slot.
- **Bound the repeats.** A per-frame log line drowns the log and slows the
  deck. Counted one-shot notes (`sc_meter_note`, `SC_METER_NOTES 8u`) give
  first-occurrence evidence without the flood.
- **Log the geometry you actually used** (`masked image width = `,
  `masked image height = `). It turns the next iteration from a guess into
  arithmetic.
- Prefer a log line over a new hook when the question is "did this path run".
- **When the number you need is the player's, ask the player.** Which stock
  image id carries a caption is decided by rbp at runtime; no static table run
  exists in `rbp` for it (see the caption history below). `rx3_images.watch_ids(owner,
  first, last)` registers a range and logs the first draw of every id in it --
  one line each, bounded by the range -- so one boot names the set. That is
  cheaper and more certain than any amount of extraction, and it is how the
  SPIRAL caption, the effect tiles and the pad faces get named.
- **A module cannot read a stock record while it starts**: the player builds
  its image table after module init. Anything that needs a record's width,
  height or ground is painted core-side, in `replace_masked`, while the table
  is copied.
- **Watching a range names a set; it does not pick one of them.** Fourteen
  effects share the caption band `0x1439..0x1470`, four variants each, and the
  deck decides which group belongs to which effect. Ordering the band by the
  effect numbering is a guess that one known id can always be made to fit, so
  do not make it. `rx3_images.claim_drawn(owner, fn)` hands the module every
  drawn id and `paint_masked_now` paints the record after the tables publish:
  the same caption id drawn twice running while the effect is the shown one is
  the caption on the play screen, because the select grid draws every effect's
  caption in that band. The paint names itself in the log, so the second boot
  can hard-code what the first one learned.

## Tests, manifests and the toolchain

- The suite is **`unittest`**, not pytest, and it runs inside `nix develop`:
  ```bash
  make test          # nix develop -c .venv/bin/python -m unittest discover -s tests -p 'test_*.py'
  ```
  `unittest discover -s tests -t .` fails: `tests` is not a package.
- Some tests compile the ARM core for real. **Every header a `.c` includes must
  be listed in that module's `manifest.json` `build_files`**, and a header that
  moves between modules has to move in both manifests, the including `.c`, the
  host test's `#include`, and any test that pins the list. Missing this shows up
  as a confusing clang error inside a temporary staging directory.
- `make -B hook` is the arbiter for a single header. LSP diagnostics about
  undeclared `framework`, `size_t`, `mmap`, `PROT_READ`, or an incomplete
  `struct installed_hook` are standalone-parse noise and are not errors.
- `shasum` without `-a 256` prints SHA-1. Always pass `-a 256`.

## Spending flash cycles

- **Batch independent hypotheses into one boot.** Three log lines added once
  cost one cycle; added one at a time they cost three.
- **Do not flash a build you have not decrypted.** The most expensive failure
  mode is booting an image that does not contain the change at all.
- **When a fix "should work" and does not, suspect the guard, the patch site,
  or the timing of the read — not the logic.** Every miss in this session was
  one of those three: a transposed guard, a patch erased by the paint order, a
  table read before it existed.
- Firmware is **1.19** (`1.19 [3101:3100]`), stock rbp SHA-1
  `cf309238491e73cdbdc1f08a09f7a3177e079068`.

## Pitfalls, with the shape of each mistake

| Symptom on the deck | What it actually was |
|---|---|
| Caption still reads HELIX, log says `relabelled = 0` | Module measured a stock record before the player built the table |
| Ticks flicker, "sometimes visible" | Painted in step 1, erased by the mirror in step 2 |
| Hook "installed" in code, never ran on the deck | Guard bytes in instruction order, not memory order |
| Meter sits still | Ticked from `watch_draw`, which is not a frame clock |
| Mods refuse to activate when a second USB is inserted | Topology guard deferred everything on ambiguous evidence |
| Now-playing title empty | Datagram had no title; the tag cache does |
| `ValueError: Select an existing output folder` | Output directory must exist before `build` |
| `grep` on `autoexec.bin` shows nothing | Image is AES-encrypted per sector; decrypt first |
| Test fails inside a temp dir with a missing header | Header not listed in the module's `build_files` |

## Constants worth keeping

| Thing | Value |
|---|---|
| Wave paint tail (return hook) | `0x0024fcd8` → `0xecbd8b04`, `0xe8bd8ff0` |
| `ui_PLAYMODE_Set` | `0x0024fb80` |
| Half-beat setup | `0x0024b72c` |
| Wave buffer pointer | `0x02498cc8`, stride `0x6f8`, width `892` |
| Image table pointer | `0x05a14f60` |
| Stock image count | `0x15cd` (`RX3_NATIVE_IMAGE_COUNT`) |
| Image record stride | `44` bytes |
| HELIX caption ids | `0x1449`..`0x144c` |
| Select-grid tile ids | `0x15aa`..`0x15ac` |
| Key-tab images | `0x1600`..`0x1603` |
| Hook pool | `RX3_HOOK_LIMIT 48` |
| Replacement pool | `REPLACEMENT_LIMIT 12` |
| Wave window | `y=47`, width `892`, stride `222` rows |
| Sidechain meter | `892x44`, `y = 47 + 222 - 22 = 247`, tick `33 ms`, idle `500 ms` |

## History of the caption work, so it is not repeated

Three attempts, one real cause:

1. Hardcoded 160×40 caption and a colour-key assumption (`50cd6c4`) — wrong
   size, wrong colour model.
2. `stock_image` added so each record could be measured (`232b209`) — right
   instinct, impossible moment: the table does not exist at module start.
3. `replace_native` at install time with refusal codes (`59673c9`, `7faa76b`) —
   correct diagnosis of the refusal, still the wrong moment.

The working design (27156fe) moves the *paint*, not the measurement: the core
paints a coverage mask into the record while it copies the stock table, and the
record decides size, ground and ink. If you find yourself adding a retry loop
around a table read, you are re-entering attempt 3.

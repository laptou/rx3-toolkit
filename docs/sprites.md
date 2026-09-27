<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19. The overlay is optional and off unless you select the sprites module and the USB stick contains `RX3_SPRITES`.

Each deck gets one overlay window, centred on that deck's waveform strip (deck 0 top strip, deck 1 bottom). Anchor 2 rests the sprite on the strip's bottom edge. Index 0 is transparent. `RX3_RUNTIME/sprite-status.log` notes "sprite window open deck N" or "sprite window could not be created".

## What you put on the stick

```
RX3_SPRITES/
  catalog.txt
  your-name.rx3spr
```

`catalog.txt` is one pack filename per line, sorted, with a final newline. Hidden files are ignored. The player does not scan the directory itself.

The computer does not read Rekordbox analysis files. Beat timing comes from the player.

## Authoring

A source folder holds `manifest.json` plus either a PNG sequence or one sheet.

| Field | Values |
| --- | --- |
| schema | 1 |
| name | file stem, no slash, not hidden |
| layout | `sequence` or `sheet` |
| width, height | 1..64, same for every frame |
| frame_count | 1..16 |
| mode | `fps` or `beat` |
| fps_num, fps_den | positive for `fps`, otherwise 0 |
| frames_per_beat, beats_per_loop | their product equals `frame_count` for `beat` |
| anchor | `center` or `bottom-center` |

Alpha below 128 is transparent. Anything else is opaque. There is no soft edge. The whole animation shares one palette of at most 255 opaque colours. Extra colours are reduced to that palette with no dither. Packs store 8-bit indices. The player expands each frame to RGB565 when it loads the pack. Index 0 becomes magenta `0xF81F`, and the window colour key skips that colour. An opaque pixel of exactly that colour is moved one step to `0xF81E`.

Repeat a write from the command line. Diagnostics go to stderr. `-v` prints the summary, `-vv` also prints each PNG name, and `-q` keeps errors only.

```sh
python -m tools.rx3_sprites preview --source DIR -v
python -m tools.rx3_sprites write --source DIR --dest DIR
```

The Toolbox **Animated sprites** tab previews the mask and writes the same folder. It does not edit Rekordbox databases or audio files. It warns when the packs exceed the 2 MiB player ceiling. The player still refuses a pack that does not fit.

## Pack bytes

A `.rx3spr` file is little-endian. It starts with the eight bytes `RX3SPR1` and a trailing NUL, then version 1 and a 64-byte header. The header names the size, frame count, mode (`1` fixed rate, `2` beat), the timing fields, the anchor, the palette format, and the body length. Fourteen pad bytes after the packed header must be zero. A 32-bit ISO-HDLC checksum covers the header with that field zeroed, then the body. The file ends at the body. Extra bytes are rejected.

The body is the UTF-8 name, a 16-bit colour palette (index 0 is transparent), then the frames. Each pixel is one byte.

## On the player

Each successful track load picks one pack for that deck. The pick is a function of the track name, the deck, and a per-boot nonce. With two or more packs, the same deck does not draw the pack it just used. Decks do not share a choice.

Fixed-rate frames follow wall time, so a skipped refresh jumps ahead. Beat-mode frames follow the player's own beat clock: absolute beat phase, in beats from the grid origin. Seek, cue, and a tempo change move that clock, so they move the frame. The pitch fader is playback speed, not the clock.

The address of that clock is recorded only after a firmware 1.19 log shows it advancing one beat per audible beat, jumping on seek or cue, and staying independent of the pitch fader. Until that proof, beat mode logs `sprites: beat clock unavailable` once and draws nothing. Fixed-rate packs still play.

A bad pack or a memory refusal disables that deck only. Stock drawing and stems keep running.

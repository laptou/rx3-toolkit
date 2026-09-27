<!-- SPDX-License-Identifier: MPL-2.0 -->
# Animated sprites

Firmware 1.19. The overlay is optional and off unless you select the sprites module and the USB stick contains `RX3_SPRITES`.

## What you put on the stick

```
RX3_SPRITES/
  catalog.txt
  tracks.rx3idx
  format.proof
  anchors.proof
  your-name.rx3spr
```

`catalog.txt` is one pack filename per line, sorted, with a final newline. Hidden files are ignored. The player does not scan the directory itself.

`format.proof` and `anchors.proof` come from a hardware check. Until both exist and agree with a single layout, deck sprites do not draw. See [hardware validation](hardware-validation.md).

Beat-mode packs also need `tracks.rx3idx`. A track with no usable grid can still play a fixed-rate pack.

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

Alpha below 128 is transparent. Anything else is opaque. There is no soft edge. At most 255 opaque colours, or 15 when the proven format is 4 bits per pixel.

The Toolbox **Animated sprites** tab previews that mask, compiles the pack, scans a Rekordbox USB for beat grids, and writes the folder by rename. It does not edit Rekordbox databases or audio files. It warns when the packs exceed the 2 MiB player ceiling. The player still refuses a pack that does not fit.

## Pack bytes

A `.rx3spr` file is little-endian. It starts with the eight bytes `RX3SPR1` and a trailing NUL, then version 1 and a 64-byte header. The header names the size, frame count, mode (`1` fixed rate, `2` beat), the timing fields, the anchor, the palette format, and the body length. Fourteen pad bytes after the packed header must be zero. A 32-bit ISO-HDLC checksum covers the header with that field zeroed, then the body. The file ends at the body. Extra bytes are rejected.

The body is the UTF-8 name, a 16-bit colour palette (index 0 is transparent), then the frames. 4-bit frames pack two pixels per byte, high nibble first.

## Beat index

`tracks.rx3idx` starts with `RX3IDX1` and a trailing NUL, version 1, and a 32-byte header. Each entry is the same 44-character export name the stems sidecars use, then the first beat in 44.1 kHz samples and the ordered tempo segments. The same checksum covers the file. Two files that collapse to one name are an error. A missing or broken grid makes beat-mode packs ineligible for that track.

## On the player

Each successful track load picks one eligible pack for that deck. The pick is a function of the track name, the deck, and a per-boot nonce. With two or more packs, the same deck does not draw the pack it just used. Decks do not share a choice.

Fixed-rate frames follow wall time, so a skipped refresh jumps ahead. Beat frames follow the TimeStretch playhead through the exported grid, including a seek and a tempo change. Master Tempo uses the same playhead. If the grid is missing, only fixed-rate packs are eligible.

Drawing happens after the stock image, and only when the current image matches a proven waveform anchor. The sprite uses a private image id. Those ids are not rewritten after the first install. A bad pack, a failed mask check, an ambiguous layout, or a memory refusal disables that deck only.

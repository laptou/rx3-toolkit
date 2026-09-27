<!-- SPDX-License-Identifier: MPL-2.0 -->
# Hardware validation

These checks are run on an XDJ-RX3 with firmware 1.19. This repository does not contain the results. A missing proof file keeps deck sprites off. Do not invent a format byte or an anchor from a guess.

Nothing in this procedure writes internal flash. Boot without the stick, or power-cycle, to return to stock.

## Order

1. Boot stock, then power-cycle, and confirm the player is unchanged.
2. Run the existing stems module alone: a track with a sidecar, a track without one, vocal and instrumental pads, scratch, slip, and Master Tempo.
3. Select the sprite probe. Put `probe.spec` and `probe.bin` in `RX3_SPRITES`. `probe.spec` is the format proof text. `probe.bin` is the palette followed by an 8×8 index image whose four corners are index 0 and whose centre pixel is not. Confirm the corners show the picture underneath. If they do not, stop. Do not switch the overlay to an opaque rectangle.
4. With `RX3_SPRITE_TRACE=1`, change every waveform layout and load a track on each deck. The player appends at most 96 unique `(window layer, image id)` lines to `/tmp/rx3-sprite-trace.log`. Match those lines to screenshots. Write `anchors.proof` only for pairs that stay on one deck in both the stacked view and the side-by-side view. A pair that appears on both decks is ambiguous and must not be used.
5. Copy the confirmed format into `format.proof`. Deselect the probe. Draw one static synthetic sprite on one proven anchor.
6. Fixed-rate animation on each deck and each waveform view.
7. Repeated track loads, checking that each deck picks independently and does not immediately repeat when another pack exists.
8. Beat sync on a constant-tempo track and a variable-tempo track. Include cue, seek, loop, scratch, tempo bend, and Master Tempo.
9. Stems and sprites together for a long playback, including USB browsing.
10. A truncated pack, a missing `RX3_SPRITES` directory, an unsupported layout, and a forced player restart. Stock audio and the stock interface must continue. The rejected test must roll the player binary back.

Keep logs, checksums, timings, and the screenshots you need. Do not commit keys, firmware, player binaries, image tables, separated audio, or artwork you do not own.

## Proof files

`format.proof` uses Unix newlines and this order:

```
RX3FMT1
format N
palette 1
bpp 4
transparent 0
entry 2
```

`bpp` is 4 or 8. `anchors.proof` starts with `RX3ANC1` and then one line per anchor:

```
layer N id N deck 0
```

`deck` is 0 or 1. Duplicate layer and id pairs are rejected.

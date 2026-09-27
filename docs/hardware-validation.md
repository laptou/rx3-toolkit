<!-- SPDX-License-Identifier: MPL-2.0 -->
# Hardware validation

These checks are run on an XDJ-RX3 with firmware 1.19. The player shows one sprite centred over the whole waveform view (both deck strips), scaled at runtime with nearest-neighbour to about 0.9× the height of one waveform strip (200 px on screen). Anchor 1 centres it on the view; anchor 2 rests it on the bottom edge. Index 0 is transparent. `RX3_RUNTIME/sprite-status.log` notes "sprite window open deck N" or "sprite window could not be created".

Nothing in this procedure writes internal flash. Boot without the stick, or power-cycle, to return to stock.

## Order

1. Boot stock, then power-cycle, and confirm the player is unchanged.
2. Run the existing stems module alone: a track with a sidecar, a track without one, vocal and instrumental pads, scratch, slip, and Master Tempo.
3. Draw one static synthetic sprite on each deck.
4. Fixed-rate animation on each deck and each waveform view.
5. Repeated track loads, checking that each deck picks independently and does not immediately repeat when another pack exists.
6. Beat sync on a constant-tempo track and a variable-tempo track. Include cue, seek, loop, scratch, tempo bend, and Master Tempo. Beat mode draws nothing until the player beat-clock address is proven and stored. The expected log until then is `sprites: beat clock unavailable`.
7. Stems and sprites together for a long playback, including USB browsing.
8. A truncated pack, a missing `RX3_SPRITES` directory, an unsupported layout, and a forced player restart. Stock audio and the stock interface must continue. The rejected test must roll the player binary back.

Keep logs, checksums, timings, and the screenshots you need. Do not commit keys, firmware, player binaries, image tables, separated audio, or artwork you do not own.

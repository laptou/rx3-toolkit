<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sidechain

With this module on, the HELIX Beat FX no longer loops. It turns the two decks
into a sidechain pair: one deck ducks whenever the other one hits.

Select HELIX, pick CH1 or CH2 with the Beat FX channel selector, and press ON:

- **The Beat FX channel** is the trigger. It plays dry.
- **The other deck** ducks whenever the trigger goes over the threshold.
- **DEPTH** sets the threshold: 0 dB fully left, down to -36 dB fully right.
- **The BEAT arrows** step the ratio: 2:1, 3:1, 4:1 (the start), 6:1, 8:1,
  12:1, 20:1 and a limiter (∞:1). The cut never goes past 24 dB. The screen
  names the effect SIDECHAIN and shows the ratio where the beat would be.
- **TIME** is the release, 10 ms to 4000 ms. The attack is a sixteenth of it.

With MASTER, MIC or AUX selected, HELIX does nothing.

While HELIX is on, a meter strip sits across the seam between the two
waveforms and scrolls about 15 seconds of history, like a compressor's
display. The grey area rising from the bottom is the trigger's level (-48 dB
to 0 dB), the dotted line is its threshold, and the orange curve hanging from
the top is how far the ducked channel is being cut (up to 24 dB).

Not demonstrated on hardware yet: that the meter's window gets a free layer
above the waveforms, and the SIDECHAIN and ratio labels on the screen. CH1 runs before CH2, so a CH2 trigger keys
CH1 one audio block late.

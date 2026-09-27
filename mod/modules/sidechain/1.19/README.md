<!-- SPDX-License-Identifier: MPL-2.0 -->
# Sidechain

With this module on, the DUB ECHO sound color FX no longer echoes. It turns the
two channels into a sidechain pair: one channel ducks whenever the other one hits.

Press DUB ECHO, then use each channel's COLOR knob:

- **Left of centre** makes that channel the trigger. The further left, the lower
  the threshold, down to -36 dB. The trigger channel itself plays dry.
- **Right of centre** makes that channel the one that ducks. The further right,
  the higher the ratio, up to 20:1. The cut never goes past 24 dB.
- **Centre** is off for that channel.

The PARAMETER dial sets the release, measured in beats of the trigger deck:
an eighth of a beat fully left, half a beat at the centre, two beats fully
right. Attack is a sixteenth of the release. The beat length comes from the
trigger track's beat grid (120 BPM without one); the pitch fader is ignored.

While DUB ECHO is on, a meter strip sits across the seam between the two
waveforms and scrolls about 15 seconds of history, like a compressor's
display. The grey area rising from the bottom is the trigger's level (-48 dB
to 0 dB), the dotted line is its threshold, and the orange curve hanging from
the top is how far the ducked channel is being cut (up to 24 dB).

Not demonstrated on hardware yet: that the meter's window gets a free layer
above the waveforms, the knob fan-out of PARAMETER to both
channels, the exact block size, and that the knobs keep their value when DUB
ECHO is pressed without being turned. The ducking keys on the trigger's latest
audio block, so it can run one block late.

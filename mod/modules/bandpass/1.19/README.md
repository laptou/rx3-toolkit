<!-- SPDX-License-Identifier: MPL-2.0 -->
# Band-pass

With this module on, the DUB ECHO sound color FX no longer echoes. It becomes a
band-pass filter that you sweep with the COLOR knob.

- **Centre** lets everything through, so the sound is dry.
- **Left of centre** narrows the band and slides it down. Fully left, only the
  bass is left, about 70 Hz to 140 Hz.
- **Right of centre** narrows the band and slides it up. Fully right, only the
  treble is left, about 4 kHz to 8.5 kHz.
- **PARAMETER** sets the resonance at the edges of the band. The resonance
  grows as you turn COLOR away from the centre, and the level drops as the
  resonance rises, so the band never plays louder than the dry signal.

Not demonstrated on hardware yet: how PARAMETER reaches both channels, and how
the knob feels across its travel.

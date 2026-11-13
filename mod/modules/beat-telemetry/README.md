<!-- SPDX-License-Identifier: MPL-2.0 -->
# Beat telemetry

An optional RX3 module that sends live beat-grid timing for TouchDesigner and other visual tools. It is separate from **Now playing**, which continues to send track metadata on UDP `50123`.

The module broadcasts one fixed-size datagram per deck approximately every 50 ms to `169.254.255.255:50124`, the directed broadcast used by the rear USB-B link. No computer response is required. Allow inbound UDP `50124` in the computer firewall.

## Packet v1

Each datagram is exactly 48 bytes. All integers are little-endian. Invalid/gridless/stopped timing fields are zero. A receiver should reject wrong magic, version, length, reserved bits, invalid deck numbers, BPM outside 60–300 BPM, phase above 999, or beat-in-bar outside 1–4 when the grid-valid flag is set.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | ASCII `BCB1` |
| 4 | 1 | Version (`1`) |
| 5 | 1 | Deck (`1` or `2`) |
| 6 | 1 | Flags: bit 0 usable loaded grid, bit 1 playing, bit 2 grid-valid, bit 3 on-air; bits 4–5 reserved (tempo-master is not reported) |
| 7 | 1 | Reserved zero |
| 8 | 8 | Session identifier; changes on module start |
| 16 | 4 | Per-deck generation; changes at observed track/grid transitions and detected seeks/loops |
| 20 | 4 | Datagram sequence; increments for each transmitted packet |
| 24 | 8 | Track identity (`0`, unavailable from the current public service) |
| 32 | 4 | Effective BPM in milli-BPM; estimated from successive reader positions when possible, otherwise the player's BPM accessor |
| 36 | 4 | Fractional beat phase in thousandths (`0`–`999`) |
| 40 | 1 | Beat in bar (`1`–`4`) |
| 41 | 7 | Reserved zero |

The grid service reports a continuous beat count whose downbeats are multiples of four. `phase` is the fractional part; `beat-in-bar` is the whole-beat index modulo four, plus one. `generation` is a re-anchor boundary: receivers must discard interpolation from an earlier generation. The module also sends a grid-invalid packet when it observes a discontinuity, followed by a fresh snapshot when possible.

The stream callback only publishes the latest reader and sample position. A worker reads the beat grid and sends packets, so the callback performs no grid walk, socket operation, allocation or wait. `playing` means that the deck's sample position advanced since the preceding worker poll. `loaded` means a beat-grid lookup succeeded; it does not claim that every loaded, gridless track can be identified.

## TouchDesigner

TouchDesigner does not decode this custom binary packet with its OSC In CHOP. Receive and validate BCB1 with a small host-side bridge, translate the fields to OSC, then receive the OSC in TD. The bridge should use the sequence and generation fields, expire stale data, and re-anchor on generation changes. Continue to connect the RX3's audio output to an audio interface for waveform/spectrum visuals; this module sends timing only.

## Validation status and limits

This is an experimental feature. The packet sender, grid lookup and USB-B broadcast must be validated on the target RX3 and each firmware version before trusting it live. The current shared audio service is guarded, but that does not prove that its beat-grid interpretation or the transport works on hardware. The module does not report tempo-master state. The seek detector is based on reader-position discontinuities and may miss very small seeks; confirm cue, loop, pitch, pause, track-load and track-unload transitions during hardware testing. UDP is unauthenticated; the directly connected USB-B link is the trust boundary.

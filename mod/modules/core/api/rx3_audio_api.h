/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_AUDIO_API_H
#define RX3_AUDIO_API_H
#include "rx3_platform.h"
/* Typed audio stages. The master and channel stages have one owner at a
 * time; a second claim fails without replacing the first. The deck stream
 * carries a small chain instead, because it is observation as often as
 * processing: every client is called after the original, and registration is
 * once per owner for the process lifetime. Processing runs on the player's
 * audio threads and must not allocate, perform I/O or wait. The core counts
 * callers in flight: release() detaches the native hook, drains admitted
 * callbacks and only then returns, so it must not be called from a callback.
 * Buffers belong to the player and are valid only during the call.
 */
struct rx3_stereo { float left, right; };

/* A deck's playback stream after speed and master tempo (TimeStretch), the
 * signal the deck actually plays. `position` is the reader frame of the first
 * output frame at 44.1 kHz; `reader` identifies the deck's current PcmReader
 * and changes with each track load. */
typedef void (*rx3_deck_stream_fn)(unsigned int deck, const void *reader, int position,
                                   struct rx3_stereo *output, unsigned int frames);
/* The master bus before the microphone talkover attenuator. */
typedef void (*rx3_master_fn)(struct rx3_stereo *output, unsigned int frames);
/* A deck's channel output, after the fader and the channel's own processing,
 * just before the master sums it: where a per-deck gain belongs. `channel`
 * is the native MixerChannel, so the beat fx manager it carries is
 * reachable; the buffer may be rewritten in place. Channel 1 runs before
 * channel 2. */
typedef void (*rx3_channel_fn)(unsigned int deck, void *channel,
                               struct rx3_stereo *output, unsigned int frames);

struct rx3_audio_service {
    int (*claim_deck_stream)(const void *owner, rx3_deck_stream_fn);
    void (*release_deck_stream)(const void *owner);
    int (*claim_master)(const void *owner, rx3_master_fn);
    void (*release_master)(const void *owner);
    int (*claim_channel)(const void *owner, rx3_channel_fn);
    void (*release_channel)(const void *owner);
    /* The playhead's place on the deck's own beat grid, in beats with bar
       downbeats on multiples of 4, walked from the reader the service
       resolved the deck by. `position` is the same frame the deck stream
       reports. Any thread; zero when the deck has no grid or the walk raced
       a track load. */
    int (*beat_at)(unsigned int deck, long position, double *beat);
};
extern const struct rx3_audio_service rx3_audio;
#endif

/* SPDX-License-Identifier: MPL-2.0 */
/* Typed audio stages and the deck identity they are resolved against. The
 * callbacks run on the player's audio threads: this file adds no allocation,
 * I/O or waiting to those paths. Claims and releases run serially. */
#include "../api/rx3_audio_api.h"
#include "rx3_audio.h"
#include "rx3_hooks.h"
#include "rx3_log.h"

/* dsp::TimeStretch::getStreamAt is the deck's playback stream: it wraps
   PcmReader::getStreamAt and is the speed and master-tempo stage, so its
   output is what the deck plays. PcmReader::getStreamAt itself is shared with
   the BPM and waveform scan, which walks a track out of order; a sequential
   process cannot sit there. TimeStretch+4 is the reader, which names the deck. */
#define TIMESTRETCH_STREAM ((unsigned long)0x000c292c)
static const uint8_t stream_guard[8] = {0xf8,0x40,0x2d,0xe9,0x00,0x40,0xa0,0xe1};
/* The master bus, just before the microphone talkover attenuator. */
#define MIC_TALKOVER ((unsigned long)0x0009c750)
static const uint8_t master_guard[8] = {0x03,0xc0,0xd0,0xe5,0x04,0x40,0x2d,0xe5};
/* MixerChannel::updateFilter runs every block per deck and leaves the
   post-fader output at channel +0x20, which the master then sums. +0x210 is
   the deck the beat fx channel selector compares against (0 or 1; aux and
   mic are other values). */
#define MIXER_CHANNEL_UPDATE_FILTER ((unsigned long)0x0009e3fc)
static const uint8_t channel_guard[8] = {0xf0,0x41,0x2d,0xe9,0x00,0x40,0xa0,0xe1};

typedef unsigned long (*stream_fn)(void *, unsigned long, struct rx3_stereo *, unsigned long);
typedef void (*talkover_fn)(void *, void *, struct rx3_stereo *, int);
typedef void (*update_filter_fn)(void *, int);

struct stage {
    struct installed_hook hook;
    void *original;
    const void *owner;
    void *client;
    unsigned int active;
};
static struct stage deck_stage, master_stage, channel_stage;
static void *volatile deck_readers[2];

/* The deck stream is observation as often as processing: sprites and swang
   read the position while stems replace the audio, so it carries a small
   client chain rather than one owner. */
#define STREAM_CLIENT_LIMIT 4u
struct stream_client {
    const void *owner;
    unsigned int priority;
    void *callback;
};
static struct stream_client stream_clients[STREAM_CLIENT_LIMIT];
static unsigned int stream_count;

typedef void (*update_filter_fn)(void *, int);

static void drain(struct stage *stage)
{
    for (;;) {
        while (__atomic_load_n(&stage->active, __ATOMIC_SEQ_CST)) usleep(1000u);
        usleep(10000u);
        if (!__atomic_load_n(&stage->active, __ATOMIC_SEQ_CST)) return;
    }
}

static unsigned long stream_hooked(void *stretch, unsigned long position,
                                   struct rx3_stereo *output, unsigned long frames)
{
    __atomic_add_fetch(&deck_stage.active, 1u, __ATOMIC_SEQ_CST);
    unsigned long result = ((stream_fn)deck_stage.original)(stretch, position, output, frames);
    const void *reader = *(void **)((uint8_t *)stretch + 4u);
    int deck = rx3_audio_deck_for_reader(reader);
    if (output && frames && deck >= 0)
        for (unsigned int i = 0; i < stream_count; i++) {
            rx3_deck_stream_fn client =
                (rx3_deck_stream_fn)__atomic_load_n(&stream_clients[i].callback,
                                                    __ATOMIC_SEQ_CST);
            if (client)
                client((unsigned int)deck, reader, (int)position, output,
                       (unsigned int)frames);
        }
    __atomic_sub_fetch(&deck_stage.active, 1u, __ATOMIC_SEQ_CST);
    return result;
}

static void master_hooked(void *self, void *state, struct rx3_stereo *output, int frames)
{
    __atomic_add_fetch(&master_stage.active, 1u, __ATOMIC_SEQ_CST);
    rx3_master_fn client = __atomic_load_n(&master_stage.client, __ATOMIC_SEQ_CST);
    if (client && output && frames > 0) client(output, (unsigned int)frames);
    ((talkover_fn)master_stage.original)(self, state, output, frames);
    __atomic_sub_fetch(&master_stage.active, 1u, __ATOMIC_SEQ_CST);
}

static int claim(struct stage *stage, const void *owner, void *client, unsigned long address,
                 const uint8_t guard[8], void *replacement)
{
    if (!owner || !client || stage->owner) return 0;
    if (!stage->original) {
        stage->original = install_hook(&stage->hook, address, guard, replacement);
        if (!stage->original) return 0;
    }
    stage->owner = owner;
    __atomic_store_n(&stage->client, client, __ATOMIC_SEQ_CST);
    return 1;
}

/* Master and channel stages: one owner each. Detach first, then drain, then
   release the trampoline. A failed restore keeps the replacement reachable,
   passing audio through untouched. */
static void release(struct stage *stage, const void *owner)
{
    if (!owner || stage->owner != owner) return;
    __atomic_store_n(&stage->client, 0, __ATOMIC_SEQ_CST);
    int detached = stage->original && detach_hook(&stage->hook);
    drain(stage);
    stage->owner = 0;
    if (!stage->original) return;
    if (!detached) {
        log_line("audio: native stage retained after restore failure");
        return;
    }
    (void)release_hook(&stage->hook);
    stage->original = 0;
}

/* After the original: the block the master will sum is the channel's own
   post-fader output, already applied to the player's channel object. */
static void channel_hooked(void *channel, int frames)
{
    __atomic_add_fetch(&channel_stage.active, 1u, __ATOMIC_SEQ_CST);
    ((update_filter_fn)channel_stage.original)(channel, frames);
    rx3_channel_fn client = __atomic_load_n(&channel_stage.client, __ATOMIC_SEQ_CST);
    if (client && channel && frames > 0) {
        unsigned int deck = *(const unsigned int *)((const uint8_t *)channel + 0x210u);
        struct rx3_stereo *output =
            *(struct rx3_stereo **)((uint8_t *)channel + 0x20u);
        if (deck < 2u && output)
            client(deck, channel, output, (unsigned int)frames);
    }
    __atomic_sub_fetch(&channel_stage.active, 1u, __ATOMIC_SEQ_CST);
}

static int claim_deck_stream(const void *owner, rx3_deck_stream_fn client)
{
    if (!owner || !client || stream_count >= STREAM_CLIENT_LIMIT) return 0;
    for (unsigned int i = 0; i < stream_count; i++)
        if (stream_clients[i].owner == owner) return 0;
    if (!deck_stage.original) {
        deck_stage.original = install_hook(&deck_stage.hook, TIMESTRETCH_STREAM,
                                           stream_guard, (void *)stream_hooked);
        if (!deck_stage.original) return 0;
    }
    stream_clients[stream_count].owner = owner;
    stream_clients[stream_count].priority = 10u;
    stream_clients[stream_count].callback = (void *)client;
    __atomic_store_n(&stream_count, stream_count + 1u, __ATOMIC_SEQ_CST);
    return 1;
}
static void release_deck_stream(const void *owner)
{
    if (!owner) return;
    unsigned int live = 0;
    for (unsigned int i = 0; i < stream_count; i++) {
        if (stream_clients[i].owner == owner)
            __atomic_store_n(&stream_clients[i].callback, 0, __ATOMIC_SEQ_CST);
        if (stream_clients[i].callback) live++;
    }
    drain(&deck_stage);
    if (!live && deck_stage.original) {
        if (detach_hook(&deck_stage.hook)) {
            (void)release_hook(&deck_stage.hook);
            deck_stage.original = 0;
            stream_count = 0;
        } else {
            log_line("audio: deck stream retained after restore failure");
        }
    }
}
static int claim_master(const void *owner, rx3_master_fn client)
{
    return claim(&master_stage, owner, (void *)client, MIC_TALKOVER, master_guard,
                 (void *)master_hooked);
}
static void release_master(const void *owner) { release(&master_stage, owner); }
static int claim_channel(const void *owner, rx3_channel_fn client)
{
    return claim(&channel_stage, owner, (void *)client, MIXER_CHANNEL_UPDATE_FILTER,
                 channel_guard, (void *)channel_hooked);
}
static void release_channel(const void *owner) { release(&channel_stage, owner); }

/* PcmReader::load runs on the native loading thread. The old reader stops
   naming a deck before the load; the new one only after it. */
void rx3_audio_track_loading(unsigned int deck)
{
    if (deck < 2u) __atomic_store_n(&deck_readers[deck], 0, __ATOMIC_SEQ_CST);
}
void rx3_audio_track_loaded(unsigned int deck, void *reader)
{
    if (deck < 2u) __atomic_store_n(&deck_readers[deck], reader, __ATOMIC_SEQ_CST);
}
int rx3_audio_deck_for_reader(const void *reader)
{
    for (unsigned int deck = 0; deck < 2u; deck++)
        if (reader && __atomic_load_n(&deck_readers[deck], __ATOMIC_SEQ_CST) == reader)
            return (int)deck;
    return -1;
}

/* The reader each deck last played through walks to its own beat grid:
 *   PcmReader +0x04 ReaderImpl, +0x69c BeatPosition,
 *   +0x24 count, +0x28 grid offset (ms), +0x34/+0x38 the beat vector,
 *   each beat 8 bytes: +0 beat in bar (1-4), +2 bpm x100, +4 time (ms).
 * Plain loads only; a track load swaps in a new ReaderImpl, so the pointers
 * are checked again at the end. Any thread.
 */
static unsigned beat_umod(unsigned value, unsigned divisor)
{
    return value - (value / divisor) * divisor;
}

static int beat_at(unsigned int deck, long position, double *beat_out)
{
    if (deck >= 2u || position < 0 || !beat_out)
        return 0;
    const volatile uint8_t *reader =
        (const volatile uint8_t *)__atomic_load_n(&deck_readers[deck], __ATOMIC_SEQ_CST);
    if (!reader)
        return 0;
    const volatile uint8_t *impl = *(const volatile uint8_t *const *)(reader + 0x04u);
    const volatile uint8_t *grid;
    const volatile uint8_t *beats;
    const volatile uint8_t *end;
    uint32_t count, low = 0, high, index;
    int offset, target, time_at, width;
    uint16_t number;
    double beat;
    if (!impl)
        return 0;
    grid = *(const volatile uint8_t *const *)(impl + 0x69cu);
    if (!grid)
        return 0;
    count = *(const volatile uint32_t *)(grid + 0x24u);
    offset = *(const volatile int *)(grid + 0x28u);
    beats = *(const volatile uint8_t *const *)(grid + 0x34u);
    end = *(const volatile uint8_t *const *)(grid + 0x38u);
    if (!count || count > 0x100000u || !beats || end != beats + count * 8u)
        return 0;
    target = (int)((double)position / 44.1) - offset;
    /* first beat after the target, then step back one. */
    high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (*(const volatile int *)(beats + middle * 8u + 4u) <= target)
            low = middle + 1u;
        else
            high = middle;
    }
    index = low ? low - 1u : 0u;
    time_at = *(const volatile int *)(beats + index * 8u + 4u);
    if (index + 1u < count) {
        width = *(const volatile int *)(beats + (index + 1u) * 8u + 4u) - time_at;
    } else {
        uint16_t bpm = *(const volatile uint16_t *)(beats + index * 8u + 2u);
        if (!bpm)
            return 0;
        width = (int)(6000000u / bpm);
    }
    if (width <= 0)
        return 0;
    beat = (double)index + (double)(target - time_at) / (double)width;
    number = *(const volatile uint16_t *)(beats + index * 8u);
    if (number >= 1u && number <= 4u)
        beat += (double)beat_umod(number + 3u - beat_umod(index, 4u), 4u);
    if (beat < 0.0)
        beat = 0.0;
    if (*(const volatile uint8_t *const *)(reader + 0x04u) != impl ||
        *(const volatile uint8_t *const *)(impl + 0x69cu) != grid)
        return 0;
    *beat_out = beat;
    return 1;
}

unsigned int rx3_audio_count(void)
{
    return (deck_stage.owner != 0) + (master_stage.owner != 0) +
           (channel_stage.owner != 0);
}

const struct rx3_audio_service rx3_audio = {
    claim_deck_stream, release_deck_stream, claim_master, release_master,
    claim_channel, release_channel, beat_at
};

/* SPDX-License-Identifier: MPL-2.0
 * Beat-grid telemetry. The deck callback publishes only a reader and sample
 * position; the worker walks the grid and sends bounded UDP snapshots.
 */
#ifndef RX3_BEAT_TELEMETRY_FEATURE_H
#define RX3_BEAT_TELEMETRY_FEATURE_H
#include "rx3_beat_telemetry_decl.h"

struct rx3_bt_live_sample {
    volatile unsigned int revision;
    const void *reader;
    const void *expected_reader;
    long position;
    volatile unsigned int awaiting_load;
};
struct rx3_bt_deck_state {
    const void *reader;
    long position;
    double beat;
    uint32_t bpm_milli;
    uint32_t generation;
    uint64_t sample_time_us;
    int initialized;
    int grid_valid;
};

static const struct rx3_services *rx3_bt_framework;
static struct rx3_bt_live_sample rx3_bt_samples[2];
static struct rx3_bt_deck_state rx3_bt_decks[2];
static unsigned char rx3_bt_owner;
static volatile int rx3_bt_running;
static int rx3_bt_audio_claimed;
static int rx3_bt_socket = -1;
static pthread_t rx3_bt_thread;
static int rx3_bt_thread_started;
static uint64_t rx3_bt_session;
static uint32_t rx3_bt_sequence;

static void rx3_bt_stream(unsigned int deck, const void *reader, int position,
                          struct rx3_stereo *output, unsigned int frames)
{
    if (deck >= 2u || !reader || !output || !frames)
        return;
    struct rx3_bt_live_sample *sample = &rx3_bt_samples[deck];
    if (!__atomic_load_n(&sample->awaiting_load, __ATOMIC_ACQUIRE))
        __atomic_store_n(&sample->expected_reader, reader, __ATOMIC_RELEASE);
    __atomic_add_fetch(&sample->revision, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&sample->reader, reader, __ATOMIC_RELAXED);
    __atomic_store_n(&sample->position, (long)position, __ATOMIC_RELAXED);
    __atomic_add_fetch(&sample->revision, 1u, __ATOMIC_RELEASE);
}

static int rx3_bt_sample_read(unsigned int deck, const void **reader, long *position)
{
    struct rx3_bt_live_sample *sample = &rx3_bt_samples[deck];
    for (unsigned int tries = 0; tries < 8u; tries++) {
        unsigned int before = __atomic_load_n(&sample->revision, __ATOMIC_ACQUIRE);
        if (before & 1u)
            continue;
        const void *got_reader = __atomic_load_n(&sample->reader, __ATOMIC_RELAXED);
        long got_position = __atomic_load_n(&sample->position, __ATOMIC_RELAXED);
        unsigned int after = __atomic_load_n(&sample->revision, __ATOMIC_ACQUIRE);
        if (before == after) {
            *reader = got_reader;
            *position = got_position;
            return 1;
        }
    }
    return 0;
}

static uint64_t rx3_bt_now_us(void)
{
    struct rx3_bt_timeval now;
    if (gettimeofday((void *)&now, 0))
        return 0;
    return (uint64_t)now.seconds * 1000000u + (uint64_t)now.microseconds;
}

static void rx3_bt_send(unsigned int deck, uint8_t flags, uint32_t bpm_milli,
                        uint32_t phase_milli, uint8_t beat_in_bar,
                        uint32_t generation)
{
    struct rx3_bt_packet packet;
    uint8_t bytes[RX3_BT_PACKET_LEN];
    packet.deck = (uint8_t)(deck + 1u);
    packet.flags = flags;
    packet.session = rx3_bt_session;
    packet.generation = generation;
    packet.sequence = rx3_bt_sequence++;
    packet.track_identity = 0; /* Track IDs are not needed for beat locking. */
    packet.bpm_milli = bpm_milli;
    packet.phase_milli = phase_milli;
    packet.beat_in_bar = beat_in_bar;
    rx3_bt_encode(&packet, bytes);

    struct rx3_bt_address to;
    to.family = RX3_BT_SOCKET_AF_INET;
    to.port = (uint16_t)((RX3_BT_PORT >> 8u) | ((RX3_BT_PORT & 0xffu) << 8u));
    to.address = RX3_BT_BROADCAST;
    for (unsigned int i = 0; i < sizeof(to.zero); i++)
        to.zero[i] = 0;
    (void)sendto(rx3_bt_socket, bytes, sizeof(bytes), RX3_BT_DONTWAIT,
                 &to, sizeof(to));
}

static uint32_t rx3_bt_base_bpm_milli(unsigned int deck)
{
    int raw = ((rx3_bt_deck_int_fn)RX3_BT_PLAY_BPM)(deck);
    if (raw < 6000 || raw > 30000)
        return 0;
    return (uint32_t)raw * 10u; /* player accessor is BPM x100 */
}

static int rx3_bt_discontinuity(const struct rx3_bt_deck_state *previous,
                                const void *reader, long position, double beat,
                                uint64_t now_us)
{
    if (!previous->initialized)
        return 0;
    if (reader != previous->reader)
        return 1;
    if (!previous->grid_valid)
        return 0;
    uint64_t elapsed = now_us >= previous->sample_time_us
        ? now_us - previous->sample_time_us : 0;
    long frame_delta = position - previous->position;
    double beat_delta = beat - previous->beat;
    /* Allow up to 3x normal playback plus 100 ms scheduling slack. Larger
       jumps and reverse motion are seeks/loops, not time to interpolate
       through. The next datagram starts a new generation. */
    uint64_t max_frames = elapsed * 132300u / 1000000u + 4410u;
    return frame_delta < 0 || (uint64_t)frame_delta > max_frames ||
           beat_delta < -0.001;
}

static uint32_t rx3_bt_effective_bpm(const struct rx3_bt_deck_state *previous,
                                     long position, double beat, int discontinuity)
{
    if (!discontinuity && previous->grid_valid) {
        long frame_delta = position - previous->position;
        double beat_delta = beat - previous->beat;
        if (frame_delta > 0 && beat_delta > 0.0 && frame_delta <= 44100L * 2L) {
            double bpm = beat_delta * 60.0 * 44100.0 / (double)frame_delta;
            if (bpm >= 60.0 && bpm <= 300.0)
                return (uint32_t)(bpm * 1000.0);
        }
        if (previous->bpm_milli)
            return previous->bpm_milli;
    }
    return 0;
}

static void rx3_bt_tick_deck(unsigned int deck, uint64_t now_us)
{
    struct rx3_bt_deck_state *state = &rx3_bt_decks[deck];
    const void *reader = 0;
    long position = 0;
    int sample_ok = rx3_bt_sample_read(deck, &reader, &position);
    struct rx3_bt_live_sample *live = &rx3_bt_samples[deck];
    const void *expected_reader = __atomic_load_n(&live->expected_reader, __ATOMIC_ACQUIRE);
    int awaiting_load = __atomic_load_n(&live->awaiting_load, __ATOMIC_ACQUIRE) != 0;
    int reader_matches = sample_ok && reader && reader == expected_reader && !awaiting_load;
    double beat = 0.0;
    int grid_ok = reader_matches && position >= 0 &&
        rx3_bt_framework->audio->beat_at(deck, position, &beat) &&
        beat >= 0.0 && beat <= 1000000000.0 && beat == beat;
    int discontinuity = rx3_bt_discontinuity(state, reader, position, beat, now_us);
    int on_air = ((rx3_bt_on_air_fn)RX3_BT_ON_AIR)(deck, 0) != 0;
    uint32_t generation = state->generation;
    if (discontinuity)
        generation++;

    uint32_t bpm_milli = 0;
    if (grid_ok) {
        bpm_milli = rx3_bt_effective_bpm(state, position, beat, discontinuity);
        if (!bpm_milli)
            bpm_milli = rx3_bt_base_bpm_milli(deck);
    }
    int valid = grid_ok && bpm_milli >= 60000u && bpm_milli <= 300000u;
    if (state->initialized && state->grid_valid && !valid && !discontinuity) {
        generation++;
        discontinuity = 1;
    }
    if (state->initialized && !state->grid_valid && valid && !discontinuity) {
        generation++;
        discontinuity = 1;
    }

    /* A valid service lookup is the only proof of a usable loaded grid. */
    int loaded = reader_matches && grid_ok;
    int playing = valid && position != state->position && reader == state->reader;
    uint8_t flags = (uint8_t)((loaded ? RX3_BT_FLAG_LOADED : 0u) |
                              (playing ? RX3_BT_FLAG_PLAYING : 0u) |
                              (valid ? RX3_BT_FLAG_GRID_VALID : 0u) |
                              (on_air ? RX3_BT_FLAG_ON_AIR : 0u));
    uint32_t phase_milli = 0;
    uint8_t beat_in_bar = 0;
    if (valid) {
        rx3_bt_phase_and_bar(beat, &phase_milli, &beat_in_bar);
    } else {
        bpm_milli = 0;
    }

    if (discontinuity) {
        uint8_t invalid_flags = (uint8_t)((loaded ? RX3_BT_FLAG_LOADED : 0u) |
                                          (on_air ? RX3_BT_FLAG_ON_AIR : 0u));
        rx3_bt_send(deck, invalid_flags, 0, 0, 0, generation);
    }
    rx3_bt_send(deck, flags, bpm_milli, phase_milli, beat_in_bar, generation);

    state->reader = sample_ok ? reader : 0;
    state->position = position;
    state->beat = beat;
    state->bpm_milli = bpm_milli;
    state->generation = generation;
    state->sample_time_us = now_us;
    state->initialized = 1;
    state->grid_valid = valid;
}

static void rx3_bt_track_will_load(unsigned int deck, void *reader,
                                    const void *track_info)
{
    (void)reader;
    (void)track_info;
    if (deck >= 2u)
        return;
    struct rx3_bt_live_sample *sample = &rx3_bt_samples[deck];
    __atomic_store_n(&sample->awaiting_load, 1u, __ATOMIC_RELEASE);
    __atomic_store_n(&sample->expected_reader, 0, __ATOMIC_RELEASE);
}

static void rx3_bt_track_did_load(unsigned int deck, void *reader,
                                  const void *track_info)
{
    (void)track_info;
    if (deck >= 2u)
        return;
    struct rx3_bt_live_sample *sample = &rx3_bt_samples[deck];
    __atomic_store_n(&sample->expected_reader, reader, __ATOMIC_RELEASE);
    __atomic_store_n(&sample->awaiting_load, 0u, __ATOMIC_RELEASE);
}

static void *rx3_bt_worker(void *unused)
{
    (void)unused;
    while (__atomic_load_n(&rx3_bt_running, __ATOMIC_ACQUIRE)) {
        uint64_t now_us = rx3_bt_now_us();
        for (unsigned int deck = 0; deck < 2u; deck++)
            rx3_bt_tick_deck(deck, now_us);
        (void)usleep(RX3_BT_TICK_US);
    }
    return 0;
}

static int rx3_bt_feature_configured(void)
{
    const char *setting = getenv("RX3_BEAT_TELEMETRY");
    return setting && setting[0] == '1';
}

static void rx3_bt_feature_remove(void)
{
    __atomic_store_n(&rx3_bt_running, 0, __ATOMIC_RELEASE);
    if (rx3_bt_thread_started) {
        (void)pthread_join(rx3_bt_thread, 0);
        rx3_bt_thread_started = 0;
    }
    if (rx3_bt_audio_claimed) {
        rx3_bt_framework->audio->release_deck_stream(&rx3_bt_owner);
        rx3_bt_audio_claimed = 0;
    }
    if (rx3_bt_socket >= 0) {
        (void)close(rx3_bt_socket);
        rx3_bt_socket = -1;
    }
}

static int rx3_bt_feature_install(void)
{
    if (!rx3_bt_framework || !rx3_bt_framework->audio ||
        !rx3_bt_framework->audio->claim_deck_stream ||
        !rx3_bt_framework->audio->release_deck_stream ||
        !rx3_bt_framework->audio->beat_at) {
        if (rx3_bt_framework)
            rx3_bt_framework->log_line("beat telemetry refused: beat-grid service is unavailable");
        return 0;
    }
    if (memcmp((const void *)RX3_BT_PLAY_BPM, rx3_bt_play_bpm_guard, 8u) ||
        memcmp((const void *)RX3_BT_ON_AIR, rx3_bt_on_air_guard, 8u)) {
        rx3_bt_framework->log_line("beat telemetry refused: a player accessor prologue differs");
        return 0;
    }
    rx3_bt_socket = socket(RX3_BT_SOCKET_AF_INET, RX3_BT_SOCKET_DGRAM, 0);
    int broadcast = 1;
    if (rx3_bt_socket < 0 ||
        setsockopt(rx3_bt_socket, RX3_BT_SOCKET_SOL_SOCKET,
                   RX3_BT_SOCKET_SO_BROADCAST, &broadcast, sizeof(broadcast))) {
        rx3_bt_framework->log_line("beat telemetry refused: no USB-link broadcast socket");
        rx3_bt_feature_remove();
        return 0;
    }
    struct rx3_bt_timeval start;
    if (gettimeofday((void *)&start, 0)) {
        rx3_bt_framework->log_line("beat telemetry refused: no session clock");
        rx3_bt_feature_remove();
        return 0;
    }
    rx3_bt_session = ((uint64_t)(uint32_t)start.seconds << 32u) |
                     (uint32_t)start.microseconds;
    if (!rx3_bt_session)
        rx3_bt_session = 1u;

    if (!rx3_bt_framework->audio->claim_deck_stream(&rx3_bt_owner, rx3_bt_stream)) {
        rx3_bt_framework->log_line("beat telemetry refused: deck stream is unavailable");
        rx3_bt_feature_remove();
        return 0;
    }
    rx3_bt_audio_claimed = 1;
    __atomic_store_n(&rx3_bt_running, 1, __ATOMIC_RELEASE);
    if (pthread_create(&rx3_bt_thread, 0, rx3_bt_worker, 0)) {
        rx3_bt_framework->log_line("beat telemetry refused: worker did not start");
        rx3_bt_feature_remove();
        return 0;
    }
    rx3_bt_thread_started = 1;
    rx3_bt_framework->log_line("beat telemetry: beat-grid snapshots on UDP 50124 over USB-B");
    return 1;
}
#endif

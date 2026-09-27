/* SPDX-License-Identifier: MPL-2.0
 * the deck tap, firmware 1.19: each deck's playback block as it leaves the
 * resampler, its playhead, and where that playhead sits on the beat grid.
 *
 * dsp::TimeStretchScratch::operate is the live playback producer: hardware
 * tracing showed every call to the TimeStretch wrapper coming from inside it,
 * scratching or not, because it also carries varispeed. it reads overlapping
 * source windows through that wrapper and resamples them, so its output -- not
 * its input -- is the deck's block stream, and that is where a sequential dsp
 * belongs. dsp::TimeStretchFGPR::operate replaces it while Master Tempo is
 * on; the two are mutually exclusive. both derive from TimeStretch, so +4 is
 * the deck's PcmReader, and position is the playhead in 44.1 khz track
 * samples.
 */

#ifndef RX3_DECK_TAP_H
#define RX3_DECK_TAP_H

#define TIMESTRETCH_OPERATE ((unsigned long)0x000a22a0)
#define TIMESTRETCH_FGPR_OPERATE ((unsigned long)0x000a73bc)

typedef long (*timestretch_operate_fn)(void *, long, Float2 *, unsigned int);

static const uint8_t timestretch_operate_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x08, 0x8b, 0x2d, 0xed
};
static const uint8_t timestretch_fgpr_guard[8] = {
    0xf0, 0x4f, 0x2d, 0xe9, 0x02, 0x8b, 0x2d, 0xed
};

static struct installed_hook timestretch_operate_hook;
static struct installed_hook timestretch_fgpr_hook;
static timestretch_operate_fn original_timestretch_operate;
static timestretch_operate_fn original_timestretch_fgpr;

static void deck_tap_block(void *stretch, long position, Float2 *output,
                           unsigned int frames)
{
    unsigned int index;
    int deck;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].stretch_block)
            runtime_features[index].stretch_block(stretch, output, frames);
    deck = deck_index_for_reader(
        *(void *const *)((const uint8_t *)stretch + 4u));
    if (deck >= 0)
        rx3_notify_playhead((unsigned int)deck, position);
}

static long hooked_timestretch_operate(void *stretch, long position,
                                       Float2 *output, unsigned int frames)
{
    long result = original_timestretch_operate(stretch, position, output,
                                               frames);
    deck_tap_block(stretch, position, output, frames);
    return result;
}

static long hooked_timestretch_fgpr(void *stretch, long position,
                                    Float2 *output, unsigned int frames)
{
    long result = original_timestretch_fgpr(stretch, position, output, frames);
    deck_tap_block(stretch, position, output, frames);
    return result;
}

/* installed once, before the features, and only when one listens. */
static void install_deck_tap(void)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            (runtime_features[index].stretch_block ||
             runtime_features[index].playhead))
            break;
    if (index == RUNTIME_FEATURE_COUNT || original_timestretch_operate)
        return;
    original_timestretch_operate = (timestretch_operate_fn)install_hook(
        &timestretch_operate_hook, TIMESTRETCH_OPERATE,
        timestretch_operate_guard, (void *)hooked_timestretch_operate);
    original_timestretch_fgpr = (timestretch_operate_fn)install_hook(
        &timestretch_fgpr_hook, TIMESTRETCH_FGPR_OPERATE,
        timestretch_fgpr_guard, (void *)hooked_timestretch_fgpr);
    if (!original_timestretch_operate || !original_timestretch_fgpr) {
        log_line("warning: deck tap guard rejected");
        uninstall_hook(&timestretch_fgpr_hook);
        uninstall_hook(&timestretch_operate_hook);
        original_timestretch_fgpr = 0;
        original_timestretch_operate = 0;
    }
}

static int rx3_deck_tap_ready(void)
{
    return original_timestretch_operate && original_timestretch_fgpr;
}

static void remove_deck_tap(void)
{
    uninstall_hook(&timestretch_fgpr_hook);
    uninstall_hook(&timestretch_operate_hook);
    original_timestretch_fgpr = 0;
    original_timestretch_operate = 0;
}

static unsigned deck_tap_umod(unsigned value, unsigned divisor)
{
    return value - (value / divisor) * divisor;
}

/* the playhead in beats on the deck's beat grid, bar downbeats on multiples
 * of 4. the grid is PcmReader +4 (ReaderImpl) +0x69c:
 *   BeatPosition +0x24 count, +0x28 grid offset (ms), +0x34/+0x38 beat vector
 *   each beat is 8 bytes: +0 beat in bar (1-4), +2 bpm x100, +4 time (ms)
 * plain loads only; quantize helpers lock and write scratch fields. a track
 * load swaps in a new ReaderImpl, so the pointers are checked again at the
 * end. any thread. */
static int rx3_deck_beat(unsigned deck, long position, double *beat_out)
{
    const uint8_t *reader;
    const uint8_t *impl;
    const uint8_t *grid;
    const uint8_t *beats;
    const uint8_t *end;
    uint32_t count;
    uint32_t low = 0;
    uint32_t high;
    uint32_t index;
    int32_t offset;
    int32_t target;
    int32_t time_at;
    int32_t width;
    uint16_t number;
    double beat;
    if (deck >= 2u || position < 0)
        return -1;
    reader = (const uint8_t *)deck_readers[deck];
    if (!reader)
        return -1;
    impl = *(const uint8_t *const volatile *)(reader + 4u);
    if (!impl)
        return -1;
    grid = *(const uint8_t *const volatile *)(impl + 0x69cu);
    if (!grid)
        return -1;
    count = *(const volatile uint32_t *)(grid + 0x24u);
    offset = *(const volatile int32_t *)(grid + 0x28u);
    beats = *(const uint8_t *const volatile *)(grid + 0x34u);
    end = *(const uint8_t *const volatile *)(grid + 0x38u);
    if (!count || count > 0x100000u || !beats || end != beats + count * 8u)
        return -1;
    target = (int32_t)((double)position / 44.1) - offset;
    /* first beat after the target, then step back one. */
    high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (*(const int32_t *)(beats + middle * 8u + 4u) <= target)
            low = middle + 1u;
        else
            high = middle;
    }
    index = low ? low - 1u : 0u;
    time_at = *(const int32_t *)(beats + index * 8u + 4u);
    if (index + 1u < count) {
        width = *(const int32_t *)(beats + (index + 1u) * 8u + 4u) - time_at;
    } else {
        uint16_t bpm = *(const uint16_t *)(beats + index * 8u + 2u);
        if (!bpm)
            return -1;
        width = (int32_t)(6000000u / bpm);
    }
    if (width <= 0)
        return -1;
    beat = (double)index + (double)(target - time_at) / (double)width;
    number = *(const uint16_t *)(beats + index * 8u);
    if (number >= 1u && number <= 4u)
        beat += (double)deck_tap_umod(number + 3u - deck_tap_umod(index, 4u),
                                      4u);
    if (beat < 0.0)
        beat = 0.0;
    if (*(const uint8_t *const volatile *)(reader + 4u) != impl ||
        *(const uint8_t *const volatile *)(impl + 0x69cu) != grid)
        return -1;
    *beat_out = beat;
    return 0;
}

#endif /* RX3_DECK_TAP_H */

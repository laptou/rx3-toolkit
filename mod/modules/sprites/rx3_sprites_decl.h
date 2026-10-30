/* SPDX-License-Identifier: MPL-2.0
 * where the deck's beat grid lives, firmware 1.19.
 *
 * the position the audio service reports is the reader frame of the block's
 * first output frame at 44.1 khz. the deck's PcmReader walks to the grid:
 *   PcmReader +0x04 ReaderImpl
 *   ReaderImpl +0x69c BeatPosition
 *   BeatPosition +0x24 count, +0x28 grid offset (ms), +0x34/+0x38 beat vector
 * each beat is 8 bytes: +0 beat in bar (1-4), +2 bpm x100, +4 time (ms).
 *
 * plain loads only. a track load swaps in a new ReaderImpl, so the pointers
 * are checked again at the end. any thread.
 */

#ifndef RX3_SPRITES_DECL_H
#define RX3_SPRITES_DECL_H

#define SPRITE_BEAT_READER_IMPL 0x04u
#define SPRITE_BEAT_GRID 0x69cu
#define SPRITE_BEAT_COUNT 0x24u
#define SPRITE_BEAT_OFFSET 0x28u
#define SPRITE_BEAT_VECTOR 0x34u
#define SPRITE_BEAT_VECTOR_END 0x38u
#define SPRITE_BEAT_STRIDE 8u

/* the reader each deck last played through, kept by the audio callback. the
   audio service hands the reader over with every block; this is only the
   cached copy the beat walk starts from. */
static const void *volatile sprite_readers[2];

static unsigned sprite_umod(unsigned value, unsigned divisor)
{
    return value - (value / divisor) * divisor;
}

/* the playhead in beats on the deck's grid, bar downbeats on multiples of
   4. -1 when the deck has no reader, no grid, or the walk raced a load. */
static int sprite_deck_beat(unsigned deck, long position, double *beat_out)
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
    int offset;
    int target;
    int time_at;
    int width;
    uint16_t number;
    double beat;
    if (deck >= 2u || position < 0)
        return -1;
    reader = (const uint8_t *)sprite_readers[deck];
    if (!reader)
        return -1;
    impl = *(const uint8_t *const volatile *)(reader + SPRITE_BEAT_READER_IMPL);
    if (!impl)
        return -1;
    grid = *(const uint8_t *const volatile *)(impl + SPRITE_BEAT_GRID);
    if (!grid)
        return -1;
    count = *(const volatile uint32_t *)(grid + SPRITE_BEAT_COUNT);
    offset = *(const volatile int *)(grid + SPRITE_BEAT_OFFSET);
    beats = *(const uint8_t *const volatile *)(grid + SPRITE_BEAT_VECTOR);
    end = *(const uint8_t *const volatile *)(grid + SPRITE_BEAT_VECTOR_END);
    if (!count || count > 0x100000u || !beats ||
        end != beats + count * SPRITE_BEAT_STRIDE)
        return -1;
    target = (int)((double)position / 44.1) - offset;
    /* first beat after the target, then step back one. */
    high = count;
    while (low < high) {
        uint32_t middle = low + (high - low) / 2u;
        if (*(const int *)(beats + middle * SPRITE_BEAT_STRIDE + 4u) <= target)
            low = middle + 1u;
        else
            high = middle;
    }
    index = low ? low - 1u : 0u;
    time_at = *(const int *)(beats + index * SPRITE_BEAT_STRIDE + 4u);
    if (index + 1u < count) {
        width = *(const int *)(beats + (index + 1u) * SPRITE_BEAT_STRIDE + 4u) -
                time_at;
    } else {
        uint16_t bpm = *(const uint16_t *)(beats + index * SPRITE_BEAT_STRIDE + 2u);
        if (!bpm)
            return -1;
        width = (int)(6000000u / bpm);
    }
    if (width <= 0)
        return -1;
    beat = (double)index + (double)(target - time_at) / (double)width;
    number = *(const uint16_t *)(beats + index * SPRITE_BEAT_STRIDE);
    if (number >= 1u && number <= 4u)
        beat += (double)sprite_umod(number + 3u - sprite_umod(index, 4u), 4u);
    if (beat < 0.0)
        beat = 0.0;
    if (*(const uint8_t *const volatile *)(reader + SPRITE_BEAT_READER_IMPL) != impl ||
        *(const uint8_t *const volatile *)(impl + SPRITE_BEAT_GRID) != grid)
        return -1;
    *beat_out = beat;
    return 0;
}

#endif /* RX3_SPRITES_DECL_H */

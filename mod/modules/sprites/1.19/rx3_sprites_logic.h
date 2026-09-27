/* SPDX-License-Identifier: MPL-2.0
 * pure sprite decisions shared by the player and the host test.
 * no allocation and no libc, so the arm hook can include it.
 */

#ifndef RX3_SPRITES_LOGIC_H
#define RX3_SPRITES_LOGIC_H

#ifndef RX3_HOOK_TYPES
#include <stddef.h>
#include <stdint.h>
#else
typedef int int32_t;
#endif

#define RX3_SPRITE_SAMPLE_RATE 44100u
#define RX3_SPRITE_NAME_LIMIT 44u

struct rx3_tempo_segment {
    int32_t start_samples;
    uint32_t bpm_x1000;
};

static uint32_t rx3_sprite_checksum(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffu;
    size_t index;
    for (index = 0; index < length; index++) {
        int bit;
        crc ^= data[index];
        for (bit = 0; bit < 8; bit++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static unsigned rx3_umod(unsigned value, unsigned divisor)
{
    unsigned quotient;
    if (!divisor)
        return 0u;
    /* keep the remainder as arithmetic. a combined divmod helper is not exported. */
    quotient = value / divisor;
    __asm__ volatile("" : "+r"(quotient));
    return value - quotient * divisor;
}

static uint32_t rx3_sprite_mix(uint32_t value)
{
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    return value;
}

/* basename, drop the extension, then the same 44-character export cut. */
static int rx3_sprite_stem(const char *path, char *out, unsigned capacity)
{
    const char *base;
    const char *dot;
    const char *cursor;
    char raw[180];
    unsigned length = 0;
    unsigned start;
    unsigned end;
    unsigned copy;
    if (!path || !out || capacity < 6u)
        return -1;
    base = path;
    dot = 0;
    for (cursor = path; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            base = cursor + 1;
            dot = 0;
        } else if (*cursor == '.')
            dot = cursor;
    }
    for (cursor = base; *cursor && !(dot && cursor == dot); cursor++) {
        if ((unsigned char)*cursor >= 32u && length < sizeof(raw))
            raw[length++] = *cursor;
    }
    start = 0;
    end = length;
    while (start < end && (raw[start] == ' ' || raw[start] == '.'))
        start++;
    while (end > start && (raw[end - 1u] == ' ' || raw[end - 1u] == '.'))
        end--;
    if (start == end) {
        out[0] = 't';
        out[1] = 'r';
        out[2] = 'a';
        out[3] = 'c';
        out[4] = 'k';
        out[5] = 0;
        return 0;
    }
    copy = end - start;
    if (copy > RX3_SPRITE_NAME_LIMIT)
        copy = RX3_SPRITE_NAME_LIMIT;
    if (copy + 1u > capacity)
        return -1;
    for (length = 0; length < copy; length++)
        out[length] = raw[start + length];
    out[copy] = 0;
    return 0;
}

static unsigned rx3_sprite_fps_frame(uint32_t elapsed_ms, unsigned fps_num,
                                    unsigned fps_den, unsigned frame_count)
{
    if (!fps_num || !fps_den || !frame_count)
        return 0;
    return (unsigned)(((uint64_t)elapsed_ms * fps_num / fps_den / 1000u) %
                      frame_count);
}

static int rx3_sprite_beat_frame(int32_t position,
                                const struct rx3_tempo_segment *segments,
                                unsigned count, unsigned frame_count,
                                unsigned beats_per_loop, unsigned *frame_out)
{
    unsigned index;
    uint64_t beats_x1000;
    if (!segments || !count || !frame_count || !beats_per_loop || !frame_out)
        return -1;
    if (position < segments[0].start_samples) {
        *frame_out = 0;
        return 0;
    }
    index = 0;
    for (unsigned cursor = 0; cursor < count; cursor++) {
        if (segments[cursor].start_samples <= position)
            index = cursor;
        else
            break;
    }
    if (index + 1u < count) {
        int32_t span = segments[index + 1u].start_samples -
                       segments[index].start_samples;
        int32_t into = position - segments[index].start_samples;
        if (span <= 0)
            return -1;
        if (into > span)
            into = span;
        beats_x1000 = (uint64_t)index * 1000u +
                      (uint64_t)into * 1000u / (uint64_t)span;
    } else {
        int32_t delta = position - segments[index].start_samples;
        if (delta < 0)
            delta = 0;
        beats_x1000 = (uint64_t)index * 1000u +
                      (uint64_t)delta * segments[index].bpm_x1000 /
                      (RX3_SPRITE_SAMPLE_RATE * 60u);
    }
    *frame_out = (unsigned)((beats_x1000 * frame_count / beats_per_loop / 1000u) %
                            frame_count);
    return 0;
}

static unsigned rx3_sprite_pick(const char *identity, unsigned deck,
                               uint32_t nonce, const char *const *names,
                               unsigned count, const char *previous)
{
    uint32_t mixed;
    unsigned index;
    unsigned length;
    if (!names || !count || !identity)
        return 0;
    length = 0;
    while (identity[length])
        length++;
    mixed = rx3_sprite_mix(rx3_sprite_checksum((const uint8_t *)identity, length) ^
                           (deck * 0x9e3779b9u) ^ nonce);
    index = rx3_umod(mixed, count);
    if (count > 1u && previous) {
        const char *chosen = names[index];
        unsigned same = 1u;
        unsigned cursor = 0;
        while (chosen[cursor] || previous[cursor]) {
            if (chosen[cursor] != previous[cursor]) {
                same = 0u;
                break;
            }
            if (!chosen[cursor])
                break;
            cursor++;
        }
        if (same)
            index = rx3_umod(index + 1u, count);
    }
    return index;
}

#endif /* RX3_SPRITES_LOGIC_H */

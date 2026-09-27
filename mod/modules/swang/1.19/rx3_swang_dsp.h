/* SPDX-License-Identifier: MPL-2.0
 * swing by time warp, shared by the player and the host test. no libc and
 * no libm.
 *
 * the grid is cut into pairs of notes. within a pair the first note is
 * stretched and the second squeezed, so the off-beat that sat at half the
 * pair lands later, at the swing point (0.5 straight, 2/3 triplet, 0.75
 * hard). every pair still starts on the grid.
 *
 * that warp is a delay that only grows and shrinks: zero at the pair's start,
 * largest at the swing point, back to zero at the end. so the output never
 * needs audio that has not arrived. a delay that moves would bend pitch, so
 * the audio is read by two grains at normal speed instead, each holding the
 * delay it started with, half a grain apart and crossfaded with triangles
 * that sum to one. a delay of zero is the input exactly.
 */

#ifndef RX3_SWANG_DSP_H
#define RX3_SWANG_DSP_H

#ifndef RX3_HOOK_TYPES
#include <stdint.h>
#endif

/* the largest delay is a quarter of a pair, and the longest pair (swung
   quarter notes) is half a beat: 8269 frames at 40 bpm. the ring holds that
   plus a grain. */
#define SW_RING 16384u
#define SW_RING_MASK (SW_RING - 1u)
/* 30 ms grains: short enough that a drum hit is not heard twice, long
   enough that bass keeps its pitch. even, so the two triangles sum to one
   exactly. */
#define SW_GRAIN 1324u
#define SW_HALF_GRAIN (SW_GRAIN / 2u)
#define SW_DELAY_LIMIT ((float)(SW_RING - SW_GRAIN - 4u))
#define SW_STRAIGHT 0.5f
#define SW_HARDEST 0.75f

struct sw_stereo {
    float left;
    float right;
};

struct sw_state {
    struct sw_stereo ring[SW_RING];
    uint32_t written;
    uint32_t age[2];
    float delay[2];
};

/* the off-beat's place in the pair, 0.5 straight to 0.75, from depth 0..1. */
static inline float sw_swing_for(float depth)
{
    if (depth < 0.0f)
        depth = 0.0f;
    if (depth > 1.0f)
        depth = 1.0f;
    return SW_STRAIGHT + (SW_HARDEST - SW_STRAIGHT) * depth;
}

/* the delay at place u (0..1) in a pair, as a fraction of the pair: output
   time u plays source time u - delay. */
static inline float sw_delay_at(float u, float swing)
{
    float source;
    if (u <= 0.0f || u >= 1.0f || swing <= SW_STRAIGHT)
        return 0.0f;
    if (u < swing)
        source = u * (0.5f / swing);
    else
        source = 0.5f + (u - swing) * (0.5f / (1.0f - swing));
    return u - source;
}

static inline void sw_reset(struct sw_state *state)
{
    uint32_t index;
    for (index = 0; index < SW_RING; index++) {
        state->ring[index].left = 0.0f;
        state->ring[index].right = 0.0f;
    }
    state->written = 0u;
    state->age[0] = 0u;
    state->age[1] = SW_HALF_GRAIN;
    state->delay[0] = 0.0f;
    state->delay[1] = 0.0f;
}

static inline int sw_idle(const struct sw_state *state)
{
    return state->delay[0] == 0.0f && state->delay[1] == 0.0f;
}

static inline struct sw_stereo sw_read(const struct sw_state *state,
                                       float delay)
{
    uint32_t whole = (uint32_t)delay;
    float part = delay - (float)whole;
    const struct sw_stereo *near =
        &state->ring[(state->written - 1u - whole) & SW_RING_MASK];
    const struct sw_stereo *far =
        &state->ring[(state->written - 2u - whole) & SW_RING_MASK];
    struct sw_stereo out;
    out.left = near->left + (far->left - near->left) * part;
    out.right = near->right + (far->right - near->right) * part;
    return out;
}

/* one block in place. pair_at (0..1) is where the block starts within its
   pair, pair_step the pairs per frame, pair_frames the frames in one pair. */
static inline void sw_process(struct sw_state *state, struct sw_stereo *buf,
                              int frames, float pair_at, float pair_step,
                              float swing, float pair_frames)
{
    int index;
    unsigned head;
    float lead = pair_step * (float)SW_HALF_GRAIN;
    for (index = 0; index < frames; index++) {
        float target;
        float centre = pair_at + lead;
        struct sw_stereo out = { 0.0f, 0.0f };
        /* a new grain is heard most at its middle, half a grain on, so it
           takes the delay the warp has there. */
        if (centre >= 1.0f)
            centre -= 1.0f;
        target = sw_delay_at(centre, swing) * pair_frames;
        if (target > SW_DELAY_LIMIT)
            target = SW_DELAY_LIMIT;
        state->ring[state->written & SW_RING_MASK] = buf[index];
        state->written++;
        for (head = 0; head < 2u; head++) {
            float weight;
            struct sw_stereo grain;
            if (state->age[head] >= SW_GRAIN) {
                state->age[head] = 0u;
                state->delay[head] = target;
            }
            weight = (float)state->age[head] / (float)SW_HALF_GRAIN;
            if (weight > 1.0f)
                weight = 2.0f - weight;
            grain = sw_read(state, state->delay[head]);
            out.left += grain.left * weight;
            out.right += grain.right * weight;
            state->age[head]++;
        }
        buf[index] = out;
        pair_at += pair_step;
        if (pair_at >= 1.0f)
            pair_at -= 1.0f;
    }
}

#endif /* RX3_SWANG_DSP_H */

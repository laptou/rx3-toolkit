/* SPDX-License-Identifier: MPL-2.0
 * swing by moving slices, shared by the player and the host test. no libc
 * and no libm.
 *
 * the grid is cut into pairs of notes. swing moves the off-beat, which sat
 * at half the pair, later to the swing point (0.5 straight, 2/3 triplet,
 * 0.75 hard). every pair still starts on the grid.
 *
 * nothing is sped up or slowed down. the output reads the input through a
 * delay that only changes at splices, and between splices it is the input
 * exactly. each note is cut into its sixteenths, and every sixteenth's hit
 * is pinned where the swing moves it: the first note's sixteenths spread
 * out, the second note's close up.
 *   - a pin is one splice a few ms before the hit, and from there the hit
 *     plays untouched.
 *   - a first-note sixteenth is longer once swung. after its attack, its
 *     tail is stretched to fill the gap: every few tens of ms the reader
 *     crossfades back a little, so that bit of the tail is heard twice.
 *   - a second-note sixteenth is shorter once swung. its tail is cut by the
 *     next pin.
 * every splice lands in phase with the audio already playing, and none runs
 * through an attack.
 */

#ifndef RX3_SWANG_DSP_H
#define RX3_SWANG_DSP_H

#ifndef RX3_HOOK_TYPES
#include <stdint.h>
#endif

/* the largest delay is a quarter of a pair, and the longest pair (swung
   quarter notes) is two beats: 33075 frames at 40 bpm. the ring holds that
   plus the phase search and its window. */
#define SW_RING 65536u
#define SW_RING_MASK (SW_RING - 1u)
#define SW_STRAIGHT 0.5f
#define SW_HARDEST 0.75f
/* the crossfade of every splice. */
#define SW_FADE 512u
/* the start of the first note that is never stretched (20 ms), and how long
   before a hit the splice that places it is done (3 ms). both shrink on
   short pairs. */
#define SW_ATTACK 882u
#define SW_PREROLL 128u
/* the stretch splices once it is this far behind: the slice heard twice. */
#define SW_JUMP 512u
/* phase search either side of a stretch splice's target: half a period of
   50 hz. a splice that places a hit may only land up to 2 ms late. */
#define SW_LAG_WIDE 441u
#define SW_LAG_NEAR 96u
/* the onset test's windows, in frames: after the hit, the gap before it,
   and the stretch before that it is compared with. */
#define SW_ONSET_AFTER 128u
#define SW_ONSET_GAP 128u
#define SW_ONSET_BEFORE 512u
/* a held delay the swing's glide has moved this far from is set again. */
#define SW_SLACK 441u
#define SW_MATCH_WINDOW 1024u
#define SW_MATCH_STRIDE 4u
#define SW_MATCH_MAX (2u * SW_LAG_WIDE / SW_MATCH_STRIDE + 2u)
#define SW_DELAY_LIMIT (SW_RING - SW_MATCH_WINDOW - SW_LAG_WIDE - 4u)
#define SW_HANN_TABLE 1024u

struct sw_stereo {
    float left;
    float right;
};

struct sw_state {
    struct sw_stereo ring[SW_RING];
    uint32_t written;
    uint32_t delay;  /* the reader heard, or fading in */
    uint32_t from;   /* the reader fading out */
    uint32_t faded;  /* frames into the crossfade, SW_FADE when none */
    int region;
    int placed;      /* this region's delay has been set since entering it */
};

static float sw_hann_table[SW_HANN_TABLE + 1u];
static int sw_hann_ready;

/* the off-beat's place in the pair, 0.5 straight to 0.75, from depth 0..1. */
static inline float sw_swing_for(float depth)
{
    if (depth < 0.0f)
        depth = 0.0f;
    if (depth > 1.0f)
        depth = 1.0f;
    return SW_STRAIGHT + (SW_HARDEST - SW_STRAIGHT) * depth;
}

struct sw_plan {
    int region;      /* 2k: slice k's pin, 2k+1: slice k's stretch */
    uint32_t target; /* the delay wanted, in frames */
    uint32_t most;   /* the stretch's limit: it never reaches back into the
                        attack it follows */
    uint32_t floor;  /* a pin's limit: the reader leaving it must not reach
                        the next slice's hit before its fade is done */
    int ahead;       /* a pin's hit, in frames after the fade (<0: passed) */
    int straight;    /* no swing: delay 0 exactly, the input untouched */
};

/* one pair's slices: each note cut into `slices` sixteenths. */
struct sw_slices {
    float pair_frames;
    float swing;
    float source; /* one slice of the input */
    int per_note;
};

static uint32_t sw_frames(float value)
{
    return value > 0.0f ? (uint32_t)(value + 0.5f) : 0u;
}

/* where slice j (0..2n, 2n the next downbeat) starts once swung. */
static float sw_slice_out(const struct sw_slices *cut, int j)
{
    float n = (float)cut->per_note;
    if (j <= cut->per_note)
        return (float)j * cut->swing * cut->pair_frames / n;
    return cut->swing * cut->pair_frames +
           (float)(j - cut->per_note) * (1.0f - cut->swing) *
               cut->pair_frames / n;
}

static float sw_slice_delay(const struct sw_slices *cut, int j)
{
    if (j >= 2 * cut->per_note)
        return 0.0f;
    return sw_slice_out(cut, j) - (float)j * cut->source;
}

static struct sw_plan sw_pin(const struct sw_slices *cut, int j, float t)
{
    struct sw_plan plan = { 0, 0u, 0u, 0u, 0, 0 };
    int count = 2 * cut->per_note;
    int home = j >= count ? j - count : j;
    plan.region = 2 * home;
    plan.target = sw_frames(sw_slice_delay(cut, j));
    plan.ahead = (int)(sw_slice_out(cut, j) - t);
    if (home >= cut->per_note)
        plan.floor = sw_frames(sw_slice_delay(cut, home + 1));
    return plan;
}

/* what frame t (0..pair_frames) of a pair wants, once a fade started now is
   done. a fading reader keeps its smaller delay and so reaches the next hit
   first: a stretch ends a jump early, while the reader it leaves is still
   a preroll short of the hit. */
static struct sw_plan sw_plan_at(float t, float swing, float pair_frames,
                                 int slices)
{
    struct sw_plan plan = { 0, 0u, 0u, 0u, 0, 1 };
    struct sw_slices cut;
    float attack = (float)SW_ATTACK;
    float preroll = (float)SW_PREROLL;
    float start;
    float end;
    int k;
    if (!(pair_frames > 0.0f) || slices < 1)
        return plan;
    if (swing > SW_HARDEST)
        swing = SW_HARDEST;
    if ((swing - SW_STRAIGHT) * pair_frames / (float)slices < 1.0f)
        return plan;
    cut.pair_frames = pair_frames;
    cut.swing = swing;
    cut.source = 0.5f * pair_frames / (float)slices;
    cut.per_note = slices;
    if (attack > cut.source / 4.0f)
        attack = cut.source / 4.0f;
    if (preroll > cut.source / 16.0f)
        preroll = cut.source / 16.0f;
    for (k = 0; k < 2 * slices - 1 && t >= sw_slice_out(&cut, k + 1); k++)
        ;
    start = sw_slice_out(&cut, k);
    end = sw_slice_out(&cut, k + 1);
    if (t >= end - preroll)
        return sw_pin(&cut, k + 1, t);
    if (k < slices) {
        /* a first-note slice: its attack, then its tail stretched. */
        float stretch_from = start + attack;
        float stretch_end = end - preroll - (float)SW_JUMP;
        float here = sw_slice_delay(&cut, k);
        float next = sw_slice_delay(&cut, k + 1);
        if (t < stretch_from)
            return sw_pin(&cut, k, t);
        if (t >= stretch_end)
            return sw_pin(&cut, k + 1, t);
        plan.straight = 0;
        plan.region = 2 * k + 1;
        plan.target = sw_frames(here + (next - here) * (t - stretch_from) /
                                           (stretch_end - stretch_from));
        plan.most = sw_frames(here + (t - stretch_from) - (float)SW_FADE);
        return plan;
    }
    /* a second-note slice plays whole until the next pin cuts its tail. */
    return sw_pin(&cut, k, t);
}

/* cos over [-pi, pi] by series. the window table is the only place it is
   needed, which keeps the hook off libm. */
static float sw_cos(float x)
{
    float square = x * x;
    float term = 1.0f;
    float sum = 1.0f;
    unsigned int n;
    for (n = 1u; n <= 8u; n++) {
        term *= -square / (float)((2u * n - 1u) * (2u * n));
        sum += term;
    }
    return sum;
}

static void sw_build_hann(void)
{
    const float two_pi = 6.28318530718f;
    unsigned int i;
    for (i = 0u; i <= SW_HANN_TABLE; i++) {
        float fraction = (float)i / (float)SW_HANN_TABLE;
        float angle = two_pi * fraction;
        if (angle > 3.14159265359f)
            angle -= two_pi;
        sw_hann_table[i] = 0.5f - 0.5f * sw_cos(angle);
    }
    sw_hann_ready = 1;
}

/* hann at 0 is silent and at 1/2 unity; its first half is the fade in, and
   one minus it the fade out, so an in-phase splice keeps its level. */
static float sw_hann(float fraction)
{
    float scaled = fraction * (float)SW_HANN_TABLE;
    unsigned int index = (unsigned int)scaled;
    float blend;
    if (index >= SW_HANN_TABLE)
        index = SW_HANN_TABLE - 1u;
    blend = scaled - (float)index;
    return sw_hann_table[index] +
           (sw_hann_table[index + 1u] - sw_hann_table[index]) * blend;
}

static struct sw_stereo sw_at(const struct sw_state *state, uint32_t delay)
{
    return state->ring[(state->written - 1u - delay) & SW_RING_MASK];
}

static float sw_mono(const struct sw_state *state, uint32_t delay)
{
    struct sw_stereo sample = sw_at(state, delay);
    return sample.left + sample.right;
}

/* whether a hit starts at delay `at`: the treble, taken as the change from
   one frame to the next so a hat is seen over a bass line, jumps 4x (6 db)
   over the 12 ms before it. a hit still ahead of the write head counts. */
static int sw_onset(const struct sw_state *state, int at)
{
    float after = 0.0f;
    float before = 0.0f;
    uint32_t i;
    if (at < (int)SW_ONSET_AFTER)
        return 1;
    for (i = 0u; i < SW_ONSET_AFTER; i++) {
        uint32_t d = (uint32_t)at - SW_ONSET_AFTER + i;
        float edge = sw_mono(state, d) - sw_mono(state, d + 1u);
        after += edge * edge;
    }
    for (i = 0u; i < SW_ONSET_BEFORE; i++) {
        uint32_t d = (uint32_t)at + SW_ONSET_GAP + i;
        float edge = sw_mono(state, d) - sw_mono(state, d + 1u);
        before += edge * edge;
    }
    return after > 1e-4f &&
           after * (float)SW_ONSET_BEFORE >
               4.0f * before * (float)SW_ONSET_AFTER;
}

/* how well the reader at candidate carries on from the one at playing,
   judged on the window both have just read. normalised by the candidate's
   energy, so a loud slice does not win by level alone. */
static float sw_score(const struct sw_state *state, uint32_t playing,
                      uint32_t candidate, uint32_t stride)
{
    float cross = 0.0f;
    float energy = 0.0f;
    uint32_t i;
    for (i = 0u; i < SW_MATCH_WINDOW; i += stride) {
        float a = sw_mono(state, playing + i);
        float b = sw_mono(state, candidate + i);
        cross += a * b;
        energy += b * b;
    }
    if (energy < 1e-9f)
        return 0.0f;
    return (cross < 0.0f ? -cross * cross : cross * cross) / energy;
}

/* the delay in [low, high] whose reader is most in phase with the one at
   playing. a whole period away is the same phase but a worse time, so among
   delays that score close to the best, the one nearest nominal wins. with
   nothing to match (silence, or no positive score) it is nominal. */
static uint32_t sw_match(const struct sw_state *state, uint32_t playing,
                         uint32_t low, uint32_t high, uint32_t nominal)
{
    float scores[SW_MATCH_MAX];
    float best = 0.0f;
    uint32_t count = 0u;
    uint32_t chosen;
    uint32_t distance = 0xffffffffu;
    uint32_t index;
    uint32_t delay;
    float fine_best;

    if (high > SW_DELAY_LIMIT)
        high = SW_DELAY_LIMIT;
    if (low > high)
        low = high;
    if (nominal < low)
        nominal = low;
    if (nominal > high)
        nominal = high;
    if (low == high)
        return low;
    for (delay = low; delay <= high && count < SW_MATCH_MAX;
         delay += SW_MATCH_STRIDE) {
        scores[count] = sw_score(state, playing, delay, SW_MATCH_STRIDE);
        if (scores[count] > best)
            best = scores[count];
        count++;
    }
    if (best <= 0.0f)
        return nominal;
    chosen = nominal;
    for (index = 0u; index < count; index++) {
        uint32_t at = low + index * SW_MATCH_STRIDE;
        uint32_t away = at > nominal ? at - nominal : nominal - at;
        if (scores[index] >= 0.9f * best && away < distance) {
            distance = away;
            chosen = at;
        }
    }
    /* the near candidate sits on the slope of its peak: climb to the top. */
    fine_best = sw_score(state, playing, chosen, 2u);
    for (;;) {
        float down = chosen > low ? sw_score(state, playing, chosen - 1u, 2u)
                                  : -1.0e30f;
        float up = chosen < high ? sw_score(state, playing, chosen + 1u, 2u)
                                 : -1.0e30f;
        if (down <= fine_best && up <= fine_best)
            break;
        if (down > up) {
            chosen--;
            fine_best = down;
        } else {
            chosen++;
            fine_best = up;
        }
    }
    return chosen;
}

static void sw_splice(struct sw_state *state, uint32_t to)
{
    if (to == state->delay)
        return;
    state->from = state->delay;
    state->delay = to;
    state->faded = 0u;
}

/* between splices: start one if the plan's delay is out of reach. */
static void sw_steer(struct sw_state *state, struct sw_plan plan)
{
    uint32_t now = state->delay;
    uint32_t target = plan.target;
    if (plan.region & 1) {
        uint32_t high = target + SW_LAG_WIDE;
        if (high > plan.most)
            high = plan.most;
        if (target >= now + SW_JUMP && plan.most >= target + SW_LAG_WIDE) {
            /* fall back by about a slice, never forward. */
            uint32_t low = target - SW_LAG_WIDE;
            if (low < now + SW_JUMP / 4u)
                low = now + SW_JUMP / 4u;
            sw_splice(state, sw_match(state, now, low, high, target));
        } else if (now > target + SW_SLACK) {
            /* ahead of the stretch after a seek or a glide back. */
            uint32_t low = target > SW_LAG_WIDE ? target - SW_LAG_WIDE : 0u;
            sw_splice(state, sw_match(state, now, low, high, target));
        }
        return;
    }
    /* a pin keeps one delay. it is set on entering the pin, and again only
       if the glide carries the swing far from it. a hit at the pin holds it
       within 2 ms of the grid; with no hit the timing is free, and the pin
       has a whole bass period to land in phase. */
    {
        int drifted = now + SW_SLACK < target || now > target + SW_SLACK;
        uint32_t reach;
        uint32_t low;
        uint32_t high;
        if (plan.straight) {
            sw_splice(state, target);
            state->placed = 1;
            return;
        }
        if (state->placed && !drifted)
            return;
        reach = sw_onset(state, (int)target - (int)SW_FADE - plan.ahead)
                    ? SW_LAG_NEAR
                    : SW_LAG_WIDE;
        low = target > reach ? target - reach : 0u;
        high = target + reach;
        if (low < plan.floor)
            low = plan.floor;
        if (now < low || now > high)
            sw_splice(state, sw_match(state, now, low, high, target));
        state->placed = 1;
    }
}

static inline void sw_reset(struct sw_state *state)
{
    uint32_t index;
    if (!sw_hann_ready)
        sw_build_hann();
    for (index = 0u; index < SW_RING; index++) {
        state->ring[index].left = 0.0f;
        state->ring[index].right = 0.0f;
    }
    state->written = 0u;
    state->delay = 0u;
    state->from = 0u;
    state->faded = SW_FADE;
    state->region = 0;
    state->placed = 1;
}

/* one block in place. pair_at (0..1) is where the block starts within its
   pair, pair_step the pairs per frame, pair_frames the frames in one pair,
   and slices the sixteenths in one note. a splice is planned for where the
   pair will be when its fade is done. */
static inline void sw_process(struct sw_state *state, struct sw_stereo *buf,
                              int frames, float pair_at, float pair_step,
                              float swing, float pair_frames, int slices)
{
    int index;
    if (!sw_hann_ready)
        sw_build_hann();
    for (index = 0; index < frames; index++) {
        float arrive = (pair_at + pair_step * (float)SW_FADE) * pair_frames;
        struct sw_plan plan;
        struct sw_stereo out;
        state->ring[state->written & SW_RING_MASK] = buf[index];
        state->written++;
        while (pair_frames > 0.0f && arrive >= pair_frames)
            arrive -= pair_frames;
        plan = sw_plan_at(arrive, swing, pair_frames, slices);
        if (plan.region != state->region) {
            state->region = plan.region;
            state->placed = 0;
        }
        if (state->faded >= SW_FADE)
            sw_steer(state, plan);
        out = sw_at(state, state->delay);
        if (state->faded < SW_FADE) {
            float in = sw_hann(0.5f * (float)state->faded / (float)SW_FADE);
            struct sw_stereo old = sw_at(state, state->from);
            out.left = old.left + (out.left - old.left) * in;
            out.right = old.right + (out.right - old.right) * in;
            state->faded++;
        }
        buf[index] = out;
        pair_at += pair_step;
        if (pair_at >= 1.0f)
            pair_at -= 1.0f;
    }
}

#endif /* RX3_SWANG_DSP_H */

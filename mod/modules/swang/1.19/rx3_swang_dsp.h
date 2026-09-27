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
 * largest at the swing point, back to zero at the end. a delay that moves
 * would bend pitch, so each grain plays the ring at normal speed and holds
 * the delay it started with. two grains sit half a length apart, windowed
 * with hann curves that sum to one.
 *
 * stretching repeats audio, so the slow note uses short grains and a hit is
 * not played twice. squeezing skips audio, so the fast note uses long grains
 * and the tone is not chopped into grit. each new grain starts in phase with
 * the one still sounding: mixed a few milliseconds apart, the two copies comb
 * and the midrange distorts. a delay of zero is the input exactly.
 */

#ifndef RX3_SWANG_DSP_H
#define RX3_SWANG_DSP_H

#ifndef RX3_HOOK_TYPES
#include <stdint.h>
#endif

/* the largest delay is a quarter of a pair, and the longest pair (swung
   quarter notes) is two beats: 33075 frames at 40 bpm. the ring holds that
   plus the long grain and the align search. */
#define SW_RING 65536u
#define SW_RING_MASK (SW_RING - 1u)
/* repeat regime and skip regime. one length cannot do both: a long grain
   replays drum hits while stretching, a short grain shreds the tone while
   squeezing. */
#define SW_GRAIN_SLOW 512u
#define SW_GRAIN_FAST 2048u
#define SW_DELAY_LIMIT ((float)(SW_RING - SW_GRAIN_FAST - 4u))
#define SW_STRAIGHT 0.5f
#define SW_HARDEST 0.75f
/* one period of about 57 hz, searched either side of the nominal delay. */
#define SW_ALIGN_MAX 768
#define SW_ALIGN_WINDOW 256u
#define SW_ALIGN_STRIDE 4
#define SW_HANN_TABLE 1024u
/* how long the two readers share the output after the rate flips. */
#define SW_FADE 64u

struct sw_stereo {
    float left;
    float right;
};

/* one reader. grain length is fixed; the two heads stay half a grain apart. */
struct sw_engine {
    uint32_t grain;
    uint32_t age[2];
    float delay[2];
    float align[2];
    float mix;
    int live;
};

struct sw_state {
    struct sw_stereo ring[SW_RING];
    uint32_t written;
    struct sw_engine slow;
    struct sw_engine fast;
    int use_fast;
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

/* how fast source time moves per output frame. below 1 the note is slowed
   and grains repeat; above 1 it is sped up and grains skip. */
static inline float sw_rate_at(float u, float swing)
{
    if (u <= 0.0f || u >= 1.0f || swing <= SW_STRAIGHT)
        return 1.0f;
    if (u < swing)
        return 0.5f / swing;
    return 0.5f / (1.0f - swing);
}

static inline float sw_clamp_delay(float delay)
{
    if (delay < 0.0f)
        return 0.0f;
    if (delay > SW_DELAY_LIMIT)
        return SW_DELAY_LIMIT;
    return delay;
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

/* hann at 0 and 1 is silent, at 1/2 it is unity. half a grain apart the two
   windows sum to one, so the level does not pump. */
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

static float sw_cubic(float a, float b, float c, float d, float t)
{
    float c0 = b;
    float c1 = 0.5f * (c - a);
    float c2 = a - 2.5f * b + 2.0f * c - 0.5f * d;
    float c3 = 0.5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

static struct sw_stereo sw_at(const struct sw_state *state, uint32_t delay)
{
    return state->ring[(state->written - 1u - delay) & SW_RING_MASK];
}

/* fractional delay. an integer delay is that sample exactly, which is what
   makes a delay of zero the input. the cubic tap nearer than the write head
   does not exist, so delays under one frame stay on the newest sample. */
static struct sw_stereo sw_read(const struct sw_state *state, float delay)
{
    uint32_t whole;
    float part;
    struct sw_stereo b;
    delay = sw_clamp_delay(delay);
    whole = (uint32_t)delay;
    part = delay - (float)whole;
    b = sw_at(state, whole);
    if (part == 0.0f || whole == 0u)
        return b;
    {
        struct sw_stereo a = sw_at(state, whole - 1u);
        struct sw_stereo c = sw_at(state, whole + 1u);
        struct sw_stereo d = sw_at(state, whole + 2u);
        struct sw_stereo out;
        out.left = sw_cubic(a.left, b.left, c.left, d.left, part);
        out.right = sw_cubic(a.right, b.right, c.right, d.right, part);
        return out;
    }
}

static float sw_mono(const struct sw_state *state, uint32_t delay)
{
    struct sw_stereo sample = sw_at(state, delay);
    return sample.left + sample.right;
}

static uint32_t sw_whole(float delay)
{
    delay = sw_clamp_delay(delay);
    return (uint32_t)delay;
}

static int sw_lag_ok(float nominal, int lag)
{
    float read = nominal + (float)lag;
    return read >= 0.0f && read <= SW_DELAY_LIMIT;
}

static float sw_corr(const struct sw_state *state, uint32_t other,
                     uint32_t incoming, int stride)
{
    float score = 0.0f;
    uint32_t i;
    for (i = 0u; i < SW_ALIGN_WINDOW; i += (uint32_t)stride)
        score += sw_mono(state, other + i) * sw_mono(state, incoming + i);
    return score;
}

/* lag added to the nominal delay so the new grain matches the head that is
   playing. a whole period away is the same phase but a worse time, so among
   lags that correlate as well as the best, the one nearest zero wins. nothing
   to align to (score <= 0) leaves the nominal delay alone. */
static float sw_align(const struct sw_state *state, float other_read,
                      float nominal)
{
    uint32_t other = sw_whole(other_read);
    int lag;
    int best_lag = 0;
    int near_lag = 0;
    float best_score;
    float floor_score;
    int low;
    int high;
    int fine_lag;
    float fine_score;

    if (!sw_lag_ok(nominal, 0))
        nominal = sw_clamp_delay(nominal);
    best_score = sw_corr(state, other, sw_whole(nominal), SW_ALIGN_STRIDE);
    for (lag = -SW_ALIGN_MAX; lag <= SW_ALIGN_MAX; lag += SW_ALIGN_STRIDE) {
        float score;
        if (lag == 0 || !sw_lag_ok(nominal, lag))
            continue;
        score = sw_corr(state, other, sw_whole(nominal + (float)lag),
                        SW_ALIGN_STRIDE);
        if (score > best_score) {
            best_score = score;
            best_lag = lag;
        }
    }
    if (best_score <= 0.0f)
        return 0.0f;

    /* a periodic copy scores almost as high as the true lag. keep the
       closest, which is the same phase and the delay the warp asked for. */
    floor_score = best_score * 0.999f;
    near_lag = best_lag;
    for (lag = -SW_ALIGN_MAX; lag <= SW_ALIGN_MAX; lag += SW_ALIGN_STRIDE) {
        float score;
        int distance;
        int best_distance;
        if (!sw_lag_ok(nominal, lag))
            continue;
        score = sw_corr(state, other, sw_whole(nominal + (float)lag),
                        SW_ALIGN_STRIDE);
        if (score < floor_score)
            continue;
        distance = lag < 0 ? -lag : lag;
        best_distance = near_lag < 0 ? -near_lag : near_lag;
        if (distance < best_distance)
            near_lag = lag;
    }

    low = near_lag - SW_ALIGN_STRIDE;
    high = near_lag + SW_ALIGN_STRIDE;
    if (low < -SW_ALIGN_MAX)
        low = -SW_ALIGN_MAX;
    if (high > SW_ALIGN_MAX)
        high = SW_ALIGN_MAX;
    fine_lag = near_lag;
    fine_score = -1.0e30f;
    for (lag = low; lag <= high; lag++) {
        float score;
        if (!sw_lag_ok(nominal, lag))
            continue;
        score = sw_corr(state, other, sw_whole(nominal + (float)lag), 1);
        if (score > fine_score) {
            fine_score = score;
            fine_lag = lag;
        }
    }
    if (fine_score <= 0.0f)
        return 0.0f;
    return (float)fine_lag;
}

static void sw_engine_clear(struct sw_engine *engine, uint32_t grain,
                            int live, float mix)
{
    engine->grain = grain;
    engine->age[0] = 0u;
    engine->age[1] = grain / 2u;
    engine->delay[0] = 0.0f;
    engine->delay[1] = 0.0f;
    engine->align[0] = 0.0f;
    engine->align[1] = 0.0f;
    engine->mix = mix;
    engine->live = live;
}

/* delay a grain should hold so its middle, half a grain on, lands on the
   warp. */
static float sw_target_at(float pair_at, float pair_step, float swing,
                          float pair_frames, uint32_t half)
{
    float centre = pair_at + pair_step * (float)half;
    while (centre >= 1.0f)
        centre -= 1.0f;
    while (centre < 0.0f)
        centre += 1.0f;
    return sw_clamp_delay(sw_delay_at(centre, swing) * pair_frames);
}

static float sw_loud_read(const struct sw_engine *engine)
{
    float best = -1.0f;
    float read = 0.0f;
    unsigned int head;
    for (head = 0u; head < 2u; head++) {
        float weight = sw_hann((float)engine->age[head] / (float)engine->grain);
        if (weight > best) {
            best = weight;
            read = engine->delay[head] + engine->align[head];
        }
    }
    return read;
}

/* the head that is about to be heard (weight 1) takes the delay at this
   frame. the silent head takes the delay it will be loudest at. both are
   slid into phase with whatever was already playing. */
static void sw_engine_arm(struct sw_state *state, struct sw_engine *engine,
                          float other_read, float pair_at, float pair_step,
                          float swing, float pair_frames)
{
    float now = sw_clamp_delay(sw_delay_at(pair_at, swing) * pair_frames);
    float centre = sw_target_at(pair_at, pair_step, swing, pair_frames,
                                engine->grain / 2u);
    float now_read = sw_clamp_delay(now + sw_align(state, other_read, now));
    float centre_read =
        sw_clamp_delay(centre + sw_align(state, other_read, centre));
    engine->age[0] = 0u;
    engine->age[1] = engine->grain / 2u;
    engine->delay[0] = centre;
    engine->align[0] = centre_read - centre;
    engine->delay[1] = now;
    engine->align[1] = now_read - now;
}

static struct sw_stereo sw_engine_step(struct sw_state *state,
                                       struct sw_engine *engine,
                                       float pair_at, float pair_step,
                                       float swing, float pair_frames)
{
    struct sw_stereo out;
    float target = sw_target_at(pair_at, pair_step, swing, pair_frames,
                                engine->grain / 2u);
    unsigned int head;
    out.left = 0.0f;
    out.right = 0.0f;
    for (head = 0u; head < 2u; head++) {
        float weight;
        float read;
        struct sw_stereo grain;
        if (engine->age[head] >= engine->grain) {
            unsigned int other = 1u - head;
            float other_read = engine->delay[other] + engine->align[other];
            float lag = sw_align(state, other_read, target);
            float placed = sw_clamp_delay(target + lag);
            engine->age[head] = 0u;
            engine->delay[head] = target;
            engine->align[head] = placed - target;
        }
        weight = sw_hann((float)engine->age[head] / (float)engine->grain);
        read = engine->delay[head] + engine->align[head];
        grain = sw_read(state, read);
        out.left += grain.left * weight;
        out.right += grain.right * weight;
        engine->age[head]++;
    }
    return out;
}

static void sw_slew(struct sw_state *state)
{
    float step = 1.0f / (float)SW_FADE;
    if (state->use_fast) {
        if (state->fast.mix < 1.0f)
            state->fast.mix += step;
        if (state->fast.mix > 1.0f)
            state->fast.mix = 1.0f;
        state->slow.mix = 1.0f - state->fast.mix;
    } else if (state->slow.mix < 1.0f) {
        state->slow.mix += step;
        if (state->slow.mix > 1.0f)
            state->slow.mix = 1.0f;
        state->fast.mix = 1.0f - state->slow.mix;
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
    sw_engine_clear(&state->slow, SW_GRAIN_SLOW, 1, 1.0f);
    sw_engine_clear(&state->fast, SW_GRAIN_FAST, 0, 0.0f);
    state->use_fast = 0;
}

/* one block in place. pair_at (0..1) is where the block starts within its
   pair, pair_step the pairs per frame, pair_frames the frames in one pair. */
static inline void sw_process(struct sw_state *state, struct sw_stereo *buf,
                              int frames, float pair_at, float pair_step,
                              float swing, float pair_frames)
{
    int index;
    if (!sw_hann_ready)
        sw_build_hann();
    for (index = 0; index < frames; index++) {
        int want_fast = sw_rate_at(pair_at, swing) > 1.0f;
        struct sw_stereo slow_out;
        struct sw_stereo fast_out;
        state->ring[state->written & SW_RING_MASK] = buf[index];
        state->written++;
        if (want_fast != state->use_fast) {
            struct sw_engine *incoming = want_fast ? &state->fast : &state->slow;
            struct sw_engine *outgoing = want_fast ? &state->slow : &state->fast;
            if (!incoming->live)
                sw_engine_arm(state, incoming, sw_loud_read(outgoing), pair_at,
                              pair_step, swing, pair_frames);
            incoming->live = 1;
            state->use_fast = want_fast;
        }
        sw_slew(state);
        if (state->slow.mix <= 0.0f)
            state->slow.live = 0;
        if (state->fast.mix <= 0.0f)
            state->fast.live = 0;
        slow_out.left = 0.0f;
        slow_out.right = 0.0f;
        fast_out.left = 0.0f;
        fast_out.right = 0.0f;
        if (state->slow.live)
            slow_out = sw_engine_step(state, &state->slow, pair_at, pair_step,
                                      swing, pair_frames);
        if (state->fast.live)
            fast_out = sw_engine_step(state, &state->fast, pair_at, pair_step,
                                      swing, pair_frames);
        buf[index].left = slow_out.left * state->slow.mix +
                          fast_out.left * state->fast.mix;
        buf[index].right = slow_out.right * state->slow.mix +
                           fast_out.right * state->fast.mix;
        pair_at += pair_step;
        if (pair_at >= 1.0f)
            pair_at -= 1.0f;
    }
}

#endif /* RX3_SWANG_DSP_H */

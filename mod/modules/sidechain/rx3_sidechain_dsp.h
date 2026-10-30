/* SPDX-License-Identifier: MPL-2.0
 * sidechain compressor maths shared by the player and the host test.
 * no libc and no libm: the arm hook links neither, so log2 and exp2 are
 * short polynomials, good to about 0.05 db, which is plenty for gain.
 *
 * the helix beat fx drives it: the beat fx channel is the trigger and the
 * other deck is ducked. DEPTH lowers the threshold, the BEAT arrows step
 * through the ratios, and TIME is the release.
 */

#ifndef RX3_SIDECHAIN_DSP_H
#define RX3_SIDECHAIN_DSP_H

/* the arm build meets the platform's own fixed-width types first; a bare
   host compile brings stdint itself. */
#ifndef RX3_PLATFORM_H
#include <stdint.h>
#endif

#define SC_THRESHOLD_RANGE_DB 36.0f
#define SC_RATIO_COUNT 8u
#define SC_RATIO_DEFAULT 2u
/* the last step is a limiter; the cut still stops at SC_GAIN_FLOOR_L2. */
static const float sc_ratios[SC_RATIO_COUNT] = {
    2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f, 20.0f, 1000.0f
};
/* the deepest cut, 24 db, in log2 units. */
#define SC_GAIN_FLOOR_L2 (-3.9863f)
#define SC_DB_PER_L2 6.0206f
#define SC_SAMPLE_RATE 44100.0f

static float sc_log2(float value)
{
    union { float f; uint32_t i; } bits;
    float mantissa;
    int exponent;
    if (!(value > 1e-12f))
        value = 1e-12f;
    bits.f = value;
    exponent = (int)((bits.i >> 23) & 0xffu) - 127;
    bits.i = (bits.i & 0x007fffffu) | 0x3f800000u;
    mantissa = bits.f;
    /* 1 + log2 on [1, 2), max error about 0.005 */
    return (float)(exponent - 1) +
           ((-0.34484843f * mantissa + 2.02466578f) * mantissa - 0.67487759f);
}

static float sc_exp2(float value)
{
    union { float f; uint32_t i; } bits;
    int whole;
    float part;
    if (value < -30.0f)
        value = -30.0f;
    if (value > 30.0f)
        value = 30.0f;
    whole = (int)value;
    if ((float)whole > value)
        whole--;
    part = value - (float)whole;
    bits.f = 1.0f + part * (0.69314718f + part * (0.24022650f +
                                                  part * (0.05550411f +
                                                          part * 0.00961813f)));
    bits.i += (uint32_t)whole << 23;
    return bits.f;
}

/* depth dial 0..1 to a threshold in log2: 0 db down to -36 db. */
static float sc_threshold_l2(float depth)
{
    if (!(depth > 0.0f))
        depth = 0.0f;
    if (depth > 1.0f)
        depth = 1.0f;
    return -depth * SC_THRESHOLD_RANGE_DB / SC_DB_PER_L2;
}

/* gain, in log2, for a key level over the threshold at a ratio. */
static float sc_gain_l2(float level_l2, float threshold_l2, float ratio)
{
    float over = level_l2 - threshold_l2;
    float gain;
    if (over <= 0.0f || ratio <= 1.0f)
        return 0.0f;
    gain = -over * (1.0f - 1.0f / ratio);
    return gain < SC_GAIN_FLOOR_L2 ? SC_GAIN_FLOOR_L2 : gain;
}

/* the TIME dial in ms, inside helix's own 10..4000 range. */
static float sc_release_ms(uint32_t time_ms)
{
    if (time_ms < 10u)
        time_ms = 10u;
    if (time_ms > 4000u)
        time_ms = 4000u;
    return (float)time_ms;
}

static float sc_attack_ms(float release_ms)
{
    float attack = release_ms / 16.0f;
    return attack < 0.5f ? 0.5f : attack;
}

/* the key is heard through a 150 hz low-pass, so the kick and bass drive the
   duck and hats and vocals do not. second-order butterworth at 44.1 khz,
   direct form i: the poles sit close to 1, where the transposed form loses
   float precision. only the level detector hears it; the trigger plays dry. */
#define SC_KEY_B0 1.1247996e-04f
#define SC_KEY_B1 2.2495992e-04f
#define SC_KEY_A1 (-1.9697786f)
#define SC_KEY_A2 0.97022848f

struct sc_key_filter {
    float x1;
    float x2;
    float y1;
    float y2;
};

static float sc_key_step(struct sc_key_filter *self, float x)
{
    float y = SC_KEY_B0 * (x + self->x2) + SC_KEY_B1 * self->x1 -
              SC_KEY_A1 * self->y1 - SC_KEY_A2 * self->y2;
    self->x2 = self->x1;
    self->x1 = x;
    self->y2 = self->y1;
    self->y1 = y;
    return y;
}

/* a silent deck decays the state into denormals, which are slow on vfp. */
static void sc_key_settle(struct sc_key_filter *self)
{
    float y1 = self->y1 < 0.0f ? -self->y1 : self->y1;
    float y2 = self->y2 < 0.0f ? -self->y2 : self->y2;
    if (y1 < 1e-20f && y2 < 1e-20f) {
        self->y1 = 0.0f;
        self->y2 = 0.0f;
    }
}

/* one-pole step for a block: how much of the gap to the target remains. */
static float sc_block_keep(float time_ms, int frames)
{
    float samples = time_ms * (SC_SAMPLE_RATE / 1000.0f);
    if (samples < 1.0f)
        samples = 1.0f;
    /* exp(-frames / samples) as exp2 */
    return sc_exp2(-1.442695f * (float)frames / samples);
}

#endif /* RX3_SIDECHAIN_DSP_H */

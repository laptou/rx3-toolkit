/* SPDX-License-Identifier: MPL-2.0
 * sidechain compressor maths shared by the player and the host test.
 * no libc and no libm: the arm hook links neither, so log2 and exp2 are
 * short polynomials, good to about 0.05 db, which is plenty for gain.
 *
 * each channel's color knob picks its role. left of centre makes it a
 * source, further left lowers the threshold. right of centre makes it
 * compressed, further right raises the ratio. the centre is off.
 */

#ifndef RX3_SIDECHAIN_DSP_H
#define RX3_SIDECHAIN_DSP_H

#ifndef RX3_HOOK_TYPES
#include <stdint.h>
#endif

#define SC_ROLE_OFF 0
#define SC_ROLE_SOURCE 1
#define SC_ROLE_COMPRESSED 2

#define SC_DEAD_ZONE 0.04f
#define SC_THRESHOLD_RANGE_DB 36.0f
#define SC_RATIO_MAX 20.0f
/* the deepest cut, 24 db, in log2 units. */
#define SC_GAIN_FLOOR_L2 (-3.9863f)
#define SC_DB_PER_L2 6.0206f
#define SC_SAMPLE_RATE 44100.0f

struct sc_role {
    int role;
    float threshold_l2;
    float ratio;
};

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

/* knob 0..1 to a role. */
static struct sc_role sc_role_for_knob(float knob)
{
    struct sc_role out;
    float span = 0.5f - SC_DEAD_ZONE;
    out.role = SC_ROLE_OFF;
    out.threshold_l2 = 0.0f;
    out.ratio = 1.0f;
    if (knob < 0.5f - SC_DEAD_ZONE) {
        float depth = (0.5f - SC_DEAD_ZONE - knob) / span;
        if (depth > 1.0f)
            depth = 1.0f;
        out.role = SC_ROLE_SOURCE;
        out.threshold_l2 = -depth * SC_THRESHOLD_RANGE_DB / SC_DB_PER_L2;
    } else if (knob > 0.5f + SC_DEAD_ZONE) {
        float depth = (knob - 0.5f - SC_DEAD_ZONE) / span;
        if (depth > 1.0f)
            depth = 1.0f;
        out.role = SC_ROLE_COMPRESSED;
        out.ratio = 1.0f + depth * (SC_RATIO_MAX - 1.0f);
    }
    return out;
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

/* release in ms: the parameter dial scales one beat of the source deck,
   from an eighth of a beat (0) through half a beat (0.5) to two beats (1). */
static float sc_release_ms(float beat_ms, float parameter)
{
    if (!(beat_ms > 50.0f && beat_ms < 3000.0f))
        beat_ms = 500.0f;
    if (parameter < 0.0f)
        parameter = 0.0f;
    if (parameter > 1.0f)
        parameter = 1.0f;
    return beat_ms * sc_exp2(4.0f * parameter - 3.0f);
}

static float sc_attack_ms(float release_ms)
{
    float attack = release_ms / 16.0f;
    return attack < 0.5f ? 0.5f : attack;
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

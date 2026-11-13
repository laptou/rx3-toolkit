/* SPDX-License-Identifier: MPL-2.0 */
/* host check of the sidechain maths against libm. */

#include "mod/modules/sidechain/rx3_sidechain_dsp.h"

#include <math.h>
#include <stdio.h>

static int near(float got, float want, float tolerance, const char *what)
{
    if (fabsf(got - want) > tolerance) {
        printf("%s: got %f want %f\n", what, got, want);
        return 0;
    }
    return 1;
}

int main(void)
{
    float value;
    for (value = 0.001f; value < 4.0f; value *= 1.37f)
        if (!near(sc_log2(value), log2f(value), 0.01f, "log2"))
            return 1;
    for (value = -20.0f; value < 4.0f; value += 0.173f)
        if (!near(sc_exp2(value), exp2f(value), exp2f(value) * 0.001f, "exp2"))
            return 2;
    if (!near(sc_threshold_l2(0.0f), 0.0f, 0.0001f, "threshold top") ||
        !near(sc_threshold_l2(1.0f) * SC_DB_PER_L2, -36.0f, 0.01f, "threshold") ||
        !near(sc_threshold_l2(2.0f) * SC_DB_PER_L2, -36.0f, 0.01f, "threshold clamp"))
        return 3;
    if (sc_ratios[SC_RATIO_DEFAULT] != 4.0f)
        return 4;
    for (value = 1.0f; value < (float)SC_RATIO_COUNT; value += 1.0f)
        if (!(sc_ratios[(unsigned)value] > sc_ratios[(unsigned)value - 1u]))
            return 5;
    /* 12 db over at 4:1 cuts 9 db */
    if (!near(sc_gain_l2(0.0f, -12.0f / SC_DB_PER_L2, 4.0f) * SC_DB_PER_L2,
              -9.0f, 0.01f, "gain"))
        return 6;
    if (sc_gain_l2(-2.0f, -1.0f, 4.0f) != 0.0f)
        return 7;
    /* the cut never passes 24 db */
    if (!near(sc_gain_l2(0.0f, -10.0f, 20.0f), SC_GAIN_FLOOR_L2, 0.0001f, "floor"))
        return 8;
    /* TIME is the release, inside helix's range */
    if (sc_release_ms(500u) != 500.0f || sc_release_ms(1u) != 10.0f ||
        sc_release_ms(9000u) != 4000.0f)
        return 9;
    if (!near(sc_block_keep(10.0f, 441), expf(-1.0f), 0.002f, "keep"))
        return 11;
    if (!near(sc_attack_ms(160.0f), 10.0f, 0.001f, "attack") ||
        sc_attack_ms(1.0f) != 0.5f)
        return 12;
    /* the key filter passes the kick, is 3 db down at 150 hz, and holds the
       hats well under it */
    {
        static const float hz[4] = { 50.0f, 150.0f, 1000.0f, 5000.0f };
        static const float want_db[4] = { 0.0f, -3.01f, -33.0f, -61.0f };
        static const float tolerance_db[4] = { 0.2f, 0.2f, 1.0f, 2.0f };
        unsigned tone;
        for (tone = 0; tone < 4u; tone++) {
            struct sc_key_filter key = { 0.0f, 0.0f, 0.0f, 0.0f };
            float peak = 0.0f;
            int n;
            for (n = 0; n < 44100; n++) {
                float y = sc_key_step(
                    &key, sinf(6.2831853f * hz[tone] * (float)n / SC_SAMPLE_RATE));
                if (n >= 22050 && fabsf(y) > peak)
                    peak = fabsf(y);
            }
            if (!near(20.0f * log10f(peak), want_db[tone], tolerance_db[tone],
                      "key filter"))
                return 13;
            /* a deck gone silent settles to exactly zero */
            for (n = 0; n < 441000; n++)
                sc_key_step(&key, 0.0f);
            sc_key_settle(&key);
            if (key.y1 != 0.0f || key.y2 != 0.0f)
                return 14;
        }
    }
    puts("sidechain dsp: OK");
    return 0;
}

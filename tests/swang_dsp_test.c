/* SPDX-License-Identifier: MPL-2.0 */
/* host check of the swing warp: its shape, a clean passthrough, and where a
   straight eighth-note click lands once it is swung. */

#include "mod/modules/swang/1.19/rx3_swang_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PAIR 11025
#define PAIRS 8
#define TOTAL (PAIR * PAIRS)
#define BLOCK 256

static int near(float got, float want, float tolerance, const char *what)
{
    if (fabsf(got - want) > tolerance) {
        printf("%s: got %f want %f\n", what, got, want);
        return 0;
    }
    return 1;
}

/* the centroid of |output| in a window, in frames from its start. */
static float centroid(const struct sw_stereo *buf, int from, int to)
{
    double weight = 0.0;
    double sum = 0.0;
    int index;
    for (index = from; index < to; index++) {
        double level = fabs(buf[index].left);
        weight += level;
        sum += level * (index - from);
    }
    return weight > 0.0 ? (float)(sum / weight) : -1.0f;
}

static void run(struct sw_state *state, struct sw_stereo *buf, float swing)
{
    int start;
    sw_reset(state);
    for (start = 0; start < TOTAL; start += BLOCK) {
        int frames = TOTAL - start < BLOCK ? TOTAL - start : BLOCK;
        float at = (float)(start % PAIR) / (float)PAIR;
        sw_process(state, buf + start, frames, at, 1.0f / (float)PAIR, swing,
                   (float)PAIR, 2);
    }
}

/* share of energy, after the first pair, that still sits within ±20 hz of
   the tone. a comb or an out-of-phase splice throws it into sidebands; a
   locked splice does not. */
static double purity(const struct sw_stereo *buf, int from, int to, double hz)
{
    int n = to - from;
    int k0 = (int)((hz - 20.0) * (double)n / 44100.0 + 0.5);
    int k1 = (int)((hz + 20.0) * (double)n / 44100.0 + 0.5);
    int k;
    int i;
    double energy = 0.0;
    double band = 0.0;
    for (i = 0; i < n; i++) {
        double sample = buf[from + i].left;
        energy += sample * sample;
    }
    for (k = k0; k <= k1; k++) {
        double angular = 2.0 * 3.141592653589793 * (double)k / (double)n;
        double coeff = 2.0 * cos(angular);
        double s1 = 0.0;
        double s2 = 0.0;
        double power;
        for (i = 0; i < n; i++) {
            double next = buf[from + i].left + coeff * s1 - s2;
            s2 = s1;
            s1 = next;
        }
        power = s2 * s2 + s1 * s1 - coeff * s1 * s2;
        /* the matching negative-frequency bin holds the other half. */
        band += 2.0 * power;
    }
    return energy > 0.0 ? band / ((double)n * energy) : 0.0;
}

int main(void)
{
    static struct sw_state state;
    struct sw_stereo *buf = calloc(TOTAL, sizeof(*buf));
    float swing = 2.0f / 3.0f;
    int pair;
    int index;

    /* the plan for swung eighths, two sixteenths a note: each sixteenth is
       pinned where the swing moves it, and the first note's tails stretch
       between pins. */
    {
        float gap = (swing - 0.5f) * PAIR;
        float lands[4] = { 0.0f, 0.5f * swing * PAIR, swing * PAIR,
                           (swing + 0.5f * (1.0f - swing)) * PAIR };
        float delays[4] = { 0.0f, 0.5f * gap, gap, 0.5f * gap };
        struct sw_plan plan;
        for (index = 0; index < 4; index++) {
            plan = sw_plan_at(lands[index] + 10.0f, swing, (float)PAIR, 2);
            if (plan.region != 2 * index || plan.straight ||
                !near((float)plan.target, delays[index], 1.0f, "pin"))
                return 1;
        }
        plan = sw_plan_at(PAIR - 10.0f, swing, (float)PAIR, 2);
        if (plan.region != 0 || plan.target != 0u)
            return 1;
        plan = sw_plan_at(0.2f * PAIR, swing, (float)PAIR, 2);
        if (plan.region != 1 || plan.target == 0u ||
            (float)plan.target >= 0.5f * gap) {
            printf("stretch is off\n");
            return 1;
        }
        plan = sw_plan_at(0.8f * PAIR, SW_STRAIGHT, (float)PAIR, 2);
        if (plan.region != 0 || !plan.straight)
            return 1;
    }
    if (!near(sw_swing_for(0.0f), 0.5f, 0.0f, "depth 0") ||
        !near(sw_swing_for(1.0f), 0.75f, 0.0f, "depth 1"))
        return 3;
    {
        int xs[8];
        int got;
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 1.0f, 0.0f, 0.5f, xs, 8);
        if (got != 1 || xs[0] != 50) {
            printf("straight eighth: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 1.0f, 0.0f, 0.75f, xs, 8);
        if (got != 1 || xs[0] != 75) {
            printf("hard eighth: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 300, 1.0f, 0.0f, 2.0f / 3.0f, xs, 8);
        if (got != 1 || xs[0] != 200) {
            printf("triplet eighth: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 1.0f, 0.25f, 0.5f, xs, 8);
        if (got != 1 || xs[0] != 75) {
            printf("phased eighth: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 2.0f, 0.0f, 0.5f, xs, 8);
        if (got != 0) {
            printf("straight quarters leaked a tick\n");
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 2.0f, 0, 200, 2.0f, 0.0f, 0.75f, xs, 8);
        if (got != 1 || xs[0] != 150) {
            printf("hard quarters: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 0.5f, 0.0f, 0.5f, xs, 8);
        if (got != 2 || xs[0] != 25 || xs[1] != 75) {
            printf("straight sixteenths: %d\n", got);
            return 8;
        }
        got = sw_offbeat_xs(0.0f, 1.0f, 0, 100, 1.0f, 0.9f, 0.75f, xs, 8);
        if (got != 1 || xs[0] != 65) {
            printf("wrapped phase: %d at %d\n", got, got ? xs[0] : -1);
            return 8;
        }
    }
    /* straight swing is the input, bit for bit. */
    for (index = 0; index < TOTAL; index++) {
        buf[index].left = sinf((float)index * 0.05f);
        buf[index].right = -buf[index].left;
    }
    run(&state, buf, SW_STRAIGHT);
    for (index = 0; index < TOTAL; index++)
        if (buf[index].left != sinf((float)index * 0.05f) ||
            buf[index].right != -sinf((float)index * 0.05f)) {
            printf("passthrough differs at %d\n", index);
            return 4;
        }

    /* a short click on every sixteenth of swung eighths. each lands where
       the swing moves it (the first note's spread out, the second note's
       closed up), no more than 2 ms late, and is played once and whole: no
       splice runs through a hit. */
    for (pair = 0; pair < 3; pair++) {
        static const float swings[3] = { 0.6f, 2.0f / 3.0f, SW_HARDEST };
        float hit_swing = swings[pair];
        float lands[4];
        int later;
        int hit;
        lands[0] = 0.0f;
        lands[1] = 0.5f * hit_swing;
        lands[2] = hit_swing;
        lands[3] = hit_swing + 0.5f * (1.0f - hit_swing);
        for (index = 0; index < TOTAL; index++)
            buf[index].left = buf[index].right =
                index % (PAIR / 4) < 32 ? 1.0f : 0.0f;
        run(&state, buf, hit_swing);
        for (later = 1; later < PAIRS; later++)
            for (hit = 0; hit < 4; hit++) {
                int from = later * PAIR + (int)(lands[hit] * PAIR) - PAIR / 16;
                int to = from + PAIR / 8;
                double energy = 0.0;
                for (index = from; index < to; index++)
                    energy += buf[index].left * buf[index].left;
                if (!near(centroid(buf, from, to) + (float)from,
                          (float)later * PAIR + lands[hit] * PAIR + 16.0f,
                          (float)SW_LAG_NEAR + 1.0f, "sixteenth") ||
                    !near((float)energy, 32.0f, 0.5f, "sixteenth level")) {
                    printf("swing %.3f, pair %d, sixteenth %d\n", hit_swing,
                           later, hit);
                    return 5;
                }
            }
    }

    /* a click on every thirty-second, across tempos, depths and notes. the
       stretch must not splice through a hit inside a sixteenth: no ghost of
       a hit above -20 db, and no hit cut short by more than 2%. */
    {
        static const float swings[3] = { 0.6f, 2.0f / 3.0f, SW_HARDEST };
        struct sw_stereo *clicks = calloc(700000, sizeof(*clicks));
        double bpm;
        int slices;
        int at;
        for (slices = 1; slices <= 4; slices *= 2)
            for (bpm = 100.0; bpm <= 160.0; bpm += 7.3)
                for (pair = 0; pair < 3; pair++) {
                    double beat = 44100.0 * 60.0 / bpm;
                    double pair_frames = beat * slices / 2.0;
                    int total = (int)(pair_frames * 10.0);
                    for (index = 0; index < total; index++) {
                        double in = fmod((double)index, beat / 8.0);
                        clicks[index].left = clicks[index].right =
                            in < 24.0 ? 0.5f : 0.0f;
                    }
                    sw_reset(&state);
                    for (at = 0; at < total; at += BLOCK) {
                        int frames = total - at < BLOCK ? total - at : BLOCK;
                        sw_process(&state, clicks + at, frames,
                                   (float)fmod(at / pair_frames, 1.0),
                                   (float)(1.0 / pair_frames), swings[pair],
                                   (float)pair_frames, slices);
                    }
                    for (index = (int)(3.0 * pair_frames);
                         index < (int)(9.0 * pair_frames);) {
                        double share = 0.0;
                        if (fabsf(clicks[index].left) <= 1e-4f) {
                            index++;
                            continue;
                        }
                        while (fabsf(clicks[index].left) > 1e-4f) {
                            share += clicks[index].left * clicks[index].left;
                            index++;
                        }
                        share /= 24.0 * 0.25;
                        if ((share < 0.5 && share > 0.01) ||
                            (share >= 0.5 && share < 0.98)) {
                            printf("bpm %.1f swing %.3f slices %d: a hit at "
                                   "%.3f of its energy\n",
                                   bpm, swings[pair], slices, share);
                            free(clicks);
                            return 9;
                        }
                    }
                }
        free(clicks);
    }

    /* a treble and a bass tone through the warp, after the first pair.
       out-of-phase splices would spread them into sidebands. */
    {
        static const double tones[2] = { 440.0, 55.0 };
        double worst = 1.0;
        int tone;
        for (tone = 0; tone < 2; tone++)
            for (pair = 0; pair < 2; pair++) {
                double step = 2.0 * 3.141592653589793 * tones[tone] / 44100.0;
                double pure;
                for (index = 0; index < TOTAL; index++)
                    buf[index].left = buf[index].right =
                        (float)sin(step * (double)index);
                run(&state, buf, pair ? SW_HARDEST : swing);
                pure = purity(buf, PAIR, TOTAL, tones[tone]);
                if (pure < worst)
                    worst = pure;
                if (pure < 0.95) {
                    printf("%.0f hz purity %.3f, below 0.95\n", tones[tone],
                           pure);
                    free(buf);
                    return 7;
                }
            }
        printf("swang dsp: ok (worst purity %.3f)\n", worst);
    }
    free(buf);
    return 0;
}

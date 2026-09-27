/* SPDX-License-Identifier: MPL-2.0
 * sidechain on the DUB ECHO sound color fx, firmware 1.19.
 *
 * rbp sums the mix in software. each mixer channel runs
 * SoundColorFxManager::operate(manager, buf, frames) once per block on the
 * audio thread. while DUB ECHO is the selected type (manager +0x24 == 4) the
 * stock echo is skipped entirely and the block goes through the compressor
 * instead. because the manager never switches to its echo slot, the knobs
 * keep landing on the active effect (*manager): +0x1c color, +0x20 parameter.
 * manager +0x84 is the MixerChannel, whose +0x14 is the input (0 or 1), and
 * mixer input n is fed by deck n.
 *
 * the compressed channel keys on the source channel's most recent block
 * peak, which is this block or the one before depending on the order the
 * mixer runs the channels: at most one block late.
 */

#ifndef RX3_SIDECHAIN_FEATURE_H
#define RX3_SIDECHAIN_FEATURE_H

#include "rx3_sidechain_dsp.h"

#define COLOR_FX_OPERATE ((unsigned long)0x000bbc68)
#define COLOR_FX_DUB_ECHO 4

typedef void (*color_fx_operate_fn)(void *, Float2 *, int);

/* stmdb sp!,{r3-r8,r10,lr} ; vpush {d8} */
static const uint8_t color_fx_operate_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x02, 0x8b, 0x2d, 0xed
};

struct sc_channel {
    volatile float level_l2;
    volatile int role;
    volatile float threshold_l2;
    float gain;
    float last_parameter;
};

static struct installed_hook color_fx_operate_hook;
static color_fx_operate_fn original_color_fx_operate;
static struct sc_channel sc_channels[2] = {
    { -40.0f, SC_ROLE_OFF, 0.0f, 1.0f, -1.0f },
    { -40.0f, SC_ROLE_OFF, 0.0f, 1.0f, -1.0f },
};
/* the parameter dial is one control; take whichever channel saw it move. */
static volatile float sc_parameter = 0.5f;
static int sc_logged;

/* one beat of the deck's grid in ms, from the first grid entry's bpm. the
   same plain loads as the sprite beat clock; 0 when there is no grid. */
static float sc_deck_beat_ms(unsigned deck)
{
    const uint8_t *reader = (const uint8_t *)deck_readers[deck];
    const uint8_t *impl;
    const uint8_t *grid;
    const uint8_t *beats;
    uint16_t bpm;
    if (!reader)
        return 0.0f;
    impl = *(const uint8_t *const volatile *)(reader + 4u);
    if (!impl)
        return 0.0f;
    grid = *(const uint8_t *const volatile *)(impl + 0x69cu);
    if (!grid || !*(const volatile uint32_t *)(grid + 0x24u))
        return 0.0f;
    beats = *(const uint8_t *const volatile *)(grid + 0x34u);
    if (!beats)
        return 0.0f;
    bpm = *(const volatile uint16_t *)(beats + 2u);
    return bpm ? 6000000.0f / (float)bpm : 0.0f;
}

static float sc_block_peak_l2(const Float2 *buf, int frames)
{
    float peak = 0.0f;
    int index;
    for (index = 0; index < frames; index++) {
        float left = buf[index].left < 0.0f ? -buf[index].left : buf[index].left;
        float right =
            buf[index].right < 0.0f ? -buf[index].right : buf[index].right;
        if (left > peak)
            peak = left;
        if (right > peak)
            peak = right;
    }
    return sc_log2(peak);
}

static void sc_process(unsigned input, const uint8_t *effect, Float2 *buf,
                       int frames)
{
    struct sc_channel *self = &sc_channels[input];
    struct sc_channel *other = &sc_channels[input ^ 1u];
    struct sc_role role = sc_role_for_knob(*(const float *)(effect + 0x1cu));
    float parameter = *(const float *)(effect + 0x20u);
    float target = 1.0f;
    float start = self->gain;
    float step;
    float release;
    int index;
    if (parameter != self->last_parameter) {
        if (self->last_parameter >= 0.0f)
            sc_parameter = parameter;
        self->last_parameter = parameter;
    }
    self->level_l2 = sc_block_peak_l2(buf, frames);
    self->threshold_l2 = role.threshold_l2;
    self->role = role.role;
    if (role.role == SC_ROLE_COMPRESSED && other->role == SC_ROLE_SOURCE)
        target = sc_exp2(sc_gain_l2(other->level_l2, other->threshold_l2,
                                    role.ratio));
    release = sc_release_ms(sc_deck_beat_ms(input ^ 1u), sc_parameter);
    self->gain = target +
                 (start - target) *
                     sc_block_keep(target < start ? sc_attack_ms(release)
                                                  : release,
                                   frames);
    if (start == 1.0f && self->gain == 1.0f)
        return;
    /* ramp across the block so a gain change never clicks. */
    step = (self->gain - start) / (float)frames;
    for (index = 0; index < frames; index++) {
        float gain = start + step * (float)(index + 1);
        buf[index].left *= gain;
        buf[index].right *= gain;
    }
    if (self->gain > 0.9999f)
        self->gain = 1.0f;
}

static void hooked_color_fx_operate(void *manager, Float2 *buf, int frames)
{
    const uint8_t *fields = (const uint8_t *)manager;
    const uint8_t *channel = *(const uint8_t *const *)(fields + 0x84u);
    const uint8_t *effect = *(const uint8_t *const *)fields;
    unsigned input = channel ? *(const uint32_t *)(channel + 0x14u) : 2u;
    if (input >= 2u || !effect || !buf || frames <= 0 ||
        *(const int *)(fields + 0x24u) != COLOR_FX_DUB_ECHO) {
        if (input < 2u) {
            sc_channels[input].role = SC_ROLE_OFF;
            sc_channels[input].gain = 1.0f;
        }
        original_color_fx_operate(manager, buf, frames);
        return;
    }
    if (!sc_logged) {
        sc_logged = 1;
        log_line("sidechain: DUB ECHO is now the sidechain compressor");
    }
    sc_process(input, effect, buf, frames);
}

static int sidechain_feature_configured(void)
{
    const char *flag = getenv("RX3_SIDECHAIN");
    return flag && flag[0] == '1';
}

static int sidechain_feature_install(void)
{
    original_color_fx_operate = (color_fx_operate_fn)install_hook(
        &color_fx_operate_hook, COLOR_FX_OPERATE, color_fx_operate_guard,
        (void *)hooked_color_fx_operate);
    return original_color_fx_operate != 0;
}

static void sidechain_feature_remove(void)
{
    uninstall_hook(&color_fx_operate_hook);
    original_color_fx_operate = 0;
}

#endif /* RX3_SIDECHAIN_FEATURE_H */

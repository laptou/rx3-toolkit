/* SPDX-License-Identifier: MPL-2.0
 * sidechain on the HELIX beat fx, firmware 1.19.
 *
 * rbp sums the mix in software. every block, MixerChannel::updateFilter
 * leaves each deck's post-fader output for the master to sum, and the core
 * hands that block to channel_block below. while HELIX is the beat fx and
 * it is ON, the beat fx channel (CH1 or CH2) is the trigger and the other
 * deck is ducked by its level.
 *
 * the stock helix never runs: operateBeatEffect returns at once while helix
 * is the settled effect, so the trigger channel plays dry. the controls are
 * read from the BeatEffectManager (MixerChannel +0x218):
 *   +0x00 beat fx channel (0 CH1, 1 CH2, others are mic, master, aux)
 *   +0x04 the channel it is switching to
 *   +0x08 active effect: +0x24 TIME in ms, +0x3c ON
 *   +0x4c / +0x50 committed / requested type, 14 is helix
 *   +0xe8 DEPTH 0..1, as the dial left it; operateBeatEffect is what copies
 *         it onto the effect, and it no longer runs
 * the BEAT arrows step the ratio: MasterInnards::onEv_BeatFxBeat(this,
 * delta) is taken over while its type (+0x3c, ui numbering) is helix, 4.
 *
 * CH1 runs before CH2, so a CH2 trigger keys CH1 one block late.
 */

#ifndef RX3_SIDECHAIN_FEATURE_H
#define RX3_SIDECHAIN_FEATURE_H

#include "../../core/1.19/rx3_ds_gr.h"
#include "rx3_sidechain_dsp.h"
#include "rx3_sidechain_meter.h"

#define OPERATE_BEAT_EFFECT ((unsigned long)0x0008a52c)
#define BEAT_FX_BEAT_EVENT ((unsigned long)0x0034553c)
#define BEAT_FX_HELIX 14
#define BEAT_FX_UI_HELIX 4

typedef void (*operate_beat_effect_fn)(void *, Float2 *, int, int);
typedef int (*beat_fx_beat_event_fn)(void *, long);

/* stmdb sp!,{r3-r8,r10,lr} ; mov r6,r1 */
static const uint8_t operate_beat_effect_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x01, 0x60, 0xa0, 0xe1
};
/* ldrb r3,[r0,#0xe9] ; stmdb sp!,{r4-r8,lr} */
static const uint8_t beat_fx_beat_event_guard[8] = {
    0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9
};

struct sc_deck {
    volatile float level_l2;
    float gain;
};

static struct installed_hook operate_beat_effect_hook;
static struct installed_hook beat_fx_beat_event_hook;
static operate_beat_effect_fn original_operate_beat_effect;
static beat_fx_beat_event_fn original_beat_fx_beat_event;
static struct sc_deck sc_decks[2] = { { -40.0f, 1.0f }, { -40.0f, 1.0f } };
static volatile unsigned sc_ratio_index = SC_RATIO_DEFAULT;
static int sc_logged;

static int sc_helix_settled(const uint8_t *manager)
{
    return *(const int *)(manager + 0x4cu) == BEAT_FX_HELIX &&
           *(const int *)(manager + 0x50u) == BEAT_FX_HELIX &&
           *(const int *)manager == *(const int *)(manager + 0x04u);
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

/* moves the deck's gain toward target and ramps it across the block so a
   gain change never clicks. */
static void sc_apply_gain(struct sc_deck *self, float target, float release,
                          Float2 *buf, int frames)
{
    float start = self->gain;
    float step;
    int index;
    self->gain = target +
                 (start - target) *
                     sc_block_keep(target < start ? sc_attack_ms(release)
                                                  : release,
                                   frames);
    if (self->gain > 0.9999f)
        self->gain = 1.0f;
    if (start == 1.0f && self->gain == 1.0f)
        return;
    step = (self->gain - start) / (float)frames;
    for (index = 0; index < frames; index++) {
        float gain = start + step * (float)(index + 1);
        buf[index].left *= gain;
        buf[index].right *= gain;
    }
}

static void sidechain_feature_channel_block(unsigned int deck, void *channel,
                                            Float2 *output, int frames)
{
    struct sc_deck *self = &sc_decks[deck];
    const uint8_t *manager =
        *(const uint8_t *const *)((const uint8_t *)channel + 0x218u);
    const uint8_t *effect;
    unsigned trigger;
    float threshold;
    float release;
    self->level_l2 = sc_block_peak_l2(output, frames);
    effect = manager ? *(const uint8_t *const *)(manager + 0x08u) : 0;
    trigger = manager ? *(const uint32_t *)manager : 2u;
    if (!effect || trigger >= 2u || !sc_helix_settled(manager) ||
        !*(const uint8_t *)(effect + 0x3cu)) {
        sc_apply_gain(self, 1.0f, 50.0f, output, frames);
        return;
    }
    if (!sc_logged) {
        sc_logged = 1;
        log_line("sidechain: HELIX is now the sidechain compressor");
    }
    threshold = sc_threshold_l2(*(const float *)(manager + 0xe8u));
    release = sc_release_ms(*(const uint32_t *)(effect + 0x24u));
    sc_meter_note_block();
    if (deck == trigger) {
        sc_meter_note_source(self->level_l2, threshold);
        sc_apply_gain(self, 1.0f, release, output, frames);
        return;
    }
    sc_apply_gain(self,
                  sc_exp2(sc_gain_l2(sc_decks[trigger].level_l2, threshold,
                                     sc_ratios[sc_ratio_index])),
                  release, output, frames);
    sc_meter_note_cut(self->gain);
}

/* helix stays dry: its wet path is skipped while it is the settled effect.
   a switch to or from helix still runs stock, so rbp's crossfade happens. */
static void hooked_operate_beat_effect(void *manager, Float2 *buf, int frames,
                                       int preview)
{
    if (manager && sc_helix_settled((const uint8_t *)manager))
        return;
    original_operate_beat_effect(manager, buf, frames, preview);
}

/* the BEAT arrows pick the ratio while helix is the selected beat fx. */
static int hooked_beat_fx_beat_event(void *innards, long delta)
{
    long index;
    if (*(const int *)((const uint8_t *)innards + 0x3cu) != BEAT_FX_UI_HELIX)
        return original_beat_fx_beat_event(innards, delta);
    index = (long)sc_ratio_index + delta;
    if (index < 0)
        index = 0;
    if (index >= (long)SC_RATIO_COUNT)
        index = (long)SC_RATIO_COUNT - 1;
    sc_ratio_index = (unsigned)index;
    return 0;
}

/* driven from rbp's image draws, which run on its ui thread. */
static void sidechain_feature_after_image(void *render, void *image,
                                          uint16_t window_layer,
                                          uint32_t image_id)
{
    (void)render;
    (void)image;
    (void)window_layer;
    (void)image_id;
    sc_meter_tick();
}

static int sidechain_feature_configured(void)
{
    const char *flag = getenv("RX3_SIDECHAIN");
    return flag && flag[0] == '1';
}

static int sidechain_feature_install(void)
{
    original_operate_beat_effect = (operate_beat_effect_fn)install_hook(
        &operate_beat_effect_hook, OPERATE_BEAT_EFFECT,
        operate_beat_effect_guard, (void *)hooked_operate_beat_effect);
    original_beat_fx_beat_event = (beat_fx_beat_event_fn)install_hook(
        &beat_fx_beat_event_hook, BEAT_FX_BEAT_EVENT, beat_fx_beat_event_guard,
        (void *)hooked_beat_fx_beat_event);
    return original_operate_beat_effect && original_beat_fx_beat_event;
}

static void sidechain_feature_remove(void)
{
    uninstall_hook(&beat_fx_beat_event_hook);
    uninstall_hook(&operate_beat_effect_hook);
    original_beat_fx_beat_event = 0;
    original_operate_beat_effect = 0;
    rx3_gr_close(&sc_meter_window);
}

#endif /* RX3_SIDECHAIN_FEATURE_H */

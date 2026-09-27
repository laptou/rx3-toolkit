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
 * on screen, helix's caption images read SIDECHAIN, and the beat number
 * reads the ratio. the play screen's beat fx block names its beat number by
 * a label index (+0x10); each ratio writes its own label there, so rbp
 * redraws when the ratio changes, and that label's number image is swapped
 * for the ratio's lettering.
 *
 * CH1 runs before CH2, so a CH2 trigger keys CH1 one block late.
 */

#ifndef RX3_SIDECHAIN_FEATURE_H
#define RX3_SIDECHAIN_FEATURE_H

#include "../../core/1.19/rx3_ds_gr.h"
#include "rx3_sidechain_dsp.h"
#include "rx3_sidechain_meter.h"
#include "rx3_sidechain_labels.h"

#define BEAT_FX_HELIX 14
#define BEAT_FX_UI_HELIX 4
/* the play screen's display numbering (CSWTCH.55 of the ui type). */
#define BEAT_FX_DISPLAY_HELIX 13
/* ui_PlayGroup +0x1e0, the copy of the beat fx block the ui draws from. */
#define BEAT_FX_SHOWN_TYPE ((const volatile int *)0x032b2c68)
/* two colour banks of 16 beat-number images, by label index. */
#define BEAT_NUMBER_IMAGES ((const uint16_t *)0x004c4780)
#define BEAT_NUMBER_IMAGE_COUNT 32u
#define BEAT_NUMBER_FIRST_ID 0x1547u
#define BEAT_NUMBER_LAST_ID 0x1561u
/* helix's name: four play-screen captions (off and on, two colour banks)
   and three select-grid tiles (dim, normal, selected). */
#define HELIX_CAPTION_FIRST 0x1449u
#define HELIX_CAPTION_LAST 0x144cu
#define HELIX_SELECT_FIRST 0x15aau
#define HELIX_SELECT_LAST 0x15acu

struct sc_deck {
    volatile float level_l2;
    float gain;
};

/* the beat labels the ratios borrow: 1/16 1/8 1/4 1/2 3/4 16 32 64. */
static const uint32_t sc_ratio_beat_labels[SC_RATIO_COUNT] = {
    3u, 4u, 5u, 6u, 7u, 12u, 13u, 14u,
};
static const struct rx3_label_mask *const sc_ratio_masks[SC_RATIO_COUNT] = {
    &sc_label_ratio_2, &sc_label_ratio_3, &sc_label_ratio_4,
    &sc_label_ratio_6, &sc_label_ratio_8, &sc_label_ratio_12,
    &sc_label_ratio_20, &sc_label_ratio_inf,
};

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
static int sidechain_feature_beat_fx_operate(void *manager, Float2 *buffer,
                                             int frames)
{
    (void)buffer;
    (void)frames;
    return sc_helix_settled((const uint8_t *)manager);
}

/* the BEAT arrows pick the ratio while helix is the selected beat fx. */
static int sidechain_feature_beat_fx_beat(void *innards, long delta)
{
    long index;
    if (*(const int *)((const uint8_t *)innards + 0x3cu) != BEAT_FX_UI_HELIX)
        return 0;
    index = (long)sc_ratio_index + delta;
    if (index < 0)
        index = 0;
    if (index >= (long)SC_RATIO_COUNT)
        index = (long)SC_RATIO_COUNT - 1;
    sc_ratio_index = (unsigned)index;
    return 1;
}

static void sidechain_feature_beat_fx_info(uint8_t *block)
{
    if (*(const int *)block == BEAT_FX_DISPLAY_HELIX)
        *(uint32_t *)(block + 0x10u) = sc_ratio_beat_labels[sc_ratio_index];
}

static uint32_t sidechain_feature_image_swap(uint32_t image_id)
{
    unsigned index;
    unsigned ratio;
    uint32_t label;
    if ((image_id >= HELIX_CAPTION_FIRST && image_id <= HELIX_CAPTION_LAST) ||
        (image_id >= HELIX_SELECT_FIRST && image_id <= HELIX_SELECT_LAST))
        return rx3_relabel_image(image_id, &sc_label_caption);
    if (image_id < BEAT_NUMBER_FIRST_ID || image_id > BEAT_NUMBER_LAST_ID ||
        *BEAT_FX_SHOWN_TYPE != BEAT_FX_DISPLAY_HELIX)
        return image_id;
    for (index = 0; index < BEAT_NUMBER_IMAGE_COUNT; index++)
        if (BEAT_NUMBER_IMAGES[index] == image_id)
            break;
    if (index == BEAT_NUMBER_IMAGE_COUNT)
        return image_id;
    label = index % 16u;
    for (ratio = 0; ratio < SC_RATIO_COUNT; ratio++)
        if (sc_ratio_beat_labels[ratio] == label)
            return rx3_relabel_image(image_id, sc_ratio_masks[ratio]);
    return image_id;
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

/* without the operate and beat hooks helix would play wet and the arrows
   would change its beat, so the feature does not start. */
static int sidechain_feature_install(void)
{
    return original_operate_beat_effect && original_beat_fx_beat_event;
}

static void sidechain_feature_remove(void)
{
    rx3_gr_close(&sc_meter_window);
}

#endif /* RX3_SIDECHAIN_FEATURE_H */

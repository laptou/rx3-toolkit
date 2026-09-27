/* SPDX-License-Identifier: MPL-2.0
 * the beat fx broker, firmware 1.19.
 *
 * features that take over a stock beat fx all need the same four places in
 * rbp, and a function can carry only one hook, so the core owns them and asks
 * each active feature in turn:
 *   operateBeatEffect(manager, buffer, frames, preview), audio thread: runs
 *     the selected beat fx on the selected channel's block, in place.
 *   MasterInnards::onEv_BeatFxBeat(this, delta), ui thread: the BEAT arrows.
 *   MasterInnards::onEv_BeatFxTime(this, delta), ui thread: the TIME knob,
 *     in encoder steps before rbp's acceleration.
 *   SetBeatFxInfo(), ui thread: fills the beat fx block of the play screen
 *     (buf_PlayGroup +0x1e0) that the ui copies and draws.
 * each hook is installed only when an active feature listens to it.
 */

#ifndef RX3_BEATFX_BROKER_H
#define RX3_BEATFX_BROKER_H

#define OPERATE_BEAT_EFFECT ((unsigned long)0x0008a52c)
#define BEAT_FX_BEAT_EVENT ((unsigned long)0x0034553c)
#define BEAT_FX_TIME_EVENT ((unsigned long)0x00346a8c)
#define SET_BEAT_FX_INFO ((unsigned long)0x001309d4)
#define BEAT_FX_INFO_BLOCK ((uint8_t *)0x0216b5a0)

typedef void (*operate_beat_effect_fn)(void *, Float2 *, int, int);
typedef int (*beat_fx_beat_event_fn)(void *, long);
typedef void (*set_beat_fx_info_fn)(void);

/* stmdb sp!,{r3-r8,r10,lr} ; mov r6,r1 */
static const uint8_t operate_beat_effect_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x01, 0x60, 0xa0, 0xe1
};
/* ldrb r3,[r0,#0xe9] ; stmdb sp!,{r4-r8,lr} */
static const uint8_t beat_fx_beat_event_guard[8] = {
    0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9
};
/* ldrb r3,[r0,#0xe9] ; stmdb sp!,{r4-r8,lr}, like the BEAT arrows */
static const uint8_t beat_fx_time_event_guard[8] = {
    0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9
};
/* stmdb sp!,{r3-r8,r10,lr} ; mov r0,#0 */
static const uint8_t set_beat_fx_info_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x00, 0x00, 0xa0, 0xe3
};

static struct installed_hook operate_beat_effect_hook;
static struct installed_hook beat_fx_beat_event_hook;
static struct installed_hook beat_fx_time_event_hook;
static struct installed_hook set_beat_fx_info_hook;
static operate_beat_effect_fn original_operate_beat_effect;
static beat_fx_beat_event_fn original_beat_fx_beat_event;
static beat_fx_beat_event_fn original_beat_fx_time_event;
static set_beat_fx_info_fn original_set_beat_fx_info;

static void hooked_operate_beat_effect(void *manager, Float2 *buffer,
                                       int frames, int preview)
{
    unsigned int index;
    if (manager && buffer && frames > 0)
        for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
            if (runtime_features[index].active &&
                runtime_features[index].beat_fx_operate &&
                runtime_features[index].beat_fx_operate(manager, buffer,
                                                         frames))
                return;
    original_operate_beat_effect(manager, buffer, frames, preview);
}

static int hooked_beat_fx_beat_event(void *innards, long delta)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].beat_fx_beat &&
            runtime_features[index].beat_fx_beat(innards, delta))
            return 0;
    return original_beat_fx_beat_event(innards, delta);
}

static int hooked_beat_fx_time_event(void *innards, long delta)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].beat_fx_time &&
            runtime_features[index].beat_fx_time(innards, delta))
            return 0;
    return original_beat_fx_time_event(innards, delta);
}

static void hooked_set_beat_fx_info(void)
{
    unsigned int index;
    original_set_beat_fx_info();
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].beat_fx_info)
            runtime_features[index].beat_fx_info(BEAT_FX_INFO_BLOCK);
}

/* ui thread: the id a feature wants drawn in place of image_id. */
static uint32_t rx3_swap_image(uint32_t image_id)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            runtime_features[index].image_swap) {
            uint32_t swapped = runtime_features[index].image_swap(image_id);
            if (swapped != image_id)
                return swapped;
        }
    return image_id;
}

static int beat_fx_broker_wanted(size_t offset)
{
    unsigned int index;
    for (index = 0; index < RUNTIME_FEATURE_COUNT; index++)
        if (runtime_features[index].active &&
            *(void *const *)((const uint8_t *)&runtime_features[index] + offset))
            return 1;
    return 0;
}

/* a rejected guard leaves that part stock; the features stay installed and
   simply are not asked. */
static void install_beat_fx_broker(void)
{
    if (!original_operate_beat_effect &&
        beat_fx_broker_wanted(__builtin_offsetof(struct rx3_runtime_feature,
                                                 beat_fx_operate))) {
        original_operate_beat_effect = (operate_beat_effect_fn)install_hook(
            &operate_beat_effect_hook, OPERATE_BEAT_EFFECT,
            operate_beat_effect_guard, (void *)hooked_operate_beat_effect);
        if (!original_operate_beat_effect)
            log_line("warning: operateBeatEffect guard rejected");
    }
    if (!original_beat_fx_beat_event &&
        beat_fx_broker_wanted(__builtin_offsetof(struct rx3_runtime_feature,
                                                 beat_fx_beat))) {
        original_beat_fx_beat_event = (beat_fx_beat_event_fn)install_hook(
            &beat_fx_beat_event_hook, BEAT_FX_BEAT_EVENT,
            beat_fx_beat_event_guard, (void *)hooked_beat_fx_beat_event);
        if (!original_beat_fx_beat_event)
            log_line("warning: onEv_BeatFxBeat guard rejected");
    }
    if (!original_beat_fx_time_event &&
        beat_fx_broker_wanted(__builtin_offsetof(struct rx3_runtime_feature,
                                                 beat_fx_time))) {
        original_beat_fx_time_event = (beat_fx_beat_event_fn)install_hook(
            &beat_fx_time_event_hook, BEAT_FX_TIME_EVENT,
            beat_fx_time_event_guard, (void *)hooked_beat_fx_time_event);
        if (!original_beat_fx_time_event)
            log_line("warning: onEv_BeatFxTime guard rejected");
    }
    if (!original_set_beat_fx_info &&
        beat_fx_broker_wanted(__builtin_offsetof(struct rx3_runtime_feature,
                                                 beat_fx_info))) {
        original_set_beat_fx_info = (set_beat_fx_info_fn)install_hook(
            &set_beat_fx_info_hook, SET_BEAT_FX_INFO, set_beat_fx_info_guard,
            (void *)hooked_set_beat_fx_info);
        if (!original_set_beat_fx_info)
            log_line("warning: SetBeatFxInfo guard rejected");
    }
}

static void remove_beat_fx_broker(void)
{
    uninstall_hook(&set_beat_fx_info_hook);
    uninstall_hook(&beat_fx_time_event_hook);
    uninstall_hook(&beat_fx_beat_event_hook);
    uninstall_hook(&operate_beat_effect_hook);
    original_set_beat_fx_info = 0;
    original_beat_fx_time_event = 0;
    original_beat_fx_beat_event = 0;
    original_operate_beat_effect = 0;
}

#endif /* RX3_BEATFX_BROKER_H */

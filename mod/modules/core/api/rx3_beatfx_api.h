/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_BEATFX_API_H
#define RX3_BEATFX_API_H
#include "rx3_platform.h"
#include "rx3_audio_api.h"
/* The beat fx the player runs, when a module needs to own one.
 *
 * rbp runs the selected beat fx on the selected channel's block through
 * dsp::BeatEffectManager::operateBeatEffect, drives its BEAT arrows and TIME
 * knob through MasterInnards events, and fills the play screen's beat fx
 * block with SetBeatFxInfo. A module that replaces a stock effect needs all
 * four places, and a function can carry only one hook, so the core owns them
 * and asks the registered modules.
 *
 * Each hook is installed when its first client registers and removed with
 * its last. Callbacks run on the player's audio or ui threads: no
 * allocation, I/O or waiting. unregister_owner() drains admitted callbacks
 * before returning, so it must not be called from one.
 */

/* The manager, as the four hooks see it. The type is both selection and ON:
 * rbp's OFF sets the next type (+0x50) to 0 and ON restores the selected
 * one. +0x4c is the type stock has faded to, +0x88 its fade state (3 is a
 * tail ringing out), +0x00/+0x04 the channel it is settled on, +0x08 the
 * active effect (whose +0x24 is TIME in ms), +0xe8 DEPTH 0..1 as the dial
 * left it.
 *
 * A module that replaces a stock effect owns the block from ON through OFF:
 * while the faded-to type is its own and the next is its own or off, on a
 * settled channel. The tail is handed back to stock, so stock can finish
 * the effect it started before the module took over. */
#define RX3_BEATFX_FADE_TAIL 3
#define RX3_BEATFX_FADED_TYPE 0x4cu
#define RX3_BEATFX_NEXT_TYPE 0x50u
#define RX3_BEATFX_CHANNEL 0x00u
#define RX3_BEATFX_NEXT_CHANNEL 0x04u
#define RX3_BEATFX_EFFECT 0x08u
#define RX3_BEATFX_FADE_STATE 0x88u
#define RX3_BEATFX_DEPTH 0xe8u

static inline int rx3_beatfx_owned(const void *manager, int type)
{
    const volatile uint8_t *fields = (const volatile uint8_t *)manager;
    int next = *(const volatile int *)(fields + RX3_BEATFX_NEXT_TYPE);
    return *(const volatile int *)(fields + RX3_BEATFX_FADED_TYPE) == type &&
           (next == type || next == 0) &&
           *(const volatile int *)(fields + RX3_BEATFX_CHANNEL) ==
               *(const volatile int *)(fields + RX3_BEATFX_NEXT_CHANNEL) &&
           *(const volatile int *)(fields + RX3_BEATFX_FADE_STATE) !=
               RX3_BEATFX_FADE_TAIL;
}

static inline int rx3_beatfx_on(const void *manager, int type)
{
    const volatile uint8_t *fields = (const volatile uint8_t *)manager;
    return rx3_beatfx_owned(manager, type) &&
           *(const volatile int *)(fields + RX3_BEATFX_NEXT_TYPE) == type;
}

/* Audio thread: the selected effect's block, in place, before stock runs
   it. Return nonzero to skip the stock effect; the first nonzero ends the
   chain. Called for every selected type: filter with rx3_beatfx_owned(). */
typedef int (*rx3_beatfx_operate_fn)(void *manager, struct rx3_stereo *buffer,
                                     unsigned int frames);
/* ui thread: the BEAT arrows or the TIME knob, in encoder steps, before
   rbp's own acceleration. `innards` is the MasterInnards whose type at
   +0x3c (ui numbering) says which effect's row is shown. Return nonzero to
   consume: the player never sees the event. */
typedef int (*rx3_beatfx_control_fn)(void *innards, long delta);
/* ui thread, after rbp filled the play screen's beat fx block. Every
   listener sees the whole block; nobody consumes. The block's first word
   is the shown effect, in the display numbering. */
typedef void (*rx3_beatfx_info_fn)(uint8_t *block);

struct rx3_beatfx_service {
    /* Lower priority runs first. One registration per owner per stage, for
       the process lifetime: unregistering stops the callbacks but does not
       free the slot, like the input service's chains. */
    int (*claim_operate)(const void *owner, unsigned int priority,
                         rx3_beatfx_operate_fn);
    int (*claim_beat)(const void *owner, unsigned int priority,
                      rx3_beatfx_control_fn);
    int (*claim_time)(const void *owner, unsigned int priority,
                      rx3_beatfx_control_fn);
    /* One info listener per owner; all listeners are called. */
    int (*claim_info)(const void *owner, rx3_beatfx_info_fn);
    void (*unregister_owner)(const void *owner);
    /* The manager the last operate call ran on, for ui-thread queries.
       Zero before the player has run one. */
    void *(*manager)(void);
};
extern const struct rx3_beatfx_service rx3_beatfx;
#endif

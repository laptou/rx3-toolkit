/* SPDX-License-Identifier: MPL-2.0 */
/* Beat fx broker. Four native hook points, each installed when its first
 * client registers: the audio block the selected effect runs on, the BEAT
 * arrows, the TIME knob, and the play screen's beat fx block. A module
 * filters by the manager's type itself; the core only owns the hooks, the
 * order the modules are asked in, and the drain on the way out.
 */
#include "rx3_beatfx.h"
#include "rx3_hooks.h"
#include "rx3_log.h"

#define OPERATE_BEAT_EFFECT ((unsigned long)0x0008a52c)
#define BEAT_FX_BEAT_EVENT  ((unsigned long)0x0034553c)
#define BEAT_FX_TIME_EVENT  ((unsigned long)0x00346a8c)
#define SET_BEAT_FX_INFO    ((unsigned long)0x001309d4)
#define BEAT_FX_INFO_BLOCK  ((uint8_t *)0x0216b5a0)

/* stmdb sp!,{r3-r8,r10,lr} ; mov r6,r1 */
static const uint8_t operate_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x01, 0x60, 0xa0, 0xe1
};
/* ldrb r3,[r0,#0xe9] ; stmdb sp!,{r4-r8,lr} */
static const uint8_t beat_event_guard[8] = {
    0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9
};
/* like the BEAT arrows */
static const uint8_t time_event_guard[8] = {
    0xe9, 0x30, 0xd0, 0xe5, 0xf0, 0x41, 0x2d, 0xe9
};
/* stmdb sp!,{r3-r8,r10,lr} ; mov r0,#0 */
static const uint8_t set_info_guard[8] = {
    0xf8, 0x45, 0x2d, 0xe9, 0x00, 0x00, 0xa0, 0xe3
};

typedef void (*operate_beat_effect_fn)(void *, struct rx3_stereo *, int, int);
typedef int (*beat_fx_event_fn)(void *, long);
typedef void (*set_beat_fx_info_fn)(void);

#define CLIENT_LIMIT 4u

struct client {
    const void *owner;
    unsigned int priority;
    void *callback;
};
struct shared_hook {
    struct installed_hook hook;
    void *original;
    unsigned int active;
};

static struct client operate_clients[CLIENT_LIMIT], beat_clients[CLIENT_LIMIT],
                      time_clients[CLIENT_LIMIT];
static unsigned int operate_count, beat_count, time_count;
static struct client info_clients[CLIENT_LIMIT];
static unsigned int info_count;
static struct shared_hook operate_hook, beat_hook, time_hook, info_hook;
/* the manager operateBeatEffect last ran on, for the ui thread's query. */
static void *volatile beatfx_manager_seen;
static uint32_t beatfx_probe_last[10];

static void enter(struct shared_hook *hook)
{
    __atomic_add_fetch(&hook->active, 1u, __ATOMIC_SEQ_CST);
}
static void leave(struct shared_hook *hook)
{
    __atomic_sub_fetch(&hook->active, 1u, __ATOMIC_SEQ_CST);
}

static void drain(struct shared_hook *hook)
{
    for (;;) {
        while (__atomic_load_n(&hook->active, __ATOMIC_SEQ_CST)) usleep(1000u);
        usleep(10000u);
        if (!__atomic_load_n(&hook->active, __ATOMIC_SEQ_CST)) return;
    }
}

/* A callback cleared before this returns can no longer be entered: every
   path that reaches it increments the counter first. */
static void relinquish(struct shared_hook *hook)
{
    if (!hook->original) return;
    int detached = detach_hook(&hook->hook);
    drain(hook);
    if (!detached) {
        log_line("beat fx: native hook retained after restore failure");
        return;
    }
    (void)release_hook(&hook->hook);
    hook->original = 0;
}

static int acquire(struct shared_hook *hook, unsigned long address,
                   const uint8_t guard[8], void *replacement)
{
    if (hook->original) return 1;
    hook->original = install_hook(&hook->hook, address, guard, replacement);
    return hook->original != 0;
}

/* room for one more owner in this chain: validated before the hook is
   installed, so a refused guard never leaves a dead client behind. */
static int chain_room(const struct client *clients, unsigned int count,
                      const void *owner, const void *callback)
{
    if (!owner || !callback || count >= CLIENT_LIMIT) return 0;
    for (unsigned int i = 0; i < count; i++)
        if (clients[i].owner == owner) return 0;
    return 1;
}

/* insert by priority, like the input service's pad chain. */
static void insert(struct client *clients, unsigned int *count, const void *owner,
                   unsigned int priority, void *callback)
{
    unsigned int at = *count;
    while (at && clients[at - 1u].priority > priority) {
        clients[at] = clients[at - 1u];
        at--;
    }
    clients[at].owner = owner;
    clients[at].priority = priority;
    clients[at].callback = callback;
    (*count)++;
}

static void hooked_operate_beat_effect(void *manager, struct rx3_stereo *buffer,
                                       int frames, int preview)
{
    enter(&operate_hook);
    beatfx_manager_seen = manager;
    int skipped = 0;
    if (manager && buffer && frames > 0)
        for (unsigned int i = 0; i < operate_count && !skipped; i++) {
            rx3_beatfx_operate_fn callback =
                (rx3_beatfx_operate_fn)__atomic_load_n(&operate_clients[i].callback,
                                                       __ATOMIC_SEQ_CST);
            if (callback)
                skipped = callback(manager, buffer, (unsigned int)frames);
        }
    if (!skipped)
        ((operate_beat_effect_fn)operate_hook.original)(manager, buffer, frames,
                                                        preview);
    leave(&operate_hook);
}

static int hooked_beat_event(void *innards, long delta)
{
    enter(&beat_hook);
    int consumed = 0;
    for (unsigned int i = 0; i < beat_count && !consumed; i++) {
        rx3_beatfx_control_fn callback =
            (rx3_beatfx_control_fn)__atomic_load_n(&beat_clients[i].callback,
                                                    __ATOMIC_SEQ_CST);
        if (callback)
            consumed = callback(innards, delta);
    }
    int result = consumed ? 0 : ((beat_fx_event_fn)beat_hook.original)(innards, delta);
    leave(&beat_hook);
    return result;
}

static int hooked_time_event(void *innards, long delta)
{
    enter(&time_hook);
    int consumed = 0;
    for (unsigned int i = 0; i < time_count && !consumed; i++) {
        rx3_beatfx_control_fn callback =
            (rx3_beatfx_control_fn)__atomic_load_n(&time_clients[i].callback,
                                                    __ATOMIC_SEQ_CST);
        if (callback)
            consumed = callback(innards, delta);
    }
    int result = consumed ? 0 : ((beat_fx_event_fn)time_hook.original)(innards, delta);
    leave(&time_hook);
    return result;
}

static size_t probe_hex(char *out, const char *label, uint32_t value)
{
    size_t n = 0;
    int shift;
    int started = 0;
    while (label[n]) {
        out[n] = label[n];
        n++;
    }
    for (shift = 28; shift >= 0; shift -= 4) {
        unsigned digit = (value >> shift) & 0xfu;
        if (digit || shift == 0)
            started = 1;
        if (started)
            out[n++] = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u);
    }
    return n;
}

/* The manager's channel, effect, type and fade fields and the block the ui
   draws, logged when any of them changes, so a module that misreads the
   manager shows up in the core log. */
static void probe(void)
{
    static const char *const labels[10] = {
        "beatfx: ch=", " next_ch=", " effect=", " type=", " next_type=",
        " fade=", " done=", " effect_on=", " display=", " label=",
    };
    const volatile uint8_t *manager = (const volatile uint8_t *)beatfx_manager_seen;
    const volatile uint8_t *effect;
    uint32_t now[10];
    char line[192];
    size_t n = 0;
    unsigned index;
    if (!manager)
        return;
    effect = *(const volatile uint8_t *const *)(manager + RX3_BEATFX_EFFECT);
    now[0] = *(const volatile uint32_t *)(manager + RX3_BEATFX_CHANNEL);
    now[1] = *(const volatile uint32_t *)(manager + RX3_BEATFX_NEXT_CHANNEL);
    now[2] = (uint32_t)(unsigned long)effect;
    now[3] = *(const volatile uint32_t *)(manager + RX3_BEATFX_FADED_TYPE);
    now[4] = *(const volatile uint32_t *)(manager + RX3_BEATFX_NEXT_TYPE);
    now[5] = *(const volatile uint32_t *)(manager + RX3_BEATFX_FADE_STATE);
    now[6] = *(const volatile uint8_t *)(manager + 0xefu);
    now[7] = effect ? *(const volatile uint8_t *)(effect + 0x3cu) : 0xffu;
    now[8] = *(const uint32_t *)BEAT_FX_INFO_BLOCK;
    now[9] = *(const uint32_t *)(BEAT_FX_INFO_BLOCK + 0x10u);
    for (index = 0; index < 10u && now[index] == beatfx_probe_last[index];
         index++)
        ;
    if (index == 10u)
        return;
    for (index = 0; index < 10u; index++) {
        beatfx_probe_last[index] = now[index];
        n += probe_hex(line + n, labels[index], now[index]);
    }
    line[n] = '\0';
    log_line(line);
}

static void hooked_set_beat_fx_info(void)
{
    enter(&info_hook);
    ((set_beat_fx_info_fn)info_hook.original)();
    for (unsigned int i = 0; i < info_count; i++) {
        rx3_beatfx_info_fn callback =
            (rx3_beatfx_info_fn)__atomic_load_n(&info_clients[i].callback,
                                                __ATOMIC_SEQ_CST);
        if (callback)
            callback(BEAT_FX_INFO_BLOCK);
    }
    probe();
    leave(&info_hook);
}

static int claim_operate(const void *owner, unsigned int priority,
                         rx3_beatfx_operate_fn callback)
{
    if (!chain_room(operate_clients, operate_count, owner, (const void *)callback))
        return 0;
    if (!acquire(&operate_hook, OPERATE_BEAT_EFFECT, operate_guard,
                 (void *)hooked_operate_beat_effect))
        return 0;
    insert(operate_clients, &operate_count, owner, priority, (void *)callback);
    return 1;
}

static int claim_beat(const void *owner, unsigned int priority,
                      rx3_beatfx_control_fn callback)
{
    if (!chain_room(beat_clients, beat_count, owner, (const void *)callback))
        return 0;
    if (!acquire(&beat_hook, BEAT_FX_BEAT_EVENT, beat_event_guard,
                 (void *)hooked_beat_event))
        return 0;
    insert(beat_clients, &beat_count, owner, priority, (void *)callback);
    return 1;
}

static int claim_time(const void *owner, unsigned int priority,
                      rx3_beatfx_control_fn callback)
{
    if (!chain_room(time_clients, time_count, owner, (const void *)callback))
        return 0;
    if (!acquire(&time_hook, BEAT_FX_TIME_EVENT, time_event_guard,
                 (void *)hooked_time_event))
        return 0;
    insert(time_clients, &time_count, owner, priority, (void *)callback);
    return 1;
}

static int claim_info(const void *owner, rx3_beatfx_info_fn callback)
{
    if (!owner || !callback || info_count >= CLIENT_LIMIT) return 0;
    for (unsigned int i = 0; i < info_count; i++)
        if (info_clients[i].owner == owner) return 0;
    if (!acquire(&info_hook, SET_BEAT_FX_INFO, set_info_guard,
                 (void *)hooked_set_beat_fx_info))
        return 0;
    info_clients[info_count].owner = owner;
    info_clients[info_count].priority = 0;
    info_clients[info_count].callback = (void *)callback;
    __atomic_store_n(&info_count, info_count + 1u, __ATOMIC_SEQ_CST);
    return 1;
}

static void unregister_owner(const void *owner)
{
    if (!owner) return;
    unsigned int operate = 0, beat = 0, knob = 0, info = 0;
    for (unsigned int i = 0; i < operate_count; i++) {
        if (operate_clients[i].owner == owner)
            __atomic_store_n(&operate_clients[i].callback, 0, __ATOMIC_SEQ_CST);
        if (operate_clients[i].callback) operate++;
    }
    for (unsigned int i = 0; i < beat_count; i++) {
        if (beat_clients[i].owner == owner)
            __atomic_store_n(&beat_clients[i].callback, 0, __ATOMIC_SEQ_CST);
        if (beat_clients[i].callback) beat++;
    }
    for (unsigned int i = 0; i < time_count; i++) {
        if (time_clients[i].owner == owner)
            __atomic_store_n(&time_clients[i].callback, 0, __ATOMIC_SEQ_CST);
        if (time_clients[i].callback) knob++;
    }
    for (unsigned int i = 0; i < info_count; i++) {
        if (info_clients[i].owner == owner)
            __atomic_store_n(&info_clients[i].callback, 0, __ATOMIC_SEQ_CST);
        if (info_clients[i].callback) info++;
    }
    /* Callbacks the owner left behind can still be running; wait them out
       before its code or state may be released. */
    drain(&operate_hook); drain(&beat_hook); drain(&time_hook); drain(&info_hook);
    if (!operate) { relinquish(&operate_hook); if (!operate_hook.original) operate_count = 0; }
    if (!beat) { relinquish(&beat_hook); if (!beat_hook.original) beat_count = 0; }
    if (!knob) { relinquish(&time_hook); if (!time_hook.original) time_count = 0; }
    if (!info) { relinquish(&info_hook); if (!info_hook.original) info_count = 0; }
}

static void *manager_now(void)
{
    return beatfx_manager_seen;
}

unsigned int rx3_beatfx_count(void)
{
    return operate_count + beat_count + time_count + info_count;
}

const struct rx3_beatfx_service rx3_beatfx = {
    claim_operate, claim_beat, claim_time, claim_info,
    unregister_owner, manager_now
};

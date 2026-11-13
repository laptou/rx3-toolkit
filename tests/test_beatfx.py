# SPDX-License-Identifier: MPL-2.0
"""Beat fx service: the claim table and the ownership rule.

The four native hooks live at firmware addresses, so dispatch is not
exercised here; what a module depends on, and what the host can check, is
the chain bookkeeping (refuse a duplicate owner, keep the limit, clear on
unregister) and the manager-layout rule every consumer filters by.
"""
import unittest

from tests import test_framework

HARNESS = r'''
#include "core/api/rx3_hook_types.h"
/* doubles: the native hooks install against nothing, so every claim is
   granted and the chain bookkeeping can be exercised. */
static char fake_original;
void *install_hook(struct installed_hook *h, unsigned long a,
                   const uint8_t g[8], void *r)
{ (void)h;(void)a;(void)g;(void)r; return &fake_original; }
int uninstall_hook(struct installed_hook *h) { (void)h; return 1; }
int detach_hook(struct installed_hook *h) { (void)h; return 1; }
int release_hook(struct installed_hook *h) { (void)h; return 1; }
#include "core/services/rx3_beatfx.c"
static const char owner_a, owner_b, owner_c;
static int operate_a(void *m, struct rx3_stereo *b, unsigned int f) { (void)m;(void)b;(void)f; return 1; }
static int control_a(void *i, long d) { (void)i;(void)d; return 1; }
static void info_a(uint8_t *block) { (void)block; }
/* A manager laid out the way the api header documents it. */
static uint8_t manager[0x100];
static void manager_set(int channel, int faded, int next, int fade) {
    memset(manager, 0, sizeof(manager));
    *(volatile int *)(manager + RX3_BEATFX_CHANNEL) = channel;
    *(volatile int *)(manager + RX3_BEATFX_NEXT_CHANNEL) = channel;
    *(volatile int *)(manager + RX3_BEATFX_FADED_TYPE) = faded;
    *(volatile int *)(manager + RX3_BEATFX_NEXT_TYPE) = next;
    *(volatile int *)(manager + RX3_BEATFX_FADE_STATE) = fade;
}
'''


class BeatfxTests(unittest.TestCase):
    run_units = test_framework.FrameworkTests.run_units

    def run_body(self, body):
        self.run_units(
            HARNESS + '\nint main(void){\n' + body + '\nreturn 0;}\n',
            [], flags=('-Wno-unused-function', '-Wno-unused-const-variable'))

    def test_claims_refuse_duplicates_and_missing_pieces(self):
        self.run_body(r'''
    assert(!rx3_beatfx.claim_operate(0, 10u, operate_a));
    assert(!rx3_beatfx.claim_operate(&owner_a, 10u, 0));
    assert(rx3_beatfx.claim_operate(&owner_a, 10u, operate_a));
    /* the same owner twice is refused, whatever the priority */
    assert(!rx3_beatfx.claim_operate(&owner_a, 1u, operate_a));
    assert(rx3_beatfx.claim_operate(&owner_b, 20u, operate_a));
    assert(rx3_beatfx.claim_beat(&owner_a, 10u, control_a));
    assert(!rx3_beatfx.claim_beat(&owner_a, 10u, control_a));
    assert(rx3_beatfx.claim_time(&owner_a, 10u, control_a));
    assert(rx3_beatfx.claim_info(&owner_a, info_a));
    assert(!rx3_beatfx.claim_info(&owner_a, info_a));
    rx3_beatfx.unregister_owner(&owner_a);
    /* Registration is once per process, like the input service's chains: the
       cleared owner's slot stays reserved, and a fresh owner is admitted. */
    assert(!rx3_beatfx.claim_operate(&owner_a, 10u, operate_a));
    assert(rx3_beatfx.claim_operate(&owner_c, 10u, operate_a));
    rx3_beatfx.unregister_owner(&owner_c);
    rx3_beatfx.unregister_owner(0);
''')

    def test_the_manager_query_starts_empty(self):
        self.run_body(r'''
    assert(rx3_beatfx.manager() == 0);
''')

    def test_ownership_follows_the_manager_layout(self):
        self.run_body(r'''
    /* ON: faded-to and next both helix, channel settled, no tail. */
    manager_set(0, 14, 14, 0);
    assert(rx3_beatfx_owned(manager, 14));
    assert(rx3_beatfx_on(manager, 14));
    /* OFF: next is 0, the module still owns the block. */
    manager_set(0, 14, 0, 0);
    assert(rx3_beatfx_owned(manager, 14));
    assert(!rx3_beatfx_on(manager, 14));
    /* a fade tail belongs to stock, whatever the types say. */
    manager_set(0, 14, 14, RX3_BEATFX_FADE_TAIL);
    assert(!rx3_beatfx_owned(manager, 14));
    /* an unsettled channel is not owned. */
    manager_set(0, 14, 14, 0);
    *(volatile int *)(manager + RX3_BEATFX_NEXT_CHANNEL) = 1;
    assert(!rx3_beatfx_owned(manager, 14));
    /* another effect's block is never owned. */
    manager_set(0, 2, 2, 0);
    assert(!rx3_beatfx_owned(manager, 14));
''')

    def test_the_client_limit_holds(self):
        self.run_body(r'''
    static const char owners[5];
    for (unsigned int i = 0; i < 4; i++)
        assert(rx3_beatfx.claim_operate(&owners[i], 10u, operate_a));
    /* the fifth is refused and nobody is replaced. */
    assert(!rx3_beatfx.claim_operate(&owners[4], 10u, operate_a));
    for (unsigned int i = 0; i < 4; i++)
        rx3_beatfx.unregister_owner(&owners[i]);
    assert(rx3_beatfx.claim_operate(&owners[4], 10u, operate_a));
''')

    def test_priority_orders_the_chain(self):
        self.run_body(r'''
    /* claim the same stage from three owners with distinct priorities; the
       table must read low priority first, like the pad chain. */
    assert(rx3_beatfx.claim_operate(&owner_a, 30u, operate_a));
    assert(rx3_beatfx.claim_operate(&owner_b, 10u, operate_a));
    assert(rx3_beatfx.claim_operate(&owner_c, 20u, operate_a));
    rx3_beatfx.unregister_owner(&owner_b);
    rx3_beatfx.unregister_owner(&owner_c);
    rx3_beatfx.unregister_owner(&owner_a);
''')


if __name__ == "__main__":
    unittest.main()

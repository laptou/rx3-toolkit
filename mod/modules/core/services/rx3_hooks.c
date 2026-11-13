/* SPDX-License-Identifier: MPL-2.0 */
#include "../api/rx3_platform.h"
#include "rx3_hooks.h"
#include "../firmware/rx3_patch.h"

struct hook_record {
    unsigned long address;
    uint8_t original[8];
    void *trampoline;
    struct installed_hook *owner;
    int detached;
};
#define RX3_HOOK_LIMIT 48u
static struct hook_record records[RX3_HOOK_LIMIT];

/*
 * Copy the first eight bytes into a trampoline and append an absolute jump to
 * address+8. These stolen instructions have no PC-relative dependency:
 *   getStreamAt : ldrb r12,[r0,#0x9c] ; stmdb sp!,{r4..r8,r10,lr}
 *   load        : stmdb sp!,{r4..r11,lr} ; sub sp,sp,#0x5c
 *   onKey_Pad   : ldrh r3,[r1,#8] ; stmdb sp!,{r4..r11,lr}
 */
static void *install_raw_hook(struct hook_record *hook, unsigned long address,
                          const uint8_t guard[8], void *replacement)
{
    if (memcmp((const void *)address, guard, 8))
        return 0;

    uint32_t *trampoline = mmap(0, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;
    memcpy(trampoline, (const void *)address, 8);
    /* Relocate an ARM literal load in either stolen instruction. Browse's
       native scroll setup uses ADD followed by LDR r2,[pc,#imm12]. */
    for (unsigned int i=0;i<2;i++) {
        uint32_t op=trampoline[i];
        if ((op & 0xffff0000u)==0xe59f0000u && ((op>>12)&15u)!=15u) {
            trampoline[4+i]=*(const uint32_t *)(address+i*4u+8u+(op&0xfffu));
            trampoline[i]=(op&0xfffff000u)|8u;
        }
    }
    trampoline[2] = 0xe51ff004;                      /* ldr pc,[pc,#-4] */
    trampoline[3] = (uint32_t)(address + 8);
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 24u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        munmap(trampoline, 4096);
        return 0;
    }

    uint32_t patch[2] = {0xe51ff004, (uint32_t)(unsigned long)replacement};
    if (write_code(address, patch, sizeof(patch))) {
        munmap(trampoline, 4096);
        return 0;
    }

    hook->address = address;
    memcpy(hook->original, guard, sizeof(hook->original));
    hook->trampoline = trampoline;
    return trampoline;
}


/* Variant for ldr rd,[pc,#imm12] followed by one more instruction. The
   trampoline reloads rd from a copied literal, replays that instruction,
   and joins address+8, so the literal does not depend on where this object
   is mapped. rd is whichever register the prologue loads, never pc. */
static void *install_raw_pc_ldr_hook(struct hook_record *hook,
                                 unsigned long address,
                                 const uint8_t guard[8], void *replacement)
{
    if (memcmp((const void *)address, guard, 8))
        return 0;

    uint32_t instruction = *(const uint32_t *)address;
    unsigned rd = (instruction >> 12) & 15u;
    /* ldr rd,[pc,#imm12] with the always condition, any register but pc. */
    if ((instruction & 0xffff0000u) != 0xe59f0000u || rd == 15u)
        return 0;
    unsigned long literal_address = address + 8u + (instruction & 0xfffu);
    uint32_t literal_value = *(const uint32_t *)literal_address;

    uint32_t *trampoline = mmap(0, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;
    trampoline[0] = 0xe59f0008u | (rd << 12);        /* ldr rd,[pc,#8] */
    trampoline[1] = *(const uint32_t *)(address + 4u); /* the next instruction */
    trampoline[2] = 0xe51ff004u;                    /* ldr pc,[pc,#-4] */
    trampoline[3] = (uint32_t)(address + 8u);
    trampoline[4] = literal_value;
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 20u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        munmap(trampoline, 4096);
        return 0;
    }

    uint32_t patch[2] = {0xe51ff004u, (uint32_t)(unsigned long)replacement};
    if (write_code(address, patch, sizeof(patch))) {
        munmap(trampoline, 4096);
        return 0;
    }

    hook->address = address;
    memcpy(hook->original, guard, sizeof(hook->original));
    hook->trampoline = trampoline;
    return trampoline;
}


/* Return-tail detours. Some rbp functions end a frame by returning straight
   out of it: `ldmia sp!,{r4-r11,pc}` pops the callee-saved registers and the
   return address in one instruction, so there is no prologue to steal and
   nothing left to jump back into. rbp's waveform pass is one of those, and a
   module that paints its own pixels needs a turn after that pop.

   The detour covers the two instructions that end the frame -- whatever runs
   last, then the tail load -- and the trampoline replays both with the load's
   `pc` rewritten to `lr`, then runs the module and returns to the original
   caller. Eight bytes in, so the patch needs no branch range. */
/* The two instructions are given as words, not bytes: the guard here is a
   pair of decoded instructions, so there is no byte order to get wrong. */
static void *install_raw_return_hook(struct hook_record *hook, unsigned long address,
                                     uint32_t before, uint32_t tail, void *after)
{
    if (*(const uint32_t *)address != before)
        return 0;
    if (*(const uint32_t *)(address + 4u) != tail)
        return 0;
    /* ldmia sp!,{...,pc}, with lr not in the list: lr would be popped as a
       data register and there would be no return address left to keep. */
    if ((tail & 0xffff0000u) != 0xe8bd0000u) return 0;
    if (!(tail & 0x8000u) || (tail & 0x4000u)) return 0;
    /* The replayed instruction carries no PC-relative dependency. */
    if ((before & 0xffff0000u) == 0xe59f0000u) return 0;
    if ((before & 0x0f000000u) == 0x0a000000u) return 0;
    if ((before & 0x0ffffff0u) == 0x012fff10u) return 0;

    uint32_t *trampoline = mmap(0, 4096, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED)
        return 0;
    trampoline[0] = before;                          /* last instruction   */
    trampoline[1] = (tail & ~0x8000u) | 0x4000u;     /* pc -> lr           */
    trampoline[2] = 0xe24dd008u;                     /* sub sp,sp,#8       */
    trampoline[3] = 0xe58de000u;                     /* str lr,[sp]        */
    trampoline[4] = 0xe59fc00cu;                     /* ldr r12,[pc,#12]   */
    trampoline[5] = 0xe12fff3cu;                     /* blx r12            */
    trampoline[6] = 0xe59dc000u;                     /* ldr r12,[sp]       */
    trampoline[7] = 0xe28dd008u;                     /* add sp,sp,#8       */
    trampoline[8] = 0xe12fff1cu;                     /* bx r12             */
    trampoline[9] = (uint32_t)(unsigned long)after;
    clear_instruction_cache((unsigned long)trampoline,
                            (unsigned long)trampoline + 40u);
    if (mprotect(trampoline, 4096, PROT_READ | PROT_EXEC)) {
        munmap(trampoline, 4096);
        return 0;
    }

    uint32_t patch[2] = {0xe51ff004u, (uint32_t)(unsigned long)trampoline};
    if (write_code(address, patch, sizeof(patch))) {
        munmap(trampoline, 4096);
        return 0;
    }

    hook->address = address;
    memcpy(hook->original, (const void *)address, sizeof(hook->original));
    hook->trampoline = trampoline;
    return trampoline;
}

/* A single pool owns every detour, even across separately compiled modules.
 * A detached address stays reserved until callbacks have drained and its
 * trampoline has been released. No implicit hook chaining is allowed.
 */
#define RX3_HOOK_PROLOGUE 0
#define RX3_HOOK_PC_LDR 1
/* One hook per handle, and no two hooks within one instruction of each other:
   the second would patch bytes the first already owns. */
static struct hook_record *claim_slot(struct installed_hook *handle, unsigned long address)
{
    if (!handle || handle->record || !address) return 0;
    struct hook_record *slot = 0;
    for (unsigned int i = 0; i < RX3_HOOK_LIMIT; i++) {
        if (!records[i].owner) { if (!slot) slot = &records[i]; continue; }
        unsigned long other = records[i].address;
        if ((address >= other && address - other < 8u) ||
            (other > address && other - address < 8u)) return 0;
    }
    return slot;
}
static void *install_owned_hook(struct installed_hook *handle, unsigned long address,
                                const uint8_t guard[8], void *replacement, int mode)
{
    if (!guard || !replacement) return 0;
    struct hook_record *slot = claim_slot(handle, address);
    if (!slot) return 0;
    void *original = mode == RX3_HOOK_PC_LDR
                         ? install_raw_pc_ldr_hook(slot, address, guard, replacement)
                         : install_raw_hook(slot, address, guard, replacement);
    if (!original) return 0;
    slot->owner = handle;
    handle->record = slot;
    return original;
}
void *install_hook(struct installed_hook *handle, unsigned long address,
                    const uint8_t guard[8], void *replacement)
{
    return install_owned_hook(handle, address, guard, replacement,
                              RX3_HOOK_PROLOGUE);
}
void *install_pc_ldr_hook(struct installed_hook *handle, unsigned long address,
                           const uint8_t guard[8], void *replacement)
{
    return install_owned_hook(handle, address, guard, replacement,
                              RX3_HOOK_PC_LDR);
}
void *install_return_hook(struct installed_hook *handle, unsigned long address,
                           uint32_t before, uint32_t tail, void *after)
{
    if (!after) return 0;
    struct hook_record *slot = claim_slot(handle, address);
    if (!slot) return 0;
    void *original = install_raw_return_hook(slot, address, before, tail, after);
    if (!original) return 0;
    slot->owner = handle;
    handle->record = slot;
    return original;
}
int hook_is_installed(const struct installed_hook *handle)
{
    return handle && handle->record && handle->record->owner == handle;
}
int detach_hook(struct installed_hook *handle)
{
    if (!handle || !handle->record) return 1;
    struct hook_record *record = handle->record;
    if (record->owner != handle) return 0;
    if (record->detached) return 1;
    if (write_code(record->address, record->original, sizeof(record->original))) return 0;
    record->detached = 1;
    return 1;
}
int release_hook(struct installed_hook *handle)
{
    if (!handle || !handle->record) return 1;
    struct hook_record *record = handle->record;
    if (record->owner != handle || !record->detached) return 0;
    if (record->trampoline && munmap(record->trampoline, 4096)) return 0;
    memset(record, 0, sizeof(*record));
    handle->record = 0;
    return 1;
}
int uninstall_hook(struct installed_hook *handle)
{
    return detach_hook(handle) && release_hook(handle);
}

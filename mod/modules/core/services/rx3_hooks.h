/* SPDX-License-Identifier: MPL-2.0 */
#ifndef RX3_HOOKS_H
#define RX3_HOOKS_H
#include "../api/rx3_platform.h"
/* Zero initialize. The framework owns the record and trampoline.
 * Installation/removal is serialized by the startup/shutdown thread.
 * No hot unload: callers must quiesce replacement callbacks before release.
 */
#include "../api/rx3_hook_types.h"
int hook_is_installed(const struct installed_hook *);
int detach_hook(struct installed_hook *);
int release_hook(struct installed_hook *);
void *install_hook(struct installed_hook *, unsigned long, const uint8_t[8], void *);
void *install_pc_ldr_hook(struct installed_hook *, unsigned long, const uint8_t[8], void *);
/* Detour at the frame's own return, for a function that ends a frame by
   popping straight out of it (`ldmia sp!,{...,pc}`). `address` carries the
   instruction before that load and the load itself; `after` runs with the
   frame already popped, before control returns to the caller. Returns the
   trampoline, or 0 when the guard does not match or the tail is not one. */
void *install_return_hook(struct installed_hook *, unsigned long, const uint8_t[8], void *);
int uninstall_hook(struct installed_hook *);
#endif

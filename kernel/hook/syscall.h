#pragma once

#include <asm/syscall.h>

/*
 * Kernel-side syscall-table edits.  Two things:
 *
 *   syscall_table_resolve()  find sys_call_table
 *   syscall_slot_hook()      replace one slot, handing back the old handler
 *   syscall_slot_unhook()    put one replaced slot back
 *
 * Each caller owns the slots it hooks and restores them itself; there is no
 * group restore.  The temporary boot watcher removes its redirects (and asks
 * init_rc.c to remove its read/fstat proxies) explicitly when it is dropped at
 * the zygote stage -- see boot/hook_init.c.
 */
extern syscall_fn_t *syscall_table;

int syscall_table_resolve(void);

int syscall_slot_hook(int nr, syscall_fn_t fn, syscall_fn_t *previous, const char *tag);

/* Put a single previously hooked slot back and drop its record. */
int syscall_slot_unhook(int nr);

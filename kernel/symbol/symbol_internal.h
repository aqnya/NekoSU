/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_SYMBOL_INTERNAL_H
#define NKSU_SYMBOL_INTERNAL_H

#include <linux/types.h>

/*
 * Internal interfaces between the symbol resolver's cache, the /proc parser,
 * the in-memory kallsyms scanner and the compressed-table decoder.  The
 * caller holds ksym_lock for every function marked below.
 */

/* symbol.c: add to the name -> address cache while holding ksym_lock. */
void nksu_ksym_cache_add_locked(const char *name, unsigned long addr);

/* symbol_proc.c: try /proc/kallsyms before falling back to a memory scan. */
unsigned long nksu_ksym_lookup_proc(const char *name);
void nksu_ksym_proc_reset(void);

/* symbol_scan.c: scan the kernel image once while holding ksym_lock. */
void nksu_ksym_scan_memory(void);

/* symbol_kallsyms.c: probe and consume one kallsyms table candidate. */
u32 nksu_kallsyms_token_table_len(const u8 *start, const u8 *end);
int nksu_kallsyms_consume(const u8 *buf, unsigned long len, const u8 *token_table);

#endif /* NKSU_SYMBOL_INTERNAL_H */

// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Compressed kallsyms table decoding.
 *
 * kallsyms memory layout (kernel/kallsyms.c):
 *
 *   kallsyms_offsets[]  or kallsyms_addresses[]   // num_syms entries
 *   kallsyms_relative_base                       // BASE_RELATIVE only
 *   kallsyms_names[]     [len][token ...]; a 0x80 length bit adds one byte:
 *                        len = (first & 0x7f) | (second << 7)
 *   kallsyms_markers[]   one u32 offset per 256 symbols
 *   kallsyms_token_table[]  256 NUL-terminated tokens (1-2 bytes each)
 *   kallsyms_token_index[]  256 u16 offsets into token_table
 *
 * token_table has a very distinctive shape (exactly 256 short tokens), so we
 * locate it first, then walk backwards to markers/names, then decode all
 * symbol names along names.
 *
 * Address decoding (see kallsyms_sym_address):
 *   - BASE_RELATIVE, !ABSOLUTE_PERCPU: addr = base + (u32)offsets[i]
 *   - BASE_RELATIVE,  ABSOLUTE_PERCPU: offsets[i] >= 0 -> offsets[i]
 *                                     offsets[i] <  0 -> base - 1 - offsets[i]
 *   - absolute array: addresses[i]
 *
 * To avoid depending on kernel global symbols, the address array and
 * relative_base are recovered from the fixed .rodata layout emitted by
 * scripts/kallsyms.c (offsets[] .. relative_base .. num_syms .. names[]):
 * relative_base is the u64 at names-16, num_syms the u32 at names-8. No
 * kernel global is referenced.
 */

#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/errno.h>

#include "symbol/symbol_internal.h"

/* Decoded-name buffer bound (KSYM_NAME_LEN is at most 512 on supported kernels). */
#define KSYM_NAME_MAX 512
#define KSYM_SYMBOL_MAX (KSYM_NAME_MAX + 2) /* name plus extended length */
#define KSYM_MARKER_SPAN_MAX (256u * KSYM_SYMBOL_MAX)
#define KSYM_MARKERS_MAX 8192 /* at most 8192 markers */

/*
 * Symbol name decoding (plain memory access on the bounce buffer).
 */

/*
 * Decode one symbol from names[..names_end), advancing *offset. An extended
 * (0x80) length uses a second byte; the leading type character is dropped.
 */
static int decode_symbol(const u8 *names, const u8 *names_end, const u16 *token_index,
			 const u8 *token_table, u32 *offset, char *out, int outsz)
{
	u32 pos = *offset;
	u32 names_len = (u32)(names_end - names);
	u32 len;
	char *dst = out;
	char *limit = out + outsz - 1;
	bool first = true;

	if (pos >= names_len)
		return -1;

	len = names[pos++];
	if (len & 0x80) {
		if (pos >= names_len)
			return -1;
		len = (len & 0x7f) | ((u32)names[pos++] << 7);
	}
	if (len >= KSYM_NAME_MAX || pos + len > names_len)
		return -1;

	while (len--) {
		u16 idx = token_index[names[pos++]];
		const char *tok = (const char *)token_table + idx;

		while (*tok) {
			if (first)
				first = false; /* drop the symbol-type character */
			else if (dst < limit)
				*dst++ = *tok;
			else
				return -1;
			tok++;
		}
	}

	*dst = '\0';
	*offset = pos;
	return 0;
}

/* Strip compiler suffixes: foo.cfi_jt / foo$xxx / foo.llvm.xxx -> foo */
static void strip_symbol_suffix(char *name)
{
	char *cut = NULL;
	char *p;

	if ((p = strchr(name, '$')))
		cut = p;
	if ((p = strstr(name, ".llvm.")))
		if (!cut || p < cut)
			cut = p;
	if ((p = strstr(name, ".cfi_jt")))
		if (!cut || p < cut)
			cut = p;
	if ((p = strstr(name, ".constprop")))
		if (!cut || p < cut)
			cut = p;

	if (cut)
		*cut = '\0';
}

/*
 * Kallsyms view location (all done on the bounce buffer).
 */

struct kallsyms_view {
	const u8 *token_table;
	u32 tlen;
	const u16 *token_index; /* 256 entries */
	const u32 *markers; /* one offset per 256 symbols */
	u32 marker_count;
	const u8 *names; /* start of kallsyms_names */
	const u8 *names_end; /* == markers */
};

/*
 * Return the token_table size if p is a valid token_table, else 0.
 *
 * A token_table is exactly 256 non-empty NUL-terminated tokens immediately
 * followed (after 2-byte alignment) by a u16 index array whose i-th entry is
 * the byte offset of the i-th token. Requiring that exact match makes false
 * positives in unrelated rodata essentially impossible, and does not assume
 * anything about individual token length.
 *
 * A cheap prefilter (enough NULs in the first 64 bytes) keeps the scan fast in
 * code/rodata regions with long NUL-free runs.
 */
u32 nksu_kallsyms_token_table_len(const u8 *p, const u8 *end)
{
	const u8 *q = p;
	u32 offsets[256];
	u32 tlen;
	const u16 *ti;
	int i;

	if (end - p < 16)
		return 0;

	/*
     * Quick reject: kallsyms_token_table is a dense run of NUL-terminated
     * tokens, so its leading bytes contain many NULs (~15 in the first 64 for
     * the 5.15 vermeer table).  The window must tolerate a long first token:
     * though the tokens are frequency-sorted, the most frequent one can still
     * be several bytes -- the vermeer kernel starts with "_global", which
     * leaves only 3 NULs in the first 16 bytes, so the previous "4 NULs in 16
     * bytes" test rejected the real table outright and every symbol lookup
     * then failed (cache=0).  Count over 64 bytes instead; ordinary
     * code/rodata rarely holds 8 NULs there.
     */
	{
		const u8 *s = p;
		const u8 *s_end = p + 64;
		int nuls = 0;

		if (s_end > end)
			s_end = end;
		while (s < s_end) {
			if (!*s && ++nuls >= 8)
				break;
			s++;
		}
		if (nuls < 8)
			return 0;
	}

	for (i = 0; i < 256; i++) {
		const u8 *start = q;

		offsets[i] = (u32)(start - p);
		while (q < end && *q)
			q++;
		if (q >= end || *q != '\0' || q == start)
			return 0;
		if ((size_t)(q - p) > 4096)
			return 0;
		q++;
	}

	tlen = (u32)(q - p);
	/* kallsyms_token_index is emitted with the label's 8-byte alignment */
	ti = (const u16 *)PTR_ALIGN(q, 8);
	if ((const u8 *)ti + 512 > end)
		return 0;

	for (i = 0; i < 256; i++) {
		if (ti[i] != offsets[i])
			return 0;
	}

	return tlen;
}

/*
 * Locate markers.
 *
 * kallsyms_markers sits just before kallsyms_token_table, but both labels are
 * 8-byte aligned, so there can be up to 4 bytes of zero padding between the
 * last marker and token_table. The array is strictly increasing and starts at
 * 0. Walk backwards from the last marker while the sequence keeps increasing;
 * the result is markers[0].
 */
static bool locate_markers(const u8 *buf_start, const u8 *token_table, const u32 **markers_out,
			   u32 *count_out)
{
	const u8 *limit = token_table - (size_t)KSYM_MARKERS_MAX * 4;
	const u8 *p, *start;
	u32 count;

	if (limit < buf_start)
		limit = buf_start;

	/* token_table must be 4-byte aligned for the backwards u32 walk */
	if (((unsigned long)token_table & 3) != 0)
		return false;

	p = token_table - 4;
	if (p < limit)
		return false;

	/* skip the 0..4 bytes of alignment padding before token_table */
	if (*(const u32 *)p == 0 && p - 4 >= limit)
		p -= 4;

	start = p;
	count = 1;

	while (start - 4 >= limit) {
		u32 cur = *(const u32 *)(start - 4);
		u32 nxt = *(const u32 *)start;

		if (cur < nxt && nxt - cur <= KSYM_MARKER_SPAN_MAX) {
			start -= 4;
			count++;
		} else {
			break;
		}
	}

	/*
     * The real markers array starts at 0. If we did not land on 0, the
     * walk went into preceding data; scan forward for the first 0.
     */
	if (count >= 2 && *(const u32 *)start != 0) {
		const u8 *q;

		for (q = start; q < token_table; q += 4) {
			if (*(const u32 *)q == 0) {
				start = q;
				count = (u32)(token_table - q) / 4;
				break;
			}
		}
	}

	if (count < 2 || *(const u32 *)start != 0)
		return false;

	*markers_out = (const u32 *)start;
	*count_out = count;
	return true;
}

/*
 * Validate a candidate names start:
 *   1. after every 256 decoded symbols the offset equals the matching
 *      markers[k];
 *   2. the whole names section decodes cleanly.
 */
static bool names_start_valid(const u8 *cand, const struct kallsyms_view *v)
{
	const u8 *names_end = v->names_end;
	u32 names_size = (u32)(names_end - cand);
	u32 off = 0;
	u32 k = 1;
	u32 sym_in_group = 0;

	for (;;) {
		char tmp[KSYM_NAME_MAX];
		u32 before = off;

		if (decode_symbol(cand, names_end, v->token_index, v->token_table, &off, tmp,
				  sizeof(tmp)) < 0)
			break;

		if (off <= before)
			return false;

		if (++sym_in_group == 256) {
			sym_in_group = 0;
			if (k < v->marker_count) {
				if (off != v->markers[k])
					return false;
				k++;
			}
		}
	}

	/*
     * All markers matched and decoding ended near the end of names. There
     * may be a little alignment padding between names and markers (16 bytes
     * of slack).
     */
	if (k != v->marker_count)
		return false;
	return off + 16 >= names_size;
}

/*
 * Try every plausible names start until the callback accepts one.
 *
 * Candidates satisfy names_size in
 * [markers[last], markers[last] + 256 * KSYM_SYMBOL_MAX] and every 256-symbol
 * boundary matching markers.
 */
static const u8 *find_names_start(const u8 *buf_start, const struct kallsyms_view *v,
				  bool (*try)(const u8 *names, const u8 *names_end, void *arg),
				  void *arg)
{
	const u8 *names_end = v->names_end;
	const u8 *cand;
	u32 last_marker;
	u32 max_names_size;

	if (v->marker_count < 2)
		return NULL;
	last_marker = v->markers[v->marker_count - 1];
	if (last_marker < 1 || (size_t)last_marker >= (size_t)(names_end - buf_start))
		return NULL;

	/*
     * names_size lies in [last_marker, last_marker + 256*KSYM_SYMBOL_MAX]:
     * the last marker points at the start of the final group, which has at
     * most 256 symbols of at most KSYM_SYMBOL_MAX bytes each.
     */
	max_names_size = last_marker + 256u * KSYM_SYMBOL_MAX;
	if (max_names_size < last_marker) /* overflow guard */
		return NULL;

	cand = names_end - last_marker;

	/*
     * Walk candidate starts downward, but never form a pointer before
     * buf_start (that would be undefined behaviour): terminate explicitly
     * when cand == buf_start instead of decrementing past it. As cand
     * decreases names_end - cand grows, so the size bound is tested first.
     */
	for (;;) {
		if ((size_t)(names_end - cand) > max_names_size)
			break;
		if (names_start_valid(cand, v) && try(cand, names_end, arg))
			return cand;
		if (cand == buf_start)
			break;
		cand--;
	}
	return NULL;
}

/*
 * Given token_table, fill token_index / markers. Returns 0 on success.
 * The names start is left to the caller (there may be several candidates).
 */
static int view_from_token_table(const u8 *buf_start, const u8 *token_table, const u8 *buf_end,
				 struct kallsyms_view *v)
{
	u32 tlen;
	int i;

	memset(v, 0, sizeof(*v));

	tlen = nksu_kallsyms_token_table_len(token_table, buf_end);
	if (!tlen)
		return -ENOENT;
	/* token_index is 8-byte aligned by output_label's ALGN */
	if (token_table + tlen + 7 + 512 > buf_end)
		return -ENOENT;

	v->token_table = token_table;
	v->tlen = tlen;
	v->token_index = (const u16 *)PTR_ALIGN(token_table + tlen, 8);

	if (v->token_index[0] != 0)
		return -ENOENT;
	for (i = 1; i < 256; i++) {
		if (v->token_index[i] <= v->token_index[i - 1] || v->token_index[i] >= tlen)
			return -ENOENT;
	}

	if (!locate_markers(buf_start, token_table, &v->markers, &v->marker_count))
		return -ENOENT;

	v->names_end = (const u8 *)v->markers;
	return 0;
}

/*
 * Full name decoding is streamed on demand (see nksu_kallsyms_consume): names
 * are
 * never stored, only decoded into a small stack buffer.
 */

/*
 * Address array location.
 */

struct addr_info {
	bool base_relative;
	const u32 *offsets; /* BASE_RELATIVE mode */
	unsigned long relative_base;
	const unsigned long *addresses; /* absolute mode (kallsyms_addresses) */
};

/*
 * Match the kernel's kallsyms_sym_address().
 *
 * With CONFIG_KALLSYMS_BASE_RELATIVE (mandatory on arm64):
 *   - without CONFIG_KALLSYMS_ABSOLUTE_PERCPU every entry is an unsigned
 *     32-bit offset from kallsyms_relative_base;
 *   - with it, non-negative entries are absolute values and negative entries
 *     encode relative values as (-offset - 1).
 *
 * CONFIG_KALLSYMS_ABSOLUTE_PERCPU is x86_64-only by default (init/Kconfig:
 * "default X86_64 && SMP") and is not set on arm64, but both branches are
 * kept so the decoder matches whichever kernel config the module was built
 * against. Note the negative branch must sign-extend the offset; a plain
 * (u32) cast would zero-extend and be off by 2^32.
 */
static unsigned long symbol_addr(const struct addr_info *ai, u32 i)
{
	if (ai->base_relative) {
#if IS_ENABLED(CONFIG_KALLSYMS_ABSOLUTE_PERCPU)
		s32 off = (s32)ai->offsets[i];

		if (off >= 0)
			return (unsigned long)off; /* absolute */
		return ai->relative_base - 1 - (unsigned long)(long)off;
#else
		return ai->relative_base + (u32)ai->offsets[i];
#endif
	}
	if (ai->addresses)
		return ai->addresses[i];
	return 0;
}

/*
 * Recover the address array and relative_base from the fixed on-disk layout.
 *
 * scripts/kallsyms.c emits, in this order:
 *     kallsyms_offsets[]        (u32 / .long per symbol)
 *     kallsyms_relative_base    (.quad, 8-byte aligned)
 *     kallsyms_num_syms         (.long, 8-byte aligned)
 *     kallsyms_names            (8-byte aligned)
 *
 * So relative_base is the u64 at names-16, num_syms the u32 at names-8, and
 * the offsets array starts at names-16-ALIGN(num_syms*4, 8) (the array is
 * 8-byte aligned, so its span is rounded up). num_syms must be consistent
 * with the number of markers, which validates the layout.
 */
static int resolve_addresses(const struct kallsyms_view *v, const u8 *buf_start, const u8 *buf_end,
			     struct addr_info *ai, u32 *num_out)
{
	const u8 *names = v->names;
	unsigned long names_addr = (unsigned long)names;
	unsigned long rel_base;
	size_t offsets_size, offsets_span;
	u32 num_syms;

	memset(ai, 0, sizeof(*ai));

	/* marker_count = ceil(num_syms / 256) */
	if (v->marker_count < 2)
		return -ENOENT;

	/* the two scalars sit just before names */
	if (names_addr < (unsigned long)buf_start + 24)
		return -ENOENT;

	memcpy(&num_syms, names - 8, sizeof(num_syms));
	if (num_syms == 0 || num_syms <= (v->marker_count - 1) * 256u ||
	    num_syms > v->marker_count * 256u)
		return -ENOENT;

	/*
     * scripts/kallsyms.c emits kallsyms_offsets[] through output_label(), so
     * the array starts on an 8-byte boundary and is immediately followed by
     * the 8-byte-aligned kallsyms_relative_base. With an odd symbol count the
     * num_syms * 4 bytes end 4 bytes short of the next 8-byte boundary, and 4
     * bytes of padding sit between the array and relative_base. The span must
     * be rounded up to 8 bytes, otherwise the array start -- and therefore
     * every decoded address -- shifts by one u32.
     */
	offsets_size = (size_t)num_syms * sizeof(u32);
	offsets_span = ALIGN(offsets_size, 8);

	/* names must lie inside the scanned buffer */
	if (names_addr < (unsigned long)buf_start || names_addr > (unsigned long)buf_end)
		return -ENOENT;

	/*
     * The offsets array, num_syms and relative_base all sit below names, so
     * names - buf_start must leave room for both scalars and the aligned
     * array. Subtract first so the comparison cannot overflow.
     */
	if (names_addr - (unsigned long)buf_start < 16 + offsets_span)
		return -ENOENT;

	memcpy(&rel_base, names - 16, sizeof(rel_base));
	if (rel_base < 0xffff000000000000UL)
		return -ENOENT;

	ai->base_relative = true;
	ai->offsets = (const u32 *)(names_addr - 16 - offsets_span);
	ai->relative_base = rel_base;
	*num_out = num_syms;
	return 0;
}

/*
 * Single-table consumption.
 */

struct try_ctx {
	const struct kallsyms_view *v;
	const u8 *buf;
	const u8 *buf_end;
	/* results */
	u32 num_syms;
	struct addr_info ai;
	bool ok;
};

/*
 * Candidate names callback: locate the address array from the fixed layout
 * that follows the names stream.
 */
static bool try_names_candidate(const u8 *names, const u8 *names_end, void *arg)
{
	struct try_ctx *ctx = arg;
	struct kallsyms_view v = *ctx->v;

	v.names = names;
	v.names_end = names_end;

	if (resolve_addresses(&v, ctx->buf, ctx->buf_end, &ctx->ai, &ctx->num_syms) != 0)
		return false;

	ctx->ok = true;
	return true;
}

int nksu_kallsyms_consume(const u8 *buf, unsigned long len, const u8 *token_table)
{
	struct kallsyms_view v;
	struct try_ctx ctx;
	const u8 *names;
	int rc;
	u32 i, offset;

	rc = view_from_token_table(buf, token_table, buf + len, &v);
	if (rc)
		return rc;

	memset(&ctx, 0, sizeof(ctx));
	ctx.v = &v;
	ctx.buf = buf;
	ctx.buf_end = buf + len;

	names = find_names_start(buf, &v, try_names_candidate, &ctx);
	if (!names || !ctx.ok)
		return -ENOENT;
	v.names = names;

	/* decode each name and cache name -> address */
	offset = 0;
	for (i = 0; i < ctx.num_syms; i++) {
		char nm[KSYM_NAME_MAX];
		char raw[KSYM_NAME_MAX];
		unsigned long a;

		if (decode_symbol(names, v.names_end, v.token_index, v.token_table, &offset, nm,
				  sizeof(nm)) < 0)
			break;

		a = symbol_addr(&ctx.ai, i);
		if (!a)
			continue;

		/*
         * Keep the original name as well as the suffix-stripped alias.
         *
         * With CONFIG_CFI_CLANG the linker emits a jump-table stub per
         * address-taken function (foo.cfi_jt); sys_call_table holds those
         * stub addresses, not the plain function addresses. The stubs live
         * at the end of .text, so the plain symbol is always seen first and
         * would otherwise win the stripped name. A caller that resolves a
         * sys_call_table entry must get the stub, so the exact name has to
         * stay resolvable too.
         */
		strscpy(raw, nm, sizeof(raw));
		strip_symbol_suffix(nm);
		if (nm[0])
			nksu_ksym_cache_add_locked(nm, a);
		if (strstr(raw, ".cfi_jt"))
			nksu_ksym_cache_add_locked(raw, a);
	}

	return 0;
}

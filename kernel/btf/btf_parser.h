// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef INFRA_BTF_PARSER_H
#define INFRA_BTF_PARSER_H

#include <linux/types.h>

struct btf;

struct nksu_btf_field {
	u32 offset; /* Byte offset */
	u32 size; /* Member size in bytes */
	u32 type_id; /* Original BTF type ID */
	u32 bit_offset; /* Bit offset within the member */
	u32 bitfield_size; /* Zero for non-bitfield */
};

enum btf_field_flags {
	BTF_FIELD_F_ALLOW_BITFIELD = 1U << 0,
	BTF_FIELD_F_CHECK_SIZE = 1U << 1,
};

/*
 * Resolve a structure/union member.
 *
 * @btf: BTF metadata
 * @struct_name: Structure or union name
 * @member_name: Member name
 * @expected_kind: Expected BTF_KIND_*, or 0 to skip
 * @expected_size: Expected size, or 0 to skip
 * @flags: BTF_FIELD_F_* options
 * @field: Result
 *
 * Returns 0 on success, negative errno otherwise.
 */
int btf_get_field(const struct btf *btf, const char *struct_name, const char *member_name,
		  u32 expected_kind, u32 expected_size, u32 flags, struct nksu_btf_field *field);

u32 nksu_btf_resolve_field_off(const char *type_name, const char *field_name, u32 kind,
			       u32 expect_size);
#endif
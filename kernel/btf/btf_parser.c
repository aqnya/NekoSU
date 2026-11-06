// SPDX-License-Identifier: GPL-2.0

#include "btf_parser.h"

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/limits.h>
#include <linux/kernel.h>
#include <linux/btf.h>

static const struct btf_type *btf_resolve_type(const struct btf *btf, u32 id, u32 *resolved_id)
{
	const struct btf_type *type;

	type = btf_type_skip_modifiers(btf, id, resolved_id);
	return type;
}

static const struct btf_type *btf_find_composite(const struct btf *btf, const char *name)
{
	s32 id;

	id = btf_find_by_name_kind(btf, name, BTF_KIND_STRUCT);
	if (id < 0)
		id = btf_find_by_name_kind(btf, name, BTF_KIND_UNION);

	if (id < 0)
		return ERR_PTR(id);

	return btf_type_by_id(btf, id);
}

static int btf_get_type_size(const struct btf *btf, u32 type_id, u32 *size)
{
	const struct btf_type *type;

	type = btf_resolve_type(btf, type_id, NULL);
	if (!type)
		return -EINVAL;

	switch (BTF_INFO_KIND(type->info)) {
	case BTF_KIND_INT:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 13, 0)
	case BTF_KIND_FLOAT:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	case BTF_KIND_ENUM64:
#endif
	case BTF_KIND_ENUM:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_DATASEC:
		*size = type->size;
		return 0;

	case BTF_KIND_PTR:
		*size = sizeof(void *);
		return 0;

	case BTF_KIND_ARRAY: {
		const struct btf_array *array = (const void *)(type + 1);
		u32 element_size;
		int ret;

		ret = btf_get_type_size(btf, array->type, &element_size);
		if (ret)
			return ret;

		if (array->nelems && element_size > U32_MAX / array->nelems)
			return -EOVERFLOW;

		*size = element_size * array->nelems;
		return 0;
	}

	default:
		return -EOPNOTSUPP;
	}
}

static int btf_validate_member(const struct btf *btf, u32 type_id, u32 expected_kind,
			       u32 expected_size, u32 flags, u32 *size)
{
	const struct btf_type *type;
	u32 actual_size;
	int ret;

	type = btf_resolve_type(btf, type_id, NULL);
	if (!type)
		return -EINVAL;

	if (expected_kind && BTF_INFO_KIND(type->info) != expected_kind)
		return -EINVAL;

	ret = btf_get_type_size(btf, type_id, &actual_size);
	if (ret)
		return ret;

	if ((flags & BTF_FIELD_F_CHECK_SIZE) && actual_size != expected_size)
		return -EINVAL;

	if (expected_size && actual_size != expected_size)
		return -EINVAL;

	*size = actual_size;
	return 0;
}

int btf_get_field(const struct btf *btf, const char *struct_name, const char *member_name,
		  u32 expected_kind, u32 expected_size, u32 flags, struct btf_field *field)
{
	const struct btf_type *type;
	const struct btf_member *members;
	const struct btf_member *member;
	const char *name;
	u32 vlen, i, bits;
	u32 offset, bit_offset, bitfield_size;
	u32 member_size;
	int ret;

	if (!btf || !struct_name || !member_name || !field)
		return -EINVAL;

	if (flags & ~BTF_FIELD_F_ALLOW_BITFIELD)
		if (flags & ~(BTF_FIELD_F_ALLOW_BITFIELD | BTF_FIELD_F_CHECK_SIZE))
			return -EINVAL;

	type = btf_find_composite(btf, struct_name);
	if (IS_ERR(type))
		return PTR_ERR(type);

	if (!type)
		return -EINVAL;

	vlen = BTF_INFO_VLEN(type->info);
	members = btf_members(type);

	for (i = 0; i < vlen; i++) {
		member = &members[i];

		name = btf_name_by_offset(btf, member->name_off);
		if (!name)
			return -EINVAL;

		if (strcmp(name, member_name))
			continue;

		bits = member->offset;
		bitfield_size = 0;

		if (BTF_INFO_KFLAG(type->info)) {
			bitfield_size = BTF_MEMBER_BITFIELD_SIZE(bits);
			bits = BTF_MEMBER_BIT_OFFSET(bits);
		}

		if (bitfield_size && !(flags & BTF_FIELD_F_ALLOW_BITFIELD))
			return -EOPNOTSUPP;

		offset = bits / 8;
		bit_offset = bits & 7;

		if (!bitfield_size && bit_offset)
			return -EINVAL;

		ret = btf_validate_member(btf, member->type, expected_kind, expected_size, flags,
					  &member_size);
		if (ret)
			return ret;

		if (offset > type->size || member_size > type->size - offset)
			return -EINVAL;

		if (bitfield_size && (bitfield_size > member_size * 8 ||
				      bit_offset + bitfield_size > member_size * 8))
			return -EINVAL;

		field->offset = offset;
		field->size = member_size;
		field->type_id = member->type;
		field->bit_offset = bit_offset;
		field->bitfield_size = bitfield_size;

		return 0;
	}

	return -ENOENT;
}
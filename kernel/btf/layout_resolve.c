// SPDX-License-Identifier: GPL-3.0-or-later

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include "symbol/symbol.h"
#include "btf_parser.h"

u32 nksu_btf_resolve_field_off(const char *type_name, const char *field_name, u32 kind,
			       u32 expect_size)
{
	struct btf *btf;
	struct btf **btf_vmlinux;
	struct nksu_btf_field field;
	unsigned long sym;
	int ret;

	if (!type_name || !field_name)
		return (u32)-1;

	sym = NKSU_KSYM_LOOKUP("btf_vmlinux");
	if (!sym) {
		pr_err("btf_vmlinux symbol is unresolved\n");
		return (u32)-1;
	}
	btf_vmlinux = (struct btf **)sym;
	btf = READ_ONCE(*btf_vmlinux);
	if (!btf) {
		pr_err("kernel BTF is unavailable\n");
		return (u32)-1;
	}

	ret = btf_get_field(btf, type_name, field_name, kind, expect_size, BTF_FIELD_F_CHECK_SIZE,
			    &field);
	if (ret) {
		pr_err("cannot resolve %s.%s: %d\n", type_name, field_name, ret);
		return (u32)-1;
	}

	if (field.bitfield_size || field.bit_offset)
		return (u32)-1;

	return field.offset;
}
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Kernel feature registry, driven by the manager over IOC_FEATURE_*.
 *
 * KernelSU keeps an equivalent table (policy/feature.c) and lets the daemon flip the
 * switches; here the manager talks to the kernel directly through the control
 * fd, so there is no userspace daemon in the loop.
 */
#include "klog.h"
#include "guards.h"
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "manager/feature.h"
#include "selinux/selinux.h"

/* Last: redirects filp_open/kernel_read/kernel_write to resolved pointers. */
#include "symbol/symbol_compat.h"

#define NKSU_MAX_FEATURES 16

static DEFINE_MUTEX(nksu_feature_mutex);
static const struct nksu_feature *nksu_features[NKSU_MAX_FEATURES];
static int nksu_feature_count;

static const struct nksu_feature *nksu_feature_find(u32 id)
{
	int i;

	for (i = 0; i < nksu_feature_count; i++) {
		if (nksu_features[i]->id == id)
			return nksu_features[i];
	}
	return NULL;
}

int nksu_feature_register(const struct nksu_feature *feature)
{
	int i;

	if (!feature || !feature->name)
		return -EINVAL;

	scoped_guard(nksu_mutex, &nksu_feature_mutex)
	{
		for (i = 0; i < nksu_feature_count; i++) {
			if (nksu_features[i]->id == feature->id)
				return -EEXIST;
		}
		if (nksu_feature_count >= NKSU_MAX_FEATURES)
			return -ENOSPC;
		nksu_features[nksu_feature_count++] = feature;
	}
	return 0;
}

int nksu_feature_get(u32 id, u64 *value)
{
	const struct nksu_feature *feature;
	int ret = -ENOENT;

	if (!value)
		return -EINVAL;

	guard(nksu_mutex)(&nksu_feature_mutex);
	feature = nksu_feature_find(id);
	if (feature && feature->get)
		ret = feature->get(value);
	return ret;
}

int nksu_feature_set(u32 id, u64 value)
{
	const struct nksu_feature *feature;
	int ret = -ENOENT;

	guard(nksu_mutex)(&nksu_feature_mutex);
	feature = nksu_feature_find(id);
	if (feature && feature->set)
		ret = feature->set(value);
	return ret;
}

int nksu_feature_render(char *buf, size_t size)
{
	int i, len = 0;

	if (!buf || size == 0)
		return -EINVAL;

	guard(nksu_mutex)(&nksu_feature_mutex);
	for (i = 0; i < nksu_feature_count; i++) {
		const struct nksu_feature *feature = nksu_features[i];
		u64 value = 0;

		if (feature->get)
			feature->get(&value);

		len += scnprintf(buf + len, size - len, "%u %s %llu\n", feature->id, feature->name,
				 (unsigned long long)value);
		if ((size_t)len >= size)
			break;
	}

	return len;
}

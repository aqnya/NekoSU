// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Per-process removal of module mounts.
 *
 * Module scripts register mount points that must not be visible to ordinary
 * apps.  When a marked zygote process changes to an app UID, the unmount runs
 * from a task_work callback (kern_path/path_umount may sleep, so it cannot run
 * inside the syscall tracepoint itself).
 */
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/types.h>

#include "klog.h"
#include "manager/umount.h"
#include "selinux/selinux.h"
#include "symbol/symbol_compat.h"

#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

struct nksu_umount_entry {
	struct list_head list;
	char *path;
	int flags;
};

static LIST_HEAD(nksu_umount_list);
static DEFINE_MUTEX(nksu_umount_lock);

int nksu_umount_add(const char *path, int flags)
{
	struct nksu_umount_entry *entry;

	if (!path || !path[0])
		return -EINVAL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->path = kstrdup(path, GFP_KERNEL);
	if (!entry->path) {
		kfree(entry);
		return -ENOMEM;
	}
	entry->flags = flags;

	mutex_lock(&nksu_umount_lock);
	list_add_tail(&entry->list, &nksu_umount_list);
	mutex_unlock(&nksu_umount_lock);
	return 0;
}

void nksu_umount_wipe(void)
{
	struct nksu_umount_entry *entry, *tmp;

	mutex_lock(&nksu_umount_lock);
	list_for_each_entry_safe(entry, tmp, &nksu_umount_list, list) {
		list_del(&entry->list);
		kfree(entry->path);
		kfree(entry);
	}
	mutex_unlock(&nksu_umount_lock);
}

static bool nksu_umount_empty(void)
{
	bool empty;

	mutex_lock(&nksu_umount_lock);
	empty = list_empty(&nksu_umount_list);
	mutex_unlock(&nksu_umount_lock);
	return empty;
}

static void nksu_umount_run(void)
{
	const struct cred *old;
	struct nksu_umount_entry *entry;

	if (nksu_umount_empty())
		return;

	old = nksu_borrow_creds_begin();
	mutex_lock(&nksu_umount_lock);
	list_for_each_entry(entry, &nksu_umount_list, list) {
		struct path path;

		if (kern_path(entry->path, 0, &path))
			continue;
		if (path.dentry == path.mnt->mnt_root && path_umount)
			path_umount(&path, entry->flags);
		else
			path_put(&path);
	}
	mutex_unlock(&nksu_umount_lock);
	if (old)
		nksu_borrow_creds_end(old);
}

struct nksu_umount_work {
	struct callback_head cb;
};

static void nksu_umount_work_fn(struct callback_head *cb)
{
	struct nksu_umount_work *work =
		container_of(cb, struct nksu_umount_work, cb);

	nksu_umount_run();
	kfree(work);
}

void nksu_umount_schedule(void)
{
	struct nksu_umount_work *work;

	if (nksu_umount_empty())
		return;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;
	init_task_work(&work->cb, nksu_umount_work_fn);
	if (task_work_add(current, &work->cb, TWA_RESUME))
		kfree(work);
}

#if defined(__clang__)
#pragma clang attribute pop
#endif

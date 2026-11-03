/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_MANAGER_H
#define NKSU_MANAGER_H

#include <linux/types.h>

/*
 * Boot-time ncore bootstrap.  Watches /data/system/packages.xml (which only
 * appears once /data is mounted), verifies the signed image the manager
 * staged in its own data directory and installs it as /data/adb/ncore.
 */
int ncore_boot_init(void);
void ncore_boot_exit(void);

bool is_manager(void);
void nksu_mark_threads_by_pid(pid_t pid);

/*
 * True once the bootstrap has installed (or found) /data/adb/ncore.  The su
 * hook reads this instead of touching the filesystem: it runs inside a
 * syscall tracepoint, where sleeping (kern_path) is not allowed.
 */
bool is_daemon_ready(void);

/* True when uid is the currently verified manager's UID.  Used by the profile
 * store to keep the manager's own entry out of allow.profile: it is re-granted
 * each boot by ncore's verified IOC_SET_MANAGER_UID, so a stale entry must
 * never survive an authoritative failed verification (or a reinstall that
 * changes the UID). */
bool is_manager_uid(uid_t uid);

/* mkdir -p for kernel-internal paths.  Runs with the caller's creds, so callers
 * that touch /data must borrow the nksu domain first. */
int nksu_kpath_mkdir_p(const char *dir);

/*
 * IOC_SET_MANAGER_UID: verify that the calling executable is a signed ncore,
 * then grant the submitted UID the default profile and remember it as the
 * manager.  Only a genuine ncore may reach it.
 */
int ioc_set_manager_uid(const void __user *data, size_t size);

#endif /* NKSU_MANAGER_H */

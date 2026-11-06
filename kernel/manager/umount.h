/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_MANAGER_UMOUNT_H
#define NKSU_MANAGER_UMOUNT_H

#include <linux/types.h>

/*
 * High mount-id range used for every mount created by ncore (module mounts)
 * and for the copies of those mounts made when a process clones a mount
 * namespace.
 */
#define NKSU_MNT_ID_MIN 2000000000U

int nksu_umount_init(void);
void nksu_umount_exit(void);

int nksu_umount_add(const char *path, int flags);
void nksu_umount_wipe(void);
void nksu_umount_schedule(void);

#endif /* NKSU_MANAGER_UMOUNT_H */

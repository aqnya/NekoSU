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
int nksu_umount_feature_init(void);
void nksu_umount_exit(void);

int nksu_mount_id_set_layout(u32 mnt_id_off, u32 mnt_off);

#endif /* NKSU_MANAGER_UMOUNT_H */

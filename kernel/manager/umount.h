#ifndef NKSU_MANAGER_UMOUNT_H
#define NKSU_MANAGER_UMOUNT_H

#include <linux/types.h>

int nksu_umount_add(const char *path, int flags);
void nksu_umount_wipe(void);
void nksu_umount_schedule(void);

#endif

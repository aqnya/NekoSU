/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NekoSU kernel/userspace version (UAPI).
 *
 * This header is the single source of truth for the LKM's manually maintained
 * version.  It is included by the kernel module (kernel/include/nksu.h) and
 * parsed by the manager build, so a developer bumps this value when the KO
 * changes.
 */
#ifndef NKSU_UAPI_VERSION_H
#define NKSU_UAPI_VERSION_H

#define NKSU_VERSION "1.0.5"

#endif /* NKSU_UAPI_VERSION_H */

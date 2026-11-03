/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "ncore: " KBUILD_BASENAME ": " fmt

/*
 * Pull in the old-kernel shims (task_work flag, mmap_lock, kvmalloc, ...) for
 * every translation unit that logs.
 */
#include "kernel_compat.h"

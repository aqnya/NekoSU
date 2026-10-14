// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Steady-state control-fd grant for the boot daemon.
 *
 * The temporary boot watcher (boot/hook_init.c) lets root obtain the control
 * fd via prctl(NKSU_PRCTL_GET_DRIVER_FD) while it is alive, but it is torn
 * down at zygote.  ncore runs as a separate process at every stage, so the
 * later stages (services, boot-completed) need the grant again.  This component
 * installs a normal (non-transient) __NR_prctl slot hook at feature-init time
 * that keeps granting the fd to root for the rest of the boot, and unhooks it
 * on the way out.
 *
 * It is registered last in feature_components so that, in the
 * CONFIG_NKSU_SYSCALL build, it wraps the steady-state dispatcher and is
 * removed before the dispatcher's group restore runs.
 */

#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>

#include "klog.h"
#include "hook/syscall.h"
#include "manager/ioctl.h"

static syscall_fn_t nksu_daemon_orig_prctl;
static bool nksu_daemon_hooked;

static long nksu_daemon_prctl(const struct pt_regs *regs)
{
	unsigned long option;
#if defined(__aarch64__)
	option = regs->regs[0];
#elif defined(__x86_64__)
	option = regs->di;
#else
	option = 0;
#endif

	if (option == NKSU_PRCTL_GET_DRIVER_FD &&
	    (uid_eq(current_euid(), GLOBAL_ROOT_UID) ||
	     uid_eq(current_uid(), GLOBAL_ROOT_UID))) {
		fmac_ctlfd_get();
		return 1;
	}

	return nksu_daemon_orig_prctl ? nksu_daemon_orig_prctl(regs) : -ENOSYS;
}

int nksu_daemon_fd_init(void)
{
	int ret;

	if (!syscall_table) {
		ret = syscall_table_resolve();
		if (ret) {
			pr_err("[daemon] cannot resolve sys_call_table: %d\n",
			       ret);
			return 0;
		}
	}

	ret = syscall_slot_hook(__NR_prctl, nksu_daemon_prctl,
				&nksu_daemon_orig_prctl, "nksu_daemon_fd");
	if (ret) {
		pr_err("[daemon] prctl hook failed: %d\n", ret);
		return 0;
	}
	nksu_daemon_hooked = true;

	pr_info("[daemon] control fd available to root at every stage\n");
	return 0;
}

void nksu_daemon_fd_exit(void)
{
	if (nksu_daemon_hooked) {
		syscall_slot_unhook(__NR_prctl);
		nksu_daemon_hooked = false;
	}
}

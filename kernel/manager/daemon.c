// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * ksud tracepoint marking.
 *
 * ksud and everything it forks must stay visible to the kernel after exec.
 * Instead of hooking prctl and granting the control fd to every root process,
 * mark the executed ksud image and all of its descendants with
 * TIF_SYSCALL_TRACEPOINT.  A sched_process_exec probe recognises the ksud
 * binary, marks it and schedules a task_work that attaches the control fd
 * before returning to userspace.  sched_process_fork propagates the mark to
 * children, so module scripts started by ksud stay in the same kernel-visible
 * group.
 *
 * The probe is registered at second_stage, before init execs ksud at
 * post-fs-data, and stays registered for the lifetime of the module.
 */
#include <linux/binfmts.h>
#include <linux/cred.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#include "symbol/symbol_compat.h"

#include "klog.h"
#include "manager/ioctl.h"

/*
 * task_work_add is reached through the kallsyms-resolved pointer; the module's
 * CFI type hash for that indirect call does not match the running kernel, so
 * disable CFI for this file (same as manager.c / symbol_compat.c).
 */
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

static struct tracepoint *tp_sched_exec;
static struct tracepoint *tp_sched_fork;
static bool nksu_daemon_hooked;

/* task_work runs just before returning to ksud userspace, after exec. */
struct nksu_daemon_work {
	struct callback_head cb;
};

static void nksu_daemon_attach(struct callback_head *cb)
{
	struct nksu_daemon_work *work = container_of(cb, struct nksu_daemon_work, cb);

	fmac_ctlfd_get();
	kfree(work);
}

static bool nksu_daemon_path(const char *filename)
{
	const char *base;

	if (!filename)
		return false;

	base = strrchr(filename, '/');
	base = base ? base + 1 : filename;

	return !strcmp(base, "ksud") || !strcmp(base, "libksud.so");
}

static void nksu_daemon_exec(void *data, struct task_struct *p, pid_t old_pid,
			     struct linux_binprm *bprm)
{
	struct nksu_daemon_work *work;

	if (!uid_eq(task_euid(p), GLOBAL_ROOT_UID))
		return;
	if (!nksu_daemon_path(bprm ? bprm->filename : NULL))
		return;

	set_tsk_thread_flag(p, TIF_SYSCALL_TRACEPOINT);
	pr_info("[daemon] marked %s (pid %d) as ksud\n",
		bprm ? bprm->filename : "?", p->pid);

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;
	init_task_work(&work->cb, nksu_daemon_attach);
	if (task_work_add(p, &work->cb, TWA_RESUME))
		kfree(work);
}

static void nksu_daemon_fork(void *data, struct task_struct *parent,
			     struct task_struct *child)
{
	if (test_tsk_thread_flag(parent, TIF_SYSCALL_TRACEPOINT))
		set_tsk_thread_flag(child, TIF_SYSCALL_TRACEPOINT);
}

struct nksu_tp_find_ctx {
	const char *name;
	struct tracepoint **out;
};

static void nksu_tp_find_cb(struct tracepoint *tp, void *priv)
{
	struct nksu_tp_find_ctx *ctx = priv;

	if (*ctx->out)
		return;
	if (!strcmp(tp->name, ctx->name))
		*ctx->out = tp;
}

static struct tracepoint *nksu_find_tracepoint(const char *name)
{
	struct tracepoint *result = NULL;
	struct nksu_tp_find_ctx ctx = { .name = name, .out = &result };

	for_each_kernel_tracepoint(nksu_tp_find_cb, &ctx);
	return result;
}

int nksu_daemon_fd_init(void)
{
	int ret;

	if (nksu_daemon_hooked)
		return 0;

	tp_sched_exec = nksu_find_tracepoint("sched_process_exec");
	tp_sched_fork = nksu_find_tracepoint("sched_process_fork");
	if (!tp_sched_exec || !tp_sched_fork) {
		pr_err("[daemon] sched tracepoints not found\n");
		return -ENOENT;
	}

	ret = tracepoint_probe_register(tp_sched_exec, nksu_daemon_exec, NULL);
	if (ret) {
		pr_err("[daemon] register sched_process_exec failed: %d\n", ret);
		return ret;
	}

	ret = tracepoint_probe_register(tp_sched_fork, nksu_daemon_fork, NULL);
	if (ret) {
		pr_err("[daemon] register sched_process_fork failed: %d\n", ret);
		tracepoint_probe_unregister(tp_sched_exec, nksu_daemon_exec, NULL);
		return ret;
	}

	nksu_daemon_hooked = true;
	pr_info("[daemon] ksud tracepoint marking active\n");
	return 0;
}

void nksu_daemon_fd_exit(void)
{
	if (!nksu_daemon_hooked)
		return;

	tracepoint_probe_unregister(tp_sched_exec, nksu_daemon_exec, NULL);
	tracepoint_probe_unregister(tp_sched_fork, nksu_daemon_fork, NULL);
	tracepoint_synchronize_unregister();
	nksu_daemon_hooked = false;
	pr_info("[daemon] ksud tracepoint marking stopped\n");
}

#if defined(__clang__)
#pragma clang attribute pop
#endif

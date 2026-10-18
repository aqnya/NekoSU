/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_TASK_H
#define NKSU_TASK_H

#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/types.h>

/*
 * Private thread-info bits used to tell ncore and its descendants apart from
 * zygote/app processes.
 *
 * TIF_SYSCALL_TRACEPOINT is shared: zygote and every app carry it, so it can
 * only gate the sys_enter probe, not identify the daemon.  These two bits are
 * deliberately outside the kernel's work masks; nothing else in the kernel
 * tests them.  They are inherited across fork and never cleared.
 */
#define NKSU_TIF_NCORE      30
#define NKSU_TIF_CLONE_MNT  29
#define NKSU_TIF_IDA_ALLOC  28

static inline bool nksu_task_is_ncore(struct task_struct *task)
{
	return test_tsk_thread_flag(task, NKSU_TIF_NCORE);
}

static inline bool nksu_task_clone_mnt(struct task_struct *task)
{
	return test_tsk_thread_flag(task, NKSU_TIF_CLONE_MNT);
}

static inline void nksu_task_set_clone_mnt(struct task_struct *task)
{
	set_tsk_thread_flag(task, NKSU_TIF_CLONE_MNT);
}

static inline void nksu_task_clear_clone_mnt(struct task_struct *task)
{
	clear_tsk_thread_flag(task, NKSU_TIF_CLONE_MNT);
}

static inline bool nksu_task_ida_alloc(struct task_struct *task)
{
	return test_tsk_thread_flag(task, NKSU_TIF_IDA_ALLOC);
}

static inline void nksu_task_set_ida_alloc(struct task_struct *task)
{
	set_tsk_thread_flag(task, NKSU_TIF_IDA_ALLOC);
}

static inline void nksu_task_clear_ida_alloc(struct task_struct *task)
{
	clear_tsk_thread_flag(task, NKSU_TIF_IDA_ALLOC);
}

#endif /* NKSU_TASK_H */

// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Module mount hiding: module mounts are given a high mount id and the
 * mount-info readers filter those ids out.  The mounts stay in place; nothing
 * is unmounted.
 */
#include <linux/cred.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/kprobes.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/syscalls.h>
#include <linux/tracepoint.h>
#include <linux/types.h>
#include <asm/ptrace.h>

#include "hook/tools.h"
#include "klog.h"
#include "manager/feature.h"
#include "manager/umount.h"
#include "nksu_task.h"
#include "symbol/symbol.h"

#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

static DEFINE_MUTEX(nksu_umount_state_lock);
static bool nksu_umount_running;

/*
 * ---------------------------------------------------------------------------
 * Mount-id isolation
 * ---------------------------------------------------------------------------
 * Every mount created by ncore (and every clone of such a mount) gets an id
 * from the high range instead of the kernel's low, sequential allocator.  The
 * mount-info readers then omit high-id mounts unless the reader is ncore
 * itself.  The low id sequence an app observes therefore stays contiguous even
 * after the per-app umount removed module mounts from its namespace.
 *
 * The struct mount field offsets are supplied by ncore, which reads them from
 * the kernel's BTF.  Hard-coding offsets across GKI versions is not an option.
 */
#if IS_ENABLED(CONFIG_KPROBES)

static typeof(register_kprobe) *nksu_register_kprobe;
static typeof(unregister_kprobe) *nksu_unregister_kprobe;
static typeof(ida_alloc_range) *nksu_ida_alloc_range;
static struct ida *nksu_mnt_id_ida;

static u32 nksu_mnt_id_off;
static u32 nksu_mnt_off;

static bool nksu_mount_layout_ready;
static bool nksu_mount_layout_seen;
static bool nksu_ida_probe_ready;
static bool nksu_mount_probes_ready;
static bool nksu_clone_probe_ready;
static bool nksu_show_probe_ready[3];
static DEFINE_MUTEX(nksu_mount_layout_lock);

#if defined(__aarch64__)
static unsigned long nksu_arg0(const struct pt_regs *regs)
{
	return regs->regs[0];
}

static unsigned long nksu_arg1(const struct pt_regs *regs)
{
	return regs->regs[1];
}
#elif defined(__x86_64__)
static unsigned long nksu_arg0(const struct pt_regs *regs)
{
	return regs->di;
}

static unsigned long nksu_arg1(const struct pt_regs *regs)
{
	return regs->si;
}
#else
#error "unsupported arch"
#endif

/* Skip the probed function and make it return @ret. */
static void nksu_skip_current(struct pt_regs *regs, long ret)
{
#if defined(__aarch64__)
	regs->regs[0] = ret;
	regs->pc = regs->regs[30];
#elif defined(__x86_64__)
	regs->ax = ret;
	regs->ip = *(unsigned long *)regs->sp;
#endif
}

/*
 * mnt_alloc_id() is inlined into alloc_vfsmnt() on newer kernels, so the
 * stable probe point is the ida_alloc_range() call it makes.  Match the IDA
 * object itself: only allocations from mnt_id_ida are redirected.
 */
static int nksu_ida_alloc_pre(struct kprobe *p, struct pt_regs *regs)
{
	struct task_struct *task = current;
	struct ida *ida = (struct ida *)nksu_arg0(regs);
	bool clone = nksu_task_clone_mnt(task);
	bool high_mnt = nksu_task_high_mnt(task);
	int id;

	if (ida != nksu_mnt_id_ida || !READ_ONCE(nksu_mount_layout_ready))
		return 0;
	if (!nksu_ida_alloc_range)
		return 0;
	if (!nksu_task_is_ncore(task) && !clone && !high_mnt)
		return 0;
	/* The recursive allocation below must reach the real function. */
	if (nksu_task_ida_alloc(task))
		return 0;

	/*
	 * GFP_ATOMIC: kprobe handlers run with preemption disabled.  The
	 * allocation can fail under memory pressure; fall through to the
	 * kernel allocator and make the failure visible instead of handing out
	 * a duplicate id.
	 */
	nksu_task_set_ida_alloc(task);
	id = nksu_ida_alloc_range(nksu_mnt_id_ida, NKSU_MNT_ID_MIN,
				  INT_MAX - 1, GFP_ATOMIC);
	nksu_task_clear_ida_alloc(task);
	if (id < 0) {
		pr_warn_ratelimited("[mount-id] high id allocation failed: %d\n",
				    id);
		if (clone)
			nksu_task_clear_clone_mnt(task);
		if (high_mnt)
			nksu_task_clear_high_mnt(task);
		return 0;
	}

	if (clone)
		nksu_task_clear_clone_mnt(task);

	nksu_skip_current(regs, id);
	return 1;
}

/*
 * clone_mnt(old, root, flag) calls alloc_vfsmnt() -> mnt_alloc_id() for the
 * copy.  A namespace clone in zygote/app context has no ncore marker, so the
 * copy must inherit the "high id" property from the source mount.
 */
static int nksu_clone_mnt_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *old = (void *)nksu_arg0(regs);
	int id;

	if (!old || !READ_ONCE(nksu_mount_layout_ready))
		return 0;

	id = READ_ONCE(*(int *)((char *)old + nksu_mnt_id_off));
	if (id >= (int)NKSU_MNT_ID_MIN)
		nksu_task_set_clone_mnt(current);

	return 0;
}

/*
 * show_mountinfo()/show_vfsmnt()/show_vfsstat() receive the embedded
 * struct vfsmount, not struct mount.  The offset comes from BTF, so recover
 * the container and read its id.
 */
static int nksu_show_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *vmnt = (void *)nksu_arg1(regs);
	void *mnt;
	int id;

	if (!vmnt || !READ_ONCE(nksu_mount_layout_ready))
		return 0;
	/* ncore must still see its own mounts (module install/debug). */
	if (nksu_task_is_ncore(current))
		return 0;

	mnt = (char *)vmnt - nksu_mnt_off;
	id = READ_ONCE(*(int *)((char *)mnt + nksu_mnt_id_off));
	if (id < (int)NKSU_MNT_ID_MIN)
		return 0;

	nksu_skip_current(regs, 0);
	return 1;
}

static struct kprobe nksu_kp_ida_alloc = {
	.symbol_name = "ida_alloc_range",
	.pre_handler = nksu_ida_alloc_pre,
};

static struct kprobe nksu_kp_clone_mnt = {
	.symbol_name = "clone_mnt",
	.pre_handler = nksu_clone_mnt_pre,
};

static struct kprobe nksu_kp_show_mountinfo = {
	.symbol_name = "show_mountinfo",
	.pre_handler = nksu_show_pre,
};

static struct kprobe nksu_kp_show_vfsmnt = {
	.symbol_name = "show_vfsmnt",
	.pre_handler = nksu_show_pre,
};

static struct kprobe nksu_kp_show_vfsstat = {
	.symbol_name = "show_vfsstat",
	.pre_handler = nksu_show_pre,
};

static struct kprobe *const nksu_show_probes[] = {
	&nksu_kp_show_mountinfo,
	&nksu_kp_show_vfsmnt,
	&nksu_kp_show_vfsstat,
};

static int nksu_resolve_probe_symbols(void)
{
	if (!nksu_register_kprobe) {
		nksu_register_kprobe =
			(typeof(register_kprobe) *)nksu_ksym_lookup("register_kprobe");
		nksu_unregister_kprobe =
			(typeof(unregister_kprobe) *)nksu_ksym_lookup("unregister_kprobe");
		nksu_ida_alloc_range =
			(typeof(ida_alloc_range) *)nksu_ksym_lookup("ida_alloc_range");
		nksu_mnt_id_ida =
			(struct ida *)nksu_ksym_lookup("mnt_id_ida");
	}

	if (!nksu_register_kprobe || !nksu_unregister_kprobe ||
	    !nksu_ida_alloc_range || !nksu_mnt_id_ida) {
		pr_err("[mount-id] unresolved kprobe/ida symbols\n");
		return -ENOENT;
	}
	return 0;
}

static int nksu_register_one(struct kprobe *kp, const char *name, bool required)
{
	int ret;

	/* A probe that once failed registration keeps a stale .addr. */
	kp->addr = NULL;
	ret = nksu_register_kprobe(kp);
	if (ret) {
		if (required)
			pr_err("[mount-id] register_kprobe(%s) failed: %d\n",
			       name, ret);
		else
			pr_warn("[mount-id] register_kprobe(%s) failed: %d\n",
				name, ret);
		return ret;
	}
	pr_info("[mount-id] hooked %s\n", name);
	return 0;
}

static int nksu_mount_probes_register_locked(void)
{
	int ret, i, show_ok = 0;

	if (nksu_mount_probes_ready)
		return 0;

	ret = nksu_resolve_probe_symbols();
	if (ret)
		return ret;

	if (!nksu_ida_probe_ready) {
		ret = nksu_register_one(&nksu_kp_ida_alloc, "ida_alloc_range", true);
		if (ret)
			return ret;
		nksu_ida_probe_ready = true;
	}

	if (!nksu_clone_probe_ready) {
		ret = nksu_register_one(&nksu_kp_clone_mnt, "clone_mnt", false);
		if (!ret)
			nksu_clone_probe_ready = true;
	}

	for (i = 0; i < ARRAY_SIZE(nksu_show_probes); i++) {
		if (nksu_show_probe_ready[i]) {
			show_ok++;
			continue;
		}
		ret = nksu_register_one(nksu_show_probes[i],
					nksu_show_probes[i]->symbol_name, false);
		if (!ret) {
			nksu_show_probe_ready[i] = true;
			show_ok++;
		}
	}

	if (!show_ok) {
		pr_err("[mount-id] no mountinfo reader could be hooked\n");
		return -ENOENT;
	}

	nksu_mount_probes_ready = true;
	return 0;
}

int nksu_mount_id_set_layout(u32 mnt_id_off, u32 mnt_off)
{
	int ret;

	if (mnt_id_off > 4096 - sizeof(int) || mnt_off > 4096 ||
	    (mnt_id_off & (sizeof(int) - 1)))
		return -EINVAL;

	mutex_lock(&nksu_mount_layout_lock);

	WRITE_ONCE(nksu_mnt_id_off, mnt_id_off);
	WRITE_ONCE(nksu_mnt_off, mnt_off);
	nksu_mount_layout_seen = true;

	/* The feature may be disabled from the manager; keep only the layout. */
	if (!READ_ONCE(nksu_umount_running)) {
		mutex_unlock(&nksu_mount_layout_lock);
		return 0;
	}

	ret = nksu_mount_probes_register_locked();
	if (ret) {
		mutex_unlock(&nksu_mount_layout_lock);
		return ret;
	}

	WRITE_ONCE(nksu_mount_layout_ready, true);
	mutex_unlock(&nksu_mount_layout_lock);
	pr_info("[mount-id] active (mnt_id@%u, mnt@%u)\n", mnt_id_off,
		mnt_off);
	return 0;
}

static void nksu_mount_probes_exit(void)
{
	int i;

	WRITE_ONCE(nksu_mount_layout_ready, false);

	if (!nksu_unregister_kprobe)
		return;

	if (nksu_ida_probe_ready) {
		nksu_unregister_kprobe(&nksu_kp_ida_alloc);
		nksu_ida_probe_ready = false;
	}
	if (nksu_clone_probe_ready) {
		nksu_unregister_kprobe(&nksu_kp_clone_mnt);
		nksu_clone_probe_ready = false;
	}
	for (i = 0; i < ARRAY_SIZE(nksu_show_probes); i++) {
		if (!nksu_show_probe_ready[i])
			continue;
		nksu_unregister_kprobe(nksu_show_probes[i]);
		nksu_show_probe_ready[i] = false;
	}
	nksu_mount_probes_ready = false;
}

#else /* !CONFIG_KPROBES */

int nksu_mount_id_set_layout(u32 mnt_id_off, u32 mnt_off)
{
	pr_err("[mount-id] CONFIG_KPROBES is disabled\n");
	return -EOPNOTSUPP;
}

static int nksu_mount_probes_register_locked(void)
{
	return -EOPNOTSUPP;
}

static void nksu_mount_probes_exit(void)
{
}

#endif /* CONFIG_KPROBES */

/*
 * ---------------------------------------------------------------------------
 * sys_enter observation
 * ---------------------------------------------------------------------------
 * This is a probe owned by the mount-hiding feature, not an entry in the main
 * syscall hook.
 */

/*
 * Module mounts must be hidden without tearing them down.  If a mount is
 * created from the module store, mark the task for the duration of the
 * syscall; the mnt_id allocation hook then gives it a high id and the
 * mount-info readers filter that id out.
 */
static bool nksu_path_is_module(const char *path)
{
	return !strncmp(path, "/data/adb/modules/", 18) ||
	       !strncmp(path, "/data/adb/modules_update/", 25) ||
	       !strncmp(path, "/adb/modules/", 13) ||
	       !strncmp(path, "/adb/modules_update/", 20);
}

static bool nksu_read_abs_path(struct pt_regs *regs, unsigned int argno,
			       char *buf, size_t len)
{
	int n = get_path_arg(regs, argno, buf, len);

	return n > 0 && n < (int)len - 1 && buf[0] == '/';
}

static bool nksu_is_module_mount(struct pt_regs *regs)
{
	char source[256];
	char target[256];
	char data[512];
	bool source_ok;
	int n;

	source_ok = nksu_read_abs_path(regs, 0, source, sizeof(source));
	if (!nksu_read_abs_path(regs, 1, target, sizeof(target)))
		return false;

	if (nksu_path_is_module(target) ||
	    (source_ok && nksu_path_is_module(source)))
		return true;

	/* overlayfs puts the module paths in the data argument. */
	n = get_path_arg(regs, 4, data, sizeof(data));
	return n > 0 &&
	       (strstr(data, "/data/adb/modules") ||
		strstr(data, "/adb/modules"));
}

static void nksu_umount_sys_enter(void *data, struct pt_regs *regs, long id)
{
	bool mount_call = id == __NR_mount;

#ifdef __NR_move_mount
	mount_call = mount_call || id == __NR_move_mount;
#endif
#ifdef __NR_fsmount
	mount_call = mount_call || id == __NR_fsmount;
#endif

	/*
	 * The high-id marker covers every mount made by one syscall (a recursive
	 * bind can create several).  It is cleared at the next syscall entry.
	 */
	if (!mount_call)
		nksu_task_clear_high_mnt(current);

	switch (id) {
	case __NR_mount:
		nksu_task_clear_high_mnt(current);
		if (nksu_is_module_mount(regs))
			nksu_task_set_high_mnt(current);
		break;
#ifdef __NR_fsmount
	case __NR_fsmount:
		nksu_task_clear_high_mnt(current);
		/* New mount API: the id is allocated by fsmount(). */
		if (nksu_task_is_ncore(current) ||
		    uid_eq(current_euid(), GLOBAL_ROOT_UID))
			nksu_task_set_high_mnt(current);
		break;
#endif
	}
}

static struct tracepoint *nksu_umount_tp_sys_enter;
static bool nksu_umount_hooked;
static DEFINE_MUTEX(nksu_umount_init_lock);

struct nksu_umount_tp_ctx {
	const char *name;
	struct tracepoint **out;
};

static void nksu_umount_tp_cb(struct tracepoint *tp, void *priv)
{
	struct nksu_umount_tp_ctx *ctx = priv;

	if (*ctx->out)
		return;
	if (!strcmp(tp->name, ctx->name))
		*ctx->out = tp;
}

static struct tracepoint *nksu_umount_find_tracepoint(const char *name)
{
	struct tracepoint *result = NULL;
	struct nksu_umount_tp_ctx ctx = { .name = name, .out = &result };

	for_each_kernel_tracepoint(nksu_umount_tp_cb, &ctx);
	return result;
}

static int nksu_umount_start(void)
{
	int ret;

	mutex_lock(&nksu_umount_init_lock);
	if (nksu_umount_hooked) {
		mutex_unlock(&nksu_umount_init_lock);
		return 0;
	}

	nksu_umount_tp_sys_enter = nksu_umount_find_tracepoint("sys_enter");
	if (!nksu_umount_tp_sys_enter) {
		pr_err("[umount] cannot find sys_enter tracepoint\n");
		mutex_unlock(&nksu_umount_init_lock);
		return -ENOENT;
	}

	ret = tracepoint_probe_register(nksu_umount_tp_sys_enter,
					nksu_umount_sys_enter, NULL);
	if (ret) {
		pr_err("[umount] register sys_enter probe failed: %d\n", ret);
		mutex_unlock(&nksu_umount_init_lock);
		return ret;
	}

	nksu_umount_hooked = true;
	mutex_unlock(&nksu_umount_init_lock);

	/*
	 * Mount-id probes are registered when the BTF layout arrives, i.e.
	 * immediately before ncore's first mount, or here if the feature is
	 * re-enabled after a boot-time disable.  Keeping them out of the
	 * pre-layout window avoids a kprobe on every ida_alloc_range() call.
	 */
	if (nksu_mount_layout_seen) {
		mutex_lock(&nksu_mount_layout_lock);
		ret = nksu_mount_probes_register_locked();
		if (!ret)
			WRITE_ONCE(nksu_mount_layout_ready, true);
		mutex_unlock(&nksu_mount_layout_lock);
		if (ret)
			pr_warn("[mount-id] probes not ready: %d\n", ret);
	}

	pr_info("[umount] sys_enter probe active\n");
	return 0;
}

static void nksu_umount_stop(void)
{
	nksu_mount_probes_exit();

	if (nksu_umount_hooked) {
		tracepoint_probe_unregister(nksu_umount_tp_sys_enter,
					    nksu_umount_sys_enter, NULL);
		tracepoint_synchronize_unregister();
		nksu_umount_hooked = false;
	}
}

#define NKSU_UMOUNT_FEATURE_NAME "umount"
#define NKSU_UMOUNT_DISABLED_NAME "umount_disabled"

static int nksu_umount_feature_get(u64 *value)
{
	*value = READ_ONCE(nksu_umount_running) ? 1 : 0;
	return 0;
}

static int nksu_umount_apply(u64 value, bool persist)
{
	int ret = 0;

	mutex_lock(&nksu_umount_state_lock);
	if (value) {
		if (!nksu_umount_running) {
			ret = nksu_umount_start();
			if (!ret)
				nksu_umount_running = true;
		}
	} else if (nksu_umount_running) {
		nksu_umount_stop();
		nksu_umount_running = false;
	}
	mutex_unlock(&nksu_umount_state_lock);

	if (!ret && persist &&
	    nksu_feature_persist_set(NKSU_UMOUNT_DISABLED_NAME, !value))
		pr_warn("[umount] cannot persist feature state\n");

	return ret;
}

static int nksu_umount_feature_set(u64 value)
{
	return nksu_umount_apply(value, true);
}

static const struct nksu_feature nksu_umount_feature = {
	.id = NKSU_FEATURE_UMOUNT,
	.name = NKSU_UMOUNT_FEATURE_NAME,
	.get = nksu_umount_feature_get,
	.set = nksu_umount_feature_set,
};

int nksu_umount_init(void)
{
	int ret;

	ret = nksu_feature_register(&nksu_umount_feature);
	if (ret && ret != -EEXIST) {
		pr_err("[umount] feature register failed: %d\n", ret);
		return ret;
	}

	mutex_lock(&nksu_umount_state_lock);
	ret = nksu_umount_start();
	if (!ret)
		nksu_umount_running = true;
	mutex_unlock(&nksu_umount_state_lock);

	return ret;
}

/*
 * Runs in the feature group, after /data and the SELinux domain are available.
 * The feature itself must already be running (it has to cover ncore's mount
 * scripts), so this only applies a persisted "disabled" marker.
 */
int nksu_umount_feature_init(void)
{
	if (!nksu_feature_persist_has(NKSU_UMOUNT_DISABLED_NAME))
		return 0;

	pr_info("[umount] disabled by persisted feature state\n");
	return nksu_umount_apply(0, false);
}

void nksu_umount_exit(void)
{
	mutex_lock(&nksu_umount_state_lock);
	nksu_umount_stop();
	nksu_umount_running = false;
	mutex_unlock(&nksu_umount_state_lock);
}

#if defined(__clang__)
#pragma clang attribute pop
#endif

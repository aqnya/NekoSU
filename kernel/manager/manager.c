// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Manager authorization and the boot-time ncore bootstrap.
 *
 * Trust is anchored in the signed ncore image rather than in the manager APK:
 * the manager packages libncore.so as an extracted native library, and at boot
 * the kernel walks /data/app for it, verifies the RSA-4096 signature appended
 * by neko/build.gradle.kts and installs the verified image as /data/adb/ncore.
 * ncore itself (whose executable is verified the same way) then reports the
 * manager package's UID through IOC_SET_MANAGER_UID and the kernel grants that
 * UID the default profile.
 *
 * fsnotify still watches /data/system/packages.xml.  PackageManagerService
 * only writes that file once /data is mounted and the package database is
 * usable, so the event doubles as the "data is ready" trigger (and retries
 * the bootstrap whenever packages change).
 */
#include "klog.h"
#include "guards.h"
#include "manager/manager.h"
#include "signing/signature.h"
#include "boot/safe_mode.h"
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/dcache.h>
#include <linux/mount.h>
#include <linux/path.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/fsnotify_backend.h>
#include <linux/task_work.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <linux/workqueue.h>
#include <linux/uidgid.h>
#include <fmac.h>
#include "hook/handle.h"

/* Last: redirects vfs_mkdir/lookup_one_len to the resolved-symbol pointers. */
#include "symbol/symbol_compat.h"

#define PKGWATCH_DIR "/data/system"
#define PKGWATCH_NAME "packages.xml"
#define PKGWATCH_MASK (FS_MOVE | FS_CLOSE_WRITE | FS_EVENT_ON_CHILD)

static struct fsnotify_group *pkgwatch_group;
static struct fsnotify_mark *pkgwatch_mark;
static bool pkgwatch_shutdown;

/*
 * The signed ncore image lives where PackageManager extracts the APK's native
 * libraries (/data/app/<...>/lib/<abi>/libncore.so), and the boot-visible copy
 * init execs is /data/adb/ncore.  Neither the package name nor the directory
 * is trusted: only the signature appended to the image is.
 */
#define NKSU_APP_DIR "/data/app"
#define NKSU_NCORE_LIB_NAME "libncore.so"
#define NKSU_DAEMON_BOOT_PATH "/data/adb/ncore"
#define NKSU_DAEMON_BIN_DIR "/data/adb/nksu/bin"

static bool kfile_exists(const char *path);
static int mark_zygote(void);

static kuid_t manager_kuid = INVALID_UID;

bool is_manager(void)
{
	return uid_valid(manager_kuid) && uid_eq(current_uid(), manager_kuid);
}

bool is_manager_uid(uid_t uid)
{
	return uid_valid(manager_kuid) && __kuid_val(manager_kuid) == uid;
}

/*
 * Drop the current manager's kernel identity and its profile entry.  Called
 * when the verified ncore reports a different manager UID or when the new
 * profile cannot be installed.
 */
static void revoke_manager(void)
{
	kuid_t old = manager_kuid;
	int ret;

	if (!uid_valid(old))
		return;

	manager_kuid = INVALID_UID;
	ret = nksu_profile_clear(__kuid_val(old));
	if (ret && ret != -ENOENT)
		pr_err("cannot revoke UID %u profile: %d\n", __kuid_val(old), ret);
	else
		pr_info("revoked manager UID %u\n", __kuid_val(old));
}

int ioc_set_manager_uid(const void __user *data, size_t size)
{
	kuid_t kuid, previous;
	u32 uid;
	int ret;

	if (size < sizeof(uid))
		return -EINVAL;

	if (copy_from_user(&uid, data, sizeof(uid)))
		return -EFAULT;

	kuid = KUIDT_INIT(uid);
	if (!uid_valid(kuid))
		return -EINVAL;

	/*
	 * Only a genuine ncore may name the manager UID.  Its executable is
	 * verified against the pinned signing key; anything else that happens
	 * to inherit the control fd (a module script, a forked helper) is
	 * rejected here.
	 */
	ret = nksu_ncore_verify_caller();
	if (ret) {
		pr_err("refusing manager UID %u: caller is not signed ncore: %d\n", uid, ret);
		return ret;
	}

	previous = manager_kuid;
	if (!uid_eq(kuid, previous)) {
		manager_kuid = INVALID_UID;
		if (uid_valid(previous)) {
			ret = nksu_profile_clear(__kuid_val(previous));
			if (ret && ret != -ENOENT)
				pr_warn("cannot revoke previous UID %u profile: %d\n",
					__kuid_val(previous), ret);
		}
		manager_kuid = kuid;
	}

	ret = nksu_profile_set_default(uid);
	if (ret) {
		pr_err("cannot grant profile to manager UID %u: %d\n", uid, ret);
		revoke_manager();
		return ret;
	}

	/*
	 * The manager process and the zygote/app pool need the tracepoint mark
	 * before they can reach the prctl/profile hooks; sync the redirect
	 * mappings for processes that were already running.
	 */
	if (mark_zygote())
		pr_warn("cannot mark the manager/zygote processes\n");
	nksu_path_sync_all();

	pr_info("manager UID %u verified and authorized\n", uid);
	return 0;
}

/*
 * Whether /data/adb/ncore is known to be present.  Set by the (sleepable)
 * bootstrap; read by the su tracepoint hook, which must not sleep.
 */
static bool daemon_ready;

bool is_daemon_ready(void)
{
	return READ_ONCE(daemon_ready);
}

/* ---- file helpers (run with the borrowed nksu creds, see below) ---- */

/* mkdir one leaf whose parent already exists. */
static int __nocfi kdir_mkdir(const char *path, umode_t mode)
{
	char parent[256];
	const char *slash;
	struct path p;
	struct inode *dir;
	struct dentry *dentry;
	size_t len;
	int err;

	slash = strrchr(path, '/');
	if (!slash || slash == path)
		return -EINVAL;

	len = (size_t)(slash - path);
	if (len >= sizeof(parent))
		return -ENAMETOOLONG;
	memcpy(parent, path, len);
	parent[len] = '\0';

	err = kern_path(parent, LOOKUP_DIRECTORY | LOOKUP_FOLLOW, &p);
	if (err)
		return err;

	dir = d_inode(p.dentry);
	inode_lock_nested(dir, I_MUTEX_PARENT);
	dentry = lookup_one_len(slash + 1, p.dentry, (int)strlen(slash + 1));
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
	} else {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
		err = vfs_mkdir(mnt_idmap(p.mnt), dir, dentry, mode);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
		err = vfs_mkdir(mnt_user_ns(p.mnt), dir, dentry, mode);
#else
		err = vfs_mkdir(dir, dentry, mode);
#endif
		if (err == -EEXIST)
			err = 0;
		dput(dentry);
	}
	inode_unlock(dir);
	path_put(&p);
	return err;
}

static int kdir_mkdir_p(const char *dir)
{
	char tmp[256];
	size_t n = strlen(dir);
	int err = 0;

	if (n == 0 || n >= sizeof(tmp))
		return -EINVAL;
	memcpy(tmp, dir, n + 1);

	for (char *q = tmp + 1;; q++) {
		if (*q == '/' || *q == '\0') {
			char saved = *q;

			*q = '\0';
			if (!kfile_exists(tmp)) {
				int e = kdir_mkdir(tmp, 0700);

				if (e && !err)
					err = e;
			}
			*q = saved;
			if (saved == '\0')
				break;
		}
	}
	return err;
}

int nksu_kpath_mkdir_p(const char *dir)
{
	return kdir_mkdir_p(dir);
}

static bool kfile_exists(const char *path)
{
	struct file *f __free(nksu_filp) = filp_open(path, O_RDONLY, 0);

	return !IS_ERR(f);
}

static void __nocfi kfile_remove(struct file *file);

/*
 * Copy the already-open, already-verified image @in to @dst.  Copying from the
 * verified file keeps the image the signature covered: reopening @src would
 * leave a window in which the path could be replaced with an unverified one.
 */
static int kfile_copy(struct file *in, const char *src, const char *dst)
{
	loff_t ipos = 0, opos = 0;
	loff_t src_size, dst_size;
	ssize_t off;
	int err = 0;

	char *buf __free(nksu_kfree) = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	struct file *out __free(nksu_filp) = filp_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (IS_ERR(out))
		return PTR_ERR(out);

	for (;;) {
		ssize_t r = nksu_kread(in, buf, PAGE_SIZE, &ipos);

		if (r < 0) {
			err = (int)r;
			break;
		}
		if (r == 0)
			break;

		off = 0;
		while (off < r) {
			ssize_t w = nksu_kwrite(out, buf + off, (size_t)(r - off), &opos);

			if (w <= 0) {
				err = w ? (int)w : -EIO;
				break;
			}
			off += w;
		}
		if (err)
			break;
	}

	if (!err) {
		src_size = i_size_read(file_inode(in));
		dst_size = i_size_read(file_inode(out));
		if (src_size <= 0 || opos != src_size || dst_size != src_size)
			err = -EIO;
	}

	if (err)
		pr_err("copy %s -> %s failed: %d\n", src, dst, err);
	if (err)
		kfile_remove(out);

	return err;
}

/* A failed copy must not leave a truncated file that the next boot trusts. */
static void __nocfi kfile_remove(struct file *file)
{
	struct dentry *parent;
	struct inode *dir;
	int ret;

	if (!file || !file->f_path.dentry)
		return;

	parent = dget_parent(file->f_path.dentry);
	dir = d_inode(parent);
	if (!dir) {
		dput(parent);
		return;
	}

	inode_lock_nested(dir, I_MUTEX_PARENT);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
	ret = vfs_unlink(mnt_idmap(file->f_path.mnt), dir, file->f_path.dentry, NULL);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
	ret = vfs_unlink(mnt_user_ns(file->f_path.mnt), dir, file->f_path.dentry, NULL);
#else
	ret = vfs_unlink(dir, file->f_path.dentry, NULL);
#endif
	inode_unlock(dir);
	dput(parent);

	if (ret && ret != -ENOENT)
		pr_warn("cannot remove partial %s: %d\n", file->f_path.dentry->d_name.name, ret);
}

static int __nocfi get_task_cmdline(struct task_struct *task, char *buffer, int buflen)
{
	unsigned long arg_start, arg_end, len;
	int res = 0;
	struct mm_struct *mm __free(nksu_mmput) = get_task_mm(task);

	if (!mm)
		return 0;

	mmap_read_lock(mm);
	arg_start = mm->arg_start;
	arg_end = mm->arg_end;
	mmap_read_unlock(mm);

	len = arg_end - arg_start;
	if (len > buflen - 1)
		len = buflen - 1;

	if (len > 0) {
		res = access_process_vm(task, arg_start, buffer, len, 0);
		if (res > 0)
			buffer[res] = '\0';
		else
			buffer[0] = '\0';
	}

	return res;
}

/* Cap the thread-group snapshot; the process list is far smaller. */
#define NKSU_ZYGOTE_SNAPSHOT_MAX 4096

static int mark_zygote(void)
{
	struct task_struct *p;
	int n = 0, i;
	char *cmdline_buf __free(nksu_kfree) = kmalloc(256, GFP_KERNEL);

	if (!cmdline_buf)
		return -ENOMEM;

	struct task_struct **tasks __free(nksu_kfree) =
		kmalloc_array(NKSU_ZYGOTE_SNAPSHOT_MAX, sizeof(*tasks), GFP_KERNEL);
	if (!tasks)
		return -ENOMEM;

	/*
	 * get_task_cmdline() sleeps (it takes mmap_lock and calls
	 * access_process_vm()), so it must not run under rcu_read_lock() -- that
	 * warns "Voluntary context switch within RCU read-side critical section".
	 * Pin every candidate under RCU, then read the cmdlines after unlocking.
	 *
	 * The tracepoint flag is only ever set (and new threads inherit it), so a
	 * process already carrying it needs no re-check: skip it here and never
	 * pay for its cmdline copy.  This makes every scan after the first one
	 * touch only newly created processes.
	 */
	rcu_read_lock();
	for_each_process(p) {
		if (p->flags & PF_KTHREAD)
			continue;
		if (test_tsk_thread_flag(p, TIF_SYSCALL_TRACEPOINT))
			continue;
		if (n == NKSU_ZYGOTE_SNAPSHOT_MAX)
			break;
		get_task_struct(p);
		tasks[n++] = p;
	}
	rcu_read_unlock();

	for (i = 0; i < n; i++) {
		pid_t pid;
		uid_t uid;
		const char *label;

		p = tasks[i];
		pid = p->pid;
		rcu_read_lock();
		uid = __kuid_val(task_uid(p));
		rcu_read_unlock();

		/*
		 * Refresh an already-running authorized process: the tracepoint
		 * flag can be cleared by other tracing users between scans, so
		 * its absence does not prove this process is irrelevant.
		 *
		 * App workers may also be pre-forked (USAP on devices where it
		 * is enabled).  Match their short comm first so a failed cmdline
		 * read cannot silently leave the whole app pool unmarked.
		 */
		if (is_manager_uid(uid) || nksu_profile_has_uid(uid)) {
			label = "authorized process";
		} else if (!strncmp(p->comm, "zygote", 6) || !strncmp(p->comm, "usap", 4) ||
			   strstr(p->comm, "app_process")) {
			label = p->comm;
		} else {
			if (get_task_cmdline(p, cmdline_buf, 256) <= 0) {
				pr_warn_ratelimited("cannot read cmdline for pid=%d uid=%u\n", pid,
						    uid);
				goto next;
			}
			if (strncmp(cmdline_buf, "zygote", 6) != 0 &&
			    strncmp(cmdline_buf, "usap", 4) != 0 &&
			    !strstr(cmdline_buf, "app_process"))
				goto next;
			label = cmdline_buf;
		}

		nksu_mark_threads_by_pid(pid);
		pr_info(": marked %s (pid=%d, uid=%u)\n", label, pid, uid);
	next:
		put_task_struct(p);
	}

	return 0;
}

/*
 * /data/app walk used by the bootstrap.
 *
 * PackageManager extracts libncore.so next to the APK at install time, so it is
 * already present before the manager has ever run.  The layout depends on the
 * Android version (legacy /data/app/<pkg>/lib/<abi>/ and the split
 * /data/app/~~<id>==/<pkg>-<id>==/lib/<abi>/), hence a depth-limited recursive
 * walk that matches the file name wherever it appears.  The signature, not the
 * path, decides which image is trusted.
 */
#define NKSU_APP_SCAN_MAX_DEPTH 6
#define NKSU_APP_SCAN_NAME_MAX 255
#define NKSU_APP_SCAN_BUF_MIN 8192
#define NKSU_APP_SCAN_BUF_MAX (32 * 1024)
#define NKSU_APP_PATH_MAX 512

/* One directory's entries: [u8 d_type][name][NUL], packed back to back. */
struct nksu_dir_list {
	struct dir_context ctx;
	char *names;
	size_t len;
	size_t cap;
	bool truncated;
};

struct nksu_app_scan {
	char path[NKSU_APP_PATH_MAX];
	char found[NKSU_APP_PATH_MAX];
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0)
static bool nksu_dir_list_actor(struct dir_context *ctx, const char *name, int namelen, loff_t pos,
				u64 ino, unsigned int d_type)
{
#else
static int nksu_dir_list_actor(struct dir_context *ctx, const char *name, int namelen, loff_t pos,
			       u64 ino, unsigned int d_type)
{
#endif
	struct nksu_dir_list *list = container_of(ctx, struct nksu_dir_list, ctx);

	(void)pos;
	(void)ino;

	if (namelen <= 0 || namelen > NKSU_APP_SCAN_NAME_MAX)
		return false;
	if (list->len + (size_t)namelen + 2 > list->cap) {
		list->truncated = true;
		return true;
	}

	list->names[list->len++] = (char)d_type;
	memcpy(list->names + list->len, name, namelen);
	list->len += namelen;
	list->names[list->len++] = '\0';
	return false;
}

/*
 * Collect one directory's entries.  The callback cannot verify or copy a
 * candidate itself: it runs while iterate_dir() holds the directory's inode
 * lock and the image check reads (and sleeps on) another file.  Retry with a
 * larger buffer when the directory does not fit, and fail loudly instead of
 * silently skipping entries when even the largest one is too small.
 */
static int kdir_list(const char *dir, char **names_out, size_t *len_out)
{
	size_t cap = NKSU_APP_SCAN_BUF_MIN;
	int ret;

	for (;;) {
		struct nksu_dir_list list = {
			.ctx.actor = nksu_dir_list_actor,
		};
		struct file *file __free(nksu_filp) = NULL;
		char *names = kmalloc(cap, GFP_KERNEL);

		if (!names)
			return -ENOMEM;

		file = filp_open(dir, O_RDONLY | O_DIRECTORY, 0);
		if (IS_ERR(file)) {
			ret = PTR_ERR(file);
			kfree(names);
			return ret;
		}

		list.names = names;
		list.cap = cap;
		ret = iterate_dir(file, &list.ctx);
		if (ret) {
			kfree(names);
			return ret;
		}
		if (!list.truncated) {
			*names_out = names;
			*len_out = list.len;
			return 0;
		}

		kfree(names);
		if (cap >= NKSU_APP_SCAN_BUF_MAX) {
			pr_err("%s has more entries than a %zu byte buffer holds\n", dir, cap);
			return -ENOSPC;
		}
		cap *= 2;
	}
}

/*
 * d_type is only a hint (DT_UNKNOWN on filesystems that do not fill it in), so
 * fall back to a lookup.  Symlinks are not followed: the scan stays inside
 * /data/app.
 */
static bool kpath_is_dir(const char *path, unsigned int d_type)
{
	struct path p;

	if (d_type == DT_DIR)
		return true;
	if (d_type != DT_UNKNOWN)
		return false;
	if (kern_path(path, LOOKUP_DIRECTORY, &p))
		return false;
	path_put(&p);
	return true;
}

/*
 * Install the image at scan->path if it is a signed ncore.
 *
 * Returns 1 when the image was installed, 0 when the candidate is not a signed
 * ncore (skip it and keep looking) and a negative errno when the copy failed
 * after verification.
 */
static int nksu_app_try_install(struct nksu_app_scan *scan, const char *dst)
{
	struct file *file __free(nksu_filp) = filp_open(scan->path, O_RDONLY, 0);
	int ret;

	if (IS_ERR(file))
		return 0;

	ret = nksu_ncore_verify_file(file, scan->path);
	if (ret)
		return 0;

	/*
	 * Only a verified image is worth the directories ncore expects next to
	 * itself.  Create them before the copy so a failed copy cannot leave the
	 * binary without its tree.
	 */
	ret = kdir_mkdir_p("/data/adb/nksu");
	if (ret && ret != -EEXIST) {
		pr_err("mkdir /data/adb/nksu failed: %d\n", ret);
		return ret;
	}
	ret = kdir_mkdir_p(NKSU_DAEMON_BIN_DIR);
	if (ret && ret != -EEXIST) {
		pr_err("mkdir %s failed: %d\n", NKSU_DAEMON_BIN_DIR, ret);
		return ret;
	}

	ret = kfile_copy(file, scan->path, dst);
	if (ret)
		return ret;

	strscpy(scan->found, scan->path, sizeof(scan->found));
	return 1;
}

static int nksu_app_scan_dir(struct nksu_app_scan *scan, size_t len, int depth, const char *dst)
{
	char *names = NULL;
	size_t names_len = 0;
	size_t off = 0;
	int ret;

	ret = kdir_list(scan->path, &names, &names_len);
	if (ret) {
		/* A package can be removed while the walk is in progress. */
		if (depth && (ret == -ENOENT || ret == -ENOTDIR))
			return 0;
		return ret;
	}

	while (off < names_len) {
		unsigned int d_type = (unsigned char)names[off];
		const char *name = names + off + 1;
		size_t name_len = strlen(name);

		off += name_len + 2;

		if (!strcmp(name, ".") || !strcmp(name, ".."))
			continue;
		if (len + name_len + 2 > sizeof(scan->path)) {
			ret = -ENAMETOOLONG;
			goto out;
		}

		scan->path[len] = '/';
		memcpy(scan->path + len + 1, name, name_len + 1);

		if (!strcmp(name, NKSU_NCORE_LIB_NAME)) {
			ret = nksu_app_try_install(scan, dst);
			if (ret)
				goto out;
		} else if (depth < NKSU_APP_SCAN_MAX_DEPTH && kpath_is_dir(scan->path, d_type)) {
			ret = nksu_app_scan_dir(scan, len + name_len + 1, depth + 1, dst);
			if (ret)
				goto out;
		}
	}

	ret = 0;
out:
	kfree(names);
	scan->path[len] = '\0';
	return ret;
}

/*
 * Find a signed libncore.so under /data/app and install it at @dst.
 *
 * Returns 0 when an image was installed, -ENOENT when nothing verified (the
 * manager has not been installed yet, which is normal on a fresh boot) and a
 * negative errno for a real failure.
 */
static int nksu_app_install_ncore(const char *dst)
{
	struct nksu_app_scan *scan __free(nksu_kfree) = kzalloc(sizeof(*scan), GFP_KERNEL);
	int ret;

	if (!scan)
		return -ENOMEM;

	strscpy(scan->path, NKSU_APP_DIR, sizeof(scan->path));
	ret = nksu_app_scan_dir(scan, strlen(scan->path), 0, dst);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return -ENOENT;

	pr_info("installed ncore from %s\n", scan->found);
	return 0;
}

/*
 * Serialize bootstraps: the initial one from ncore_boot_init() and one
 * task_work per packages.xml change can overlap.
 */
static DEFINE_MUTEX(bootstrap_mutex);

static void __nocfi pkgwatch_observer_exit(void);
/*
 * Install /data/adb/ncore from the signed libncore.so PackageManager extracted
 * under /data/app.  The image is only trusted after its appended signature
 * verifies, and the whole operation runs with a cred borrowed from the nksu
 * domain because the triggering context (a kthread, or system_server's rename
 * task_work) cannot reach /data itself.
 */
static int ncore_bootstrap(void)
{
	const struct cred *old;
	int ret = 0;

	guard(nksu_mutex)(&bootstrap_mutex);

	old = nksu_borrow_creds_begin();
	if (!old) {
		pr_err("cannot borrow credentials for the ncore bootstrap\n");
		return -ENOMEM;
	}

	if (kfile_exists(NKSU_DAEMON_BOOT_PATH)) {
		pr_info("ncore exists, skip!");
		WRITE_ONCE(daemon_ready, true);
		WRITE_ONCE(pkgwatch_shutdown, true);
		pkgwatch_observer_exit();
		goto out;
	}

	ret = nksu_app_install_ncore(NKSU_DAEMON_BOOT_PATH);
	if (ret == -ENOENT) {
		pr_info("no signed %s under %s yet\n", NKSU_NCORE_LIB_NAME, NKSU_APP_DIR);
		goto out;
	}
	if (ret) {
		pr_err("cannot install ncore from %s: %d\n", NKSU_APP_DIR, ret);
		goto out;
	}

	/* Label it nksu_file so init's injected init.rc can exec it. */
	nksu_relabel_path(NKSU_DAEMON_BOOT_PATH);
	WRITE_ONCE(daemon_ready, true);

out:
	nksu_borrow_creds_end(old);

	/*
	 * No ncore means no module can run this boot, so volume-key safe mode
	 * has nothing to protect.  Idempotent; the safe-mode watcher is armed
	 * earlier (second_stage), so this simply stops it counting.
	 */
	if (!READ_ONCE(daemon_ready))
		nksu_safe_mode_disable();

	return ret;
}

/*
 * packages.xml observer.
 *
 * PackageManagerService republishes /data/system/packages.xml whenever the set
 * of installed packages changes.  It normally writes a reserve copy and renames
 * it into place; some boot paths instead rewrite the file directly.  Watch the
 * completed rename and close-for-write events rather than FS_CREATE: a create
 * can fire while the writer still has an empty or partial file.  The callback
 * runs under the fsnotify SRCU lock and the rename's directory locks, while the
 * bootstrap sleeps and reads files, so the work is deferred with task_work:
 * it then runs in the renamer's context and finishes before the rename
 * returns.
 *
 * packages.xml is also the earliest reliable "/data is mounted" signal, since
 * it is rewritten during boot before zygote starts.
 */

static void pkgwatch_work_fn(struct work_struct *work)
{
	if (!READ_ONCE(pkgwatch_shutdown))
		ncore_bootstrap();
	module_put(THIS_MODULE);
}

static DECLARE_WORK(pkgwatch_work, pkgwatch_work_fn);

static void pkgwatch_task_work(struct callback_head *cb)
{
	/* Runs after exit_fs() if the task is exiting; path lookups would oops. */
	if (!READ_ONCE(pkgwatch_shutdown) && !(current->flags & PF_EXITING))
		ncore_bootstrap();

	kfree(cb);
	module_put(THIS_MODULE);
}

static void pkgwatch_defer_bootstrap(void)
{
	struct callback_head *cb;

	if (READ_ONCE(pkgwatch_shutdown))
		return;

	if (!try_module_get(THIS_MODULE)) {
		pr_warn_ratelimited("cannot pin module for the ncore bootstrap\n");
		return;
	}

	if (!(current->flags & PF_KTHREAD)) {
		/*
		 * The fsnotify callback may run under rename locks.  Allocate
		 * atomically and never run the sleeping bootstrap inline; on
		 * failure fall back to the workqueue, which can be cancelled at
		 * exit.
		 */
		cb = kzalloc(sizeof(*cb), GFP_ATOMIC);
		if (cb) {
			init_task_work(cb, pkgwatch_task_work);
			if (!task_work_add(current, cb, TWA_RESUME))
				return;
			kfree(cb);
		}
	}

	/* kthread, allocation failure or exiting task: defer off this context. */
	if (!schedule_work(&pkgwatch_work)) {
		pr_debug("ncore bootstrap already pending\n");
		module_put(THIS_MODULE);
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
static int pkgwatch_handle_inode_event(struct fsnotify_mark *mark, u32 mask, struct inode *inode,
				       struct inode *dir, const struct qstr *file_name, u32 cookie)
{
	(void)mark;
	(void)mask;
	(void)inode;
	(void)dir;
	(void)cookie;

	if (!file_name || file_name->len != sizeof(PKGWATCH_NAME) - 1)
		return 0;
	if (memcmp(file_name->name, PKGWATCH_NAME, sizeof(PKGWATCH_NAME) - 1))
		return 0;

	pkgwatch_defer_bootstrap();
	return 0;
}

static const struct fsnotify_ops pkgwatch_ops = {
	.handle_inode_event = pkgwatch_handle_inode_event,
};
#else
/* 4.9 has the old group-wide handle_event callback and group-owned marks. */
static void pkgwatch_free_mark(struct fsnotify_mark *mark)
{
	kfree(mark);
}

static int pkgwatch_handle_event(struct fsnotify_group *group, struct inode *inode,
				 struct fsnotify_mark *inode_mark,
				 struct fsnotify_mark *vfsmount_mark, u32 mask, void *data,
				 int data_type, const unsigned char *file_name, u32 cookie)
{
	(void)group;
	(void)inode;
	(void)inode_mark;
	(void)vfsmount_mark;
	(void)mask;
	(void)data;
	(void)data_type;
	(void)cookie;

	if (!file_name || strlen((const char *)file_name) != sizeof(PKGWATCH_NAME) - 1)
		return 0;
	if (memcmp(file_name, PKGWATCH_NAME, sizeof(PKGWATCH_NAME) - 1))
		return 0;

	pkgwatch_defer_bootstrap();
	return 0;
}

static const struct fsnotify_ops pkgwatch_ops = {
	.handle_event = pkgwatch_handle_event,
};
#endif

static void __nocfi pkgwatch_observer_exit(void)
{
	if (pkgwatch_mark) {
		fsnotify_destroy_mark(pkgwatch_mark, pkgwatch_group);
		fsnotify_put_mark(pkgwatch_mark);
		pkgwatch_mark = NULL;
	}
	if (pkgwatch_group) {
		fsnotify_put_group(pkgwatch_group);
		pkgwatch_group = NULL;
	}
}

static int __nocfi pkgwatch_observer_init(void)
{
	const struct cred *old;
	struct fsnotify_mark *mark;
	struct path kpath;
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	pkgwatch_group = fsnotify_alloc_group(&pkgwatch_ops, 0);
#else
	pkgwatch_group = fsnotify_alloc_group(&pkgwatch_ops);
#endif
	if (IS_ERR(pkgwatch_group)) {
		ret = PTR_ERR(pkgwatch_group);
		pkgwatch_group = NULL;
		return ret;
	}

	/* /data/system is only reachable under nksu's domain + full caps. */
	old = nksu_borrow_creds_begin();
	if (!old) {
		ret = -ENOMEM;
		goto out_group;
	}
	ret = kern_path(PKGWATCH_DIR, LOOKUP_FOLLOW, &kpath);
	nksu_borrow_creds_end(old);
	if (ret)
		goto out_group;

	mark = kzalloc(sizeof(*mark), GFP_KERNEL);
	if (!mark) {
		ret = -ENOMEM;
		path_put(&kpath);
		goto out_group;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	fsnotify_init_mark(mark, pkgwatch_group);
#else
	fsnotify_init_mark(mark, pkgwatch_free_mark);
#endif
	mark->mask = PKGWATCH_MASK;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	ret = nksu_fsnotify_add_inode_mark(mark, d_inode(kpath.dentry), 0);
#else
	ret = nksu_fsnotify_add_mark(mark, pkgwatch_group, d_inode(kpath.dentry), NULL, 0);
#endif
	path_put(&kpath);
	if (ret) {
		fsnotify_put_mark(mark);
		goto out_group;
	}

	pkgwatch_mark = mark;
	pr_info("watching %s/%s\n", PKGWATCH_DIR, PKGWATCH_NAME);
	return 0;

out_group:
	pr_warn("package observer unavailable (%d)\n", ret);
	fsnotify_put_group(pkgwatch_group);
	pkgwatch_group = NULL;
	return ret;
}

int ncore_boot_init(void)
{
	int ret;

	pr_info("ncore bootstrap starting\n");

	/*
	 * Install the observer first, then try once.  A first-stage
	 * (vendor_boot) load can reach this before PackageManagerService has
	 * written packages.xml; the observer then triggers the bootstrap as
	 * soon as the file appears.
	 */
	if (pkgwatch_observer_init())
		pr_warn("package observer unavailable, booting once\n");

	ret = ncore_bootstrap();
	if (ret)
		pr_err("initial ncore bootstrap failed: %d\n", ret);
	return 0;
}

void ncore_boot_exit(void)
{
	WRITE_ONCE(pkgwatch_shutdown, true);
	pkgwatch_observer_exit();
	if (cancel_work_sync(&pkgwatch_work))
		module_put(THIS_MODULE);
	nksu_borrow_creds_release();
}

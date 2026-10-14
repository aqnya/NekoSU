#include "klog.h"
#include "manager/manager.h"
#include "manager/abx.h"
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
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/atomic.h>
#include <linux/fsnotify_backend.h>
#include <linux/task_work.h>
#include <linux/cred.h>
#include <linux/capability.h>
#include <fmac.h>

/* Last: redirects vfs_mkdir/lookup_one_len to the resolved-symbol pointers. */
#include "symbol/symbol_compat.h"

/*
 * vfs_mkdir()/lookup_one_len() are reached through resolved pointers, so
 * disable CFI for this file: the type-hash the module emits for the indirect
 * calls may differ from the running kernel's.
 */
#if defined(__clang__)
#pragma clang attribute push(__attribute__((no_sanitize("cfi"))), apply_to=function)
#endif

#define TARGET_PACKAGE "me.nekosu.aqnya"
#define TARGET_HASH                                                                                                    \
    "\xd8\x09\xb6\x34\x80\xbe\xea\x60\x86\xcd\x78\x68\xe4\x2f\x2e\xb2\xb9\x20\x2c\x67\x0c\x0d\x3d\xb0\x89\x4e\x9e\x43\x12\xf1\x28\xe9"

#define PACKAGES_XML_PATH "/data/system/packages.xml"
#define MAX_PACKAGES_XML_SIZE (8 * 1024 * 1024)

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
 * Whether /data/adb/nksu/ncore is known to be present.  Set by the (sleepable)
 * manager scan; read by the su tracepoint hook, which must not sleep.
 */
static bool ncore_ready;

bool is_ncore_ready(void)
{
    return READ_ONCE(ncore_ready);
}

/* The ABX (Android Binary XML) parser lives in manager/abx.c. */

static int sha256_bytes(const u8 *data, size_t len, u8 out[32])
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;
    int ret = -1;

    tfm = crypto_alloc_shash("sha256", 0, 0);
    if (IS_ERR(tfm))
        return -1;

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (desc) {
        desc->tfm = tfm;
        if (crypto_shash_init(desc) == 0 && crypto_shash_update(desc, data, len) == 0 &&
            crypto_shash_final(desc, out) == 0)
            ret = 0;
        kfree(desc);
    }
    crypto_free_shash(tfm);
    return ret;
}

/*
 * Verify the manager's signing certificate in /data/system/packages.xml and
 * capture its on-disk locations.  The ABX parsing lives in manager/abx.c; here
 * we only hash the certificate it returns and compare it with TARGET_HASH.
 * nativeLibraryPath is the extracted jniLibs directory (<codePath>/lib/<abi>);
 * codePath is the fallback for images that do not write it.
 */
static bool verify_package_signature(struct abx_package *paths)
{
    struct file *fp;
    loff_t pos = 0;
    ssize_t rd;
    size_t fsize;
    u8 *buf;
    bool valid = false;

    fp = filp_open(PACKAGES_XML_PATH, O_RDONLY, 0);
    if (IS_ERR(fp))
        return false;

    fsize = i_size_read(fp->f_inode);
    if (fsize < 4 || fsize > MAX_PACKAGES_XML_SIZE) {
        filp_close(fp, NULL);
        return false;
    }

    buf = kvmalloc(fsize, GFP_KERNEL);
    if (!buf) {
        filp_close(fp, NULL);
        return false;
    }

    rd = kernel_read(fp, buf, fsize, &pos);
    filp_close(fp, NULL);
    if (rd != (ssize_t)fsize)
        goto out_free;

    if (abx_find_package(buf, fsize, TARGET_PACKAGE, paths)) {
        u8 hash[32];

        if (sha256_bytes(paths->cert, paths->cert_len, hash) == 0 &&
            !memcmp(hash, TARGET_HASH, 32))
            valid = true;
    }

out_free:
    kvfree(buf);
    return valid;
}

static uid_t get_uid_from_packages_list(const char *package_name)
{
    struct file *file;
    char *buf, *line, *p, *token;
    loff_t pos = 0;
    uid_t target_uid = (uid_t)-1;
    size_t fsize;
    ssize_t read_size;

    file = filp_open("/data/system/packages.list", O_RDONLY, 0);
    if (IS_ERR(file))
        return (uid_t)-1;

    fsize = i_size_read(file->f_inode);
    if (fsize == 0 || fsize > MAX_PACKAGES_XML_SIZE) {
        filp_close(file, NULL);
        return (uid_t)-1;
    }

    /* The list can exceed one read buffer; read it whole. */
    buf = kvmalloc(fsize + 1, GFP_KERNEL);
    if (!buf) {
        filp_close(file, NULL);
        return (uid_t)-1;
    }

    read_size = kernel_read(file, buf, fsize, &pos);
    filp_close(file, NULL);
    if (read_size <= 0) {
        kvfree(buf);
        return (uid_t)-1;
    }
    buf[read_size] = '\0';

    p = buf;
    while ((line = strsep(&p, "\n")) != NULL) {
        token = strsep(&line, " ");
        if (token && strcmp(token, package_name) == 0) {
            token = strsep(&line, " ");
            if (token && kstrtouint(token, 10, &target_uid) == 0)
                break;
        }
    }

    kvfree(buf);
    return target_uid;
}

static int get_task_cmdline(struct task_struct *task, char *buffer, int buflen)
{
    struct mm_struct *mm;
    unsigned long arg_start, arg_end, len;
    int res = 0;

    mm = get_task_mm(task);
    if (!mm)
        return 0;

    down_read(&mm->mmap_lock);
    arg_start = mm->arg_start;
    arg_end = mm->arg_end;
    up_read(&mm->mmap_lock);

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

    mmput(mm);
    return res;
}

/* Cap the thread-group snapshot; the process list is far smaller. */
#define NKSU_ZYGOTE_SNAPSHOT_MAX 4096

static int mark_zygote(void)
{
    struct task_struct *p, **tasks;
    char *cmdline_buf;
    int n = 0, i;

    cmdline_buf = kmalloc(256, GFP_KERNEL);
    if (!cmdline_buf)
        return -ENOMEM;

    tasks = kmalloc_array(NKSU_ZYGOTE_SNAPSHOT_MAX, sizeof(*tasks),
                          GFP_KERNEL);
    if (!tasks) {
        kfree(cmdline_buf);
        return -ENOMEM;
    }

    /*
     * get_task_cmdline() sleeps (it takes mmap_lock and calls
     * access_process_vm()), so it must not run under rcu_read_lock() -- that
     * warns "Voluntary context switch within RCU read-side critical section".
     * Pin every candidate under RCU, then read the cmdlines after unlocking.
     */
    rcu_read_lock();
    for_each_process(p) {
        if (p->flags & PF_KTHREAD)
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

        p = tasks[i];
        if (get_task_cmdline(p, cmdline_buf, 256) <= 0)
            goto next;

        if (strncmp(cmdline_buf, "zygote", 6) != 0 &&
            strncmp(cmdline_buf, "zygote64", 8) != 0 &&
            !strstr(cmdline_buf, "app_process"))
            goto next;

        pid = p->pid;
        rcu_read_lock();
        uid = __kuid_val(task_uid(p));
        rcu_read_unlock();

        mark_threads_by_pid(pid);
        pr_info("[manager] : marked %s (pid=%d, uid=%u)\n", cmdline_buf, pid,
                uid);
next:
        put_task_struct(p);
    }

    kfree(tasks);
    kfree(cmdline_buf);
    return 0;
}

/*
 * The scan runs from a kthread, i.e. in the kernel SELinux domain, which is
 * not allowed to read /data/system, so it runs with a cred borrowed from the
 * nksu domain via nksu_borrow_creds_begin().
 */

/*
 * Bootstrap /data/adb/nksu/{ncore,bin/busybox} straight from the verified
 * manager package, so root works on the very first boot without the manager
 * having to run `ncore install` from a root shell it does not have yet.
 *
 * The manager ships both binaries as jniLibs (libncore.so and libbusybox.so);
 * the kernel only sees the extracted nativeLibraryPath directory from
 * packages.xml.  This mirrors KernelSU's `is_ksud_exists()` gate and the
 * manager-provided libksud.so, but installs from the kernel so no userspace
 * round-trip is needed.
 */
#define NKSU_NCORE_BOOT_PATH    "/data/adb/nksu/ncore"
#define NKSU_NCORE_BIN_DIR      "/data/adb/nksu/bin"
#define NKSU_NCORE_BUSYBOX_PATH NKSU_NCORE_BIN_DIR "/busybox"
#define NKSU_NCORE_LIB_NAME     "libncore.so"
#define NKSU_NCORE_BUSYBOX_LIB  "libbusybox.so"

static bool kfile_exists(const char *path)
{
    struct file *f = filp_open(path, O_RDONLY, 0);

    if (IS_ERR(f))
        return false;
    filp_close(f, NULL);
    return true;
}

/* mkdir one leaf whose parent already exists. */
static int kdir_mkdir(const char *path, umode_t mode)
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

static int kfile_copy(const char *src, const char *dst)
{
    struct file *in, *out;
    char *buf;
    loff_t ipos = 0, opos = 0;
    ssize_t off;
    int err = 0;

    in = filp_open(src, O_RDONLY, 0);
    if (IS_ERR(in))
        return PTR_ERR(in);

    out = filp_open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (IS_ERR(out)) {
        err = PTR_ERR(out);
        filp_close(in, NULL);
        return err;
    }

    buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
    if (!buf) {
        err = -ENOMEM;
    } else {
        for (;;) {
            ssize_t r = kernel_read(in, buf, PAGE_SIZE, &ipos);

            if (r < 0) {
                err = (int)r;
                break;
            }
            if (r == 0)
                break;

            off = 0;
            while (off < r) {
                ssize_t w = kernel_write(out, buf + off, (size_t)(r - off), &opos);

                if (w < 0) {
                    err = (int)w;
                    break;
                }
                off += w;
            }
            if (err)
                break;
        }
        kfree(buf);
    }

    filp_close(out, NULL);
    filp_close(in, NULL);
    return err;
}

/* codePath is either the package dir or .../base.apk; reduce it to the dir. */
static void code_path_base(const char *code, char *out, size_t cap)
{
    size_t n = strlen(code);

    if (n > 4 && strcmp(code + n - 4, ".apk") == 0) {
        const char *slash = strrchr(code, '/');

        if (slash) {
            n = (size_t)(slash - code);
            if (n >= cap)
                n = cap - 1;
            memcpy(out, code, n);
            out[n] = '\0';
            return;
        }
    }
    strscpy(out, code, cap);
}

static bool resolve_lib_dir(const struct abx_package *paths, char *out, size_t cap)
{
    char cand[320];
    char base[256];

    if (paths->native_lib_path[0]) {
        snprintf(cand, sizeof(cand), "%s/%s", paths->native_lib_path, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            strscpy(out, paths->native_lib_path, cap);
            return true;
        }
    }

    if (paths->code_path[0]) {
        code_path_base(paths->code_path, base, sizeof(base));

        snprintf(cand, sizeof(cand), "%s/lib/arm64/%s", base, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            snprintf(out, cap, "%s/lib/arm64", base);
            return true;
        }
        snprintf(cand, sizeof(cand), "%s/lib/arm/%s", base, NKSU_NCORE_LIB_NAME);
        if (kfile_exists(cand)) {
            snprintf(out, cap, "%s/lib/arm", base);
            return true;
        }
    }
    return false;
}

static int install_ncore_from_manager(const struct abx_package *paths)
{
    char lib_dir[256];
    char src[320];
    int err;

    if (kfile_exists(NKSU_NCORE_BOOT_PATH)) {
        /*
         * Already bootstrapped.  Re-assert the label every boot: an older
         * build created this as adb_data_file, which init cannot execute, and
         * this also repairs a file whose xattr was lost.
         */
        nksu_relabel_path(NKSU_NCORE_BOOT_PATH);
        nksu_relabel_path(NKSU_NCORE_BUSYBOX_PATH);
        WRITE_ONCE(ncore_ready, true);
        return 0;
    }

    if (!resolve_lib_dir(paths, lib_dir, sizeof(lib_dir))) {
        pr_err("[manager] cannot locate %s in the manager install\n", NKSU_NCORE_LIB_NAME);
        return -ENOENT;
    }

    err = kdir_mkdir_p("/data/adb/nksu");
    if (err && err != -EEXIST) {
        pr_err("[manager] mkdir /data/adb/nksu failed: %d\n", err);
        return err;
    }
    err = kdir_mkdir_p(NKSU_NCORE_BIN_DIR);
    if (err && err != -EEXIST) {
        pr_err("[manager] mkdir %s failed: %d\n", NKSU_NCORE_BIN_DIR, err);
        return err;
    }

    snprintf(src, sizeof(src), "%s/%s", lib_dir, NKSU_NCORE_LIB_NAME);
    err = kfile_copy(src, NKSU_NCORE_BOOT_PATH);
    if (err) {
        pr_err("[manager] copy %s -> %s failed: %d\n", src, NKSU_NCORE_BOOT_PATH, err);
        return err;
    }
    /* Label it nksu_file so init's injected init.rc can exec it. */
    nksu_relabel_path(NKSU_NCORE_BOOT_PATH);

    snprintf(src, sizeof(src), "%s/%s", lib_dir, NKSU_NCORE_BUSYBOX_LIB);
    if (kfile_exists(src)) {
        int berr = kfile_copy(src, NKSU_NCORE_BUSYBOX_PATH);

        if (berr)
            pr_warn("[manager] copy busybox failed: %d\n", berr);
        else
            nksu_relabel_path(NKSU_NCORE_BUSYBOX_PATH);
    } else {
        pr_warn("[manager] %s not found, busybox not installed\n", src);
    }

    pr_info("[manager] installed ncore from %s\n", lib_dir);
    WRITE_ONCE(ncore_ready, true);
    return 0;
}

/*
 * Scans can be triggered concurrently: the initial one from appscan_init() and
 * one task_work per packages.xml change.  Serialize them (the scan mutates the
 * profile table and caches the borrowed nksu creds).
 */
static DEFINE_MUTEX(appscan_mutex);

static int scan_and_apply(void)
{
    const struct cred *old;
    struct abx_package paths = { 0 };
    uid_t uid;
    int ret = -1;

    mutex_lock(&appscan_mutex);
    old = nksu_borrow_creds_begin();
    if (!old) {
        mutex_unlock(&appscan_mutex);
        return -ENOMEM;
    }

    uid = get_uid_from_packages_list(TARGET_PACKAGE);
    if (uid == (uid_t)-1) {
        pr_err("[manager] Could not find UID for %s\n", TARGET_PACKAGE);
        goto out;
    }

    if (verify_package_signature(&paths)) {
        pr_info("[manager] Verification passed. "
                "Granting privileges to UID %u\n",
                uid);
        /*
         * Record the UID before granting the profile so the store can leave
         * the manager's own entry out of allow.profile: it is re-created on
         * every boot from this verified scan, never restored from disk.
         */
        manager_kuid = make_kuid(current_user_ns(), uid);
        /* Install ncore/busybox before the profile store writes its file so
         * /data/adb/nksu exists for the first save. */
        install_ncore_from_manager(&paths);
        nksu_profile_set_default(uid);
#ifndef CONFIG_NKSU_SYSCALL
        mark_zygote();
#endif
        ret = 0;
    } else {
        pr_err("[manager] Signature mismatch!\n");
    }

out:
    nksu_borrow_creds_end(old);
    mutex_unlock(&appscan_mutex);

    /*
     * No ncore means no module can run this boot, so volume-key safe mode has
     * nothing to protect.  Idempotent, and safe before the safe-mode component
     * has initialized (the state is observed by nksu_safe_mode_init).
     */
    if (!READ_ONCE(ncore_ready))
        nksu_safe_mode_disable();

    return ret;
}

/*
 * packages.xml observer.
 *
 * PackageManagerService republishes /data/system/packages.xml whenever the set
 * of installed packages changes (it writes a reserve copy and renames it into
 * place), so re-run the manager scan on every create/rename instead of polling.
 * The callback runs under the fsnotify SRCU lock and the rename's directory
 * locks, while the scan sleeps and reads files, so the work is deferred with
 * task_work (KernelSU's ksu_defer_track_throne): it then runs in the renamer's
 * context and finishes before the rename returns, so a freshly installed
 * manager is verified and granted before it can be opened.
 */
#define APPSCAN_WATCH_DIR "/data/system"
#define APPSCAN_WATCH_NAME "packages.xml"
#define APPSCAN_WATCH_MASK (FS_CREATE | FS_MOVE | FS_EVENT_ON_CHILD)

static struct fsnotify_group *appscan_group;
static struct fsnotify_mark *appscan_mark;

static void appscan_task_work(struct callback_head *cb)
{
    kfree(cb);

    /* Runs after exit_fs() if the task is exiting; path lookups would oops. */
    if (current->flags & PF_EXITING)
        return;

    scan_and_apply();
}

static void appscan_defer_scan(void)
{
    struct callback_head *cb;

    if (!(current->flags & PF_KTHREAD)) {
        cb = kzalloc(sizeof(*cb), GFP_KERNEL);
        if (cb) {
            init_task_work(cb, appscan_task_work);
            if (!task_work_add(current, cb, TWA_RESUME))
                return;
            kfree(cb);
        }
    }

    /* kthread or allocation failure: run it inline. */
    scan_and_apply();
}

static int appscan_handle_inode_event(struct fsnotify_mark *mark, u32 mask,
                                      struct inode *inode, struct inode *dir,
                                      const struct qstr *file_name, u32 cookie)
{
    (void)mark;
    (void)mask;
    (void)inode;
    (void)dir;
    (void)cookie;

    if (!file_name || file_name->len != sizeof(APPSCAN_WATCH_NAME) - 1)
        return 0;
    if (memcmp(file_name->name, APPSCAN_WATCH_NAME,
               sizeof(APPSCAN_WATCH_NAME) - 1))
        return 0;

    appscan_defer_scan();
    return 0;
}

static const struct fsnotify_ops appscan_ops = {
    .handle_inode_event = appscan_handle_inode_event,
};

static void appscan_observer_exit(void)
{
    if (appscan_mark) {
        fsnotify_destroy_mark(appscan_mark, appscan_group);
        fsnotify_put_mark(appscan_mark);
        appscan_mark = NULL;
    }
    if (appscan_group) {
        fsnotify_put_group(appscan_group);
        appscan_group = NULL;
    }
}

static int appscan_observer_init(void)
{
    const struct cred *old;
    struct fsnotify_mark *mark;
    struct path kpath;
    int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
    appscan_group = fsnotify_alloc_group(&appscan_ops, 0);
#else
    appscan_group = fsnotify_alloc_group(&appscan_ops);
#endif
    if (IS_ERR(appscan_group)) {
        ret = PTR_ERR(appscan_group);
        appscan_group = NULL;
        return ret;
    }

    /* /data/system is only reachable under nksu's domain + full caps. */
    old = nksu_borrow_creds_begin();
    ret = kern_path(APPSCAN_WATCH_DIR, LOOKUP_FOLLOW, &kpath);
    nksu_borrow_creds_end(old);
    if (ret)
        goto out_group;

    mark = kzalloc(sizeof(*mark), GFP_KERNEL);
    if (!mark) {
        ret = -ENOMEM;
        path_put(&kpath);
        goto out_group;
    }

    fsnotify_init_mark(mark, appscan_group);
    mark->mask = APPSCAN_WATCH_MASK;
    ret = fsnotify_add_inode_mark(mark, d_inode(kpath.dentry), 0);
    path_put(&kpath);
    if (ret) {
        fsnotify_put_mark(mark);
        goto out_group;
    }

    appscan_mark = mark;
    pr_info("[manager] watching %s/%s\n", APPSCAN_WATCH_DIR, APPSCAN_WATCH_NAME);
    return 0;

out_group:
    pr_warn("[manager] package observer unavailable (%d)\n", ret);
    fsnotify_put_group(appscan_group);
    appscan_group = NULL;
    return ret;
}

int appscan_init(void)
{
    pr_info("[manager] Module starting scan...\n");

    /*
     * Install the observer first, then scan once.  A first-stage (vendor_boot)
     * load can reach this before PackageManagerService has written
     * packages.xml; the observer then triggers the scan as soon as it appears,
     * so no retry loop is needed.
     */
    if (appscan_observer_init())
        pr_warn("[manager] package observer unavailable, scanning boot only\n");

    scan_and_apply();
    return 0;
}

void appscan_exit(void)
{
    appscan_observer_exit();
    nksu_borrow_creds_release();
}

#if defined(__clang__)
#pragma clang attribute pop
#endif

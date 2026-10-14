// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Volume-key safe mode.
 *
 * The module watches the volume-up key from early in the boot until ncore
 * reports boot completion.  Three key-down events within that window create
 * /data/adb/nksu/safe_module and reboot the device, so the next boot comes up
 * with every module and stage script skipped (ncore checks the marker in
 * utils::is_safe_mode()).
 *
 * Key detection uses the input subsystem: a small input_handler that matches
 * EV_KEY/KEY_VOLUMEUP devices, opens them, and counts key-down (value == 1)
 * events.  The handler callback runs in the input device's context (possibly
 * atomic), so it only bumps a counter and wakes the worker thread; the thread
 * does the file I/O and the reboot.
 *
 * The file is written while borrowing the nksu domain creds, because a kernel
 * thread's own creds cannot touch /data/adb (see selinux/selinux.h).
 */

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <linux/wait.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>

#include "klog.h"
#include "boot/safe_mode.h"
#include "hook/syscall.h"
#include "manager/manager.h"
#include "manager/ioctl.h"
#include "selinux/selinux.h"

#define NKSU_SAFE_DIR       "/data/adb/nksu"
#define NKSU_SAFE_MARKER    NKSU_SAFE_DIR "/safe_module"
#define NKSU_SAFE_PRESSES   3

static struct task_struct *nksu_safe_thread;
static DECLARE_WAIT_QUEUE_HEAD(nksu_safe_wq);
static DECLARE_COMPLETION(nksu_safe_exited);
static atomic_t nksu_safe_presses = ATOMIC_INIT(0);
/* Set once the watcher is told to stop (boot completed, or no ncore). */
static bool nksu_safe_disarmed __read_mostly;
static bool nksu_safe_input_registered;

/*
 * ncore reports boot completion with prctl(NKSU_PRCTL_BOOT_COMPLETED) from its
 * boot-completed stage.  It runs as root and is not in the uid profile, and the
 * default build's tracepoint dispatcher only traces profile UIDs, so the
 * notification gets its own prctl syscall-slot hook that sees every caller.
 * It is installed last (see core.c) so it wraps the syscall dispatcher in the
 * CONFIG_NKSU_SYSCALL build and the raw prctl otherwise.
 */
static syscall_fn_t nksu_safe_orig_prctl;
static bool nksu_safe_prctl_hooked;

static long nksu_safe_prctl_sys(const struct pt_regs *regs)
{
	unsigned long option;
#if defined(__aarch64__)
	option = regs->regs[0];
#elif defined(__x86_64__)
	option = regs->di;
#else
	option = 0;
#endif

	if (option == NKSU_PRCTL_BOOT_COMPLETED &&
	    (uid_eq(current_euid(), GLOBAL_ROOT_UID) ||
	     uid_eq(current_uid(), GLOBAL_ROOT_UID))) {
		nksu_safe_mode_disable();
		return 1;
	}

	return nksu_safe_orig_prctl ? nksu_safe_orig_prctl(regs) : -ENOSYS;
}

/* ---- input handler: count volume-up key-down events ---- */

static void nksu_safe_events(struct input_handle *handle,
			     const struct input_value *vals, unsigned int count)
{
	unsigned int i;
	bool pressed = false;

	if (READ_ONCE(nksu_safe_disarmed))
		return;

	for (i = 0; i < count; i++) {
		/*
		 * value 1 is a key-down; 2 is autorepeat and 0 is key-up, so a
		 * held key still counts once.
		 */
		if (vals[i].type == EV_KEY && vals[i].code == KEY_VOLUMEUP &&
		    vals[i].value == 1)
			pressed = true;
	}

	if (pressed) {
		atomic_inc(&nksu_safe_presses);
		wake_up_interruptible(&nksu_safe_wq);
	}
}

static int nksu_safe_connect(struct input_handler *handler,
			     struct input_dev *dev,
			     const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "nksu-safe";

	error = input_register_handle(handle);
	if (error)
		goto err_free;

	error = input_open_device(handle);
	if (error)
		goto err_unregister;

	return 0;

err_unregister:
	input_unregister_handle(handle);
err_free:
	kfree(handle);
	return error;
}

static void nksu_safe_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/* Only attach to devices that actually expose a volume-up key. */
static const struct input_device_id nksu_safe_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			 INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_VOLUMEUP)] = BIT_MASK(KEY_VOLUMEUP) },
	},
	{ },
};

static struct input_handler nksu_safe_handler = {
	.events = nksu_safe_events,
	.connect = nksu_safe_connect,
	.disconnect = nksu_safe_disconnect,
	.name = "nksu-safe",
	.id_table = nksu_safe_ids,
};

/* ---- worker: create the marker and reboot ---- */

static void nksu_safe_trigger(void)
{
	const struct cred *old;

	pr_alert("[safe_mode] volume-up pressed %d times, enabling safe mode\n",
		 NKSU_SAFE_PRESSES);

	old = nksu_borrow_creds_begin();
	if (old) {
		struct file *fp;
		loff_t pos = 0;

		nksu_kpath_mkdir_p(NKSU_SAFE_DIR);
		fp = filp_open(NKSU_SAFE_MARKER,
			       O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (IS_ERR(fp)) {
			pr_err("[safe_mode] cannot create %s: %ld\n",
			       NKSU_SAFE_MARKER, PTR_ERR(fp));
		} else {
			static const char msg[] = "nksu safe mode\n";

			kernel_write(fp, msg, sizeof(msg) - 1, &pos);
			vfs_fsync(fp, 0);
			filp_close(fp, NULL);
			pr_info("[safe_mode] created %s\n", NKSU_SAFE_MARKER);
		}
		nksu_borrow_creds_end(old);
	}

	/* Let the write land, then reboot into the safe-mode boot. */
	msleep(500);
	kernel_restart(NULL);
}

static int nksu_safe_thread_fn(void *unused)
{
	for (;;) {
		if (READ_ONCE(nksu_safe_disarmed))
			break;

		if (atomic_read(&nksu_safe_presses) >= NKSU_SAFE_PRESSES) {
			nksu_safe_trigger();
			break;
		}

		wait_event_interruptible(nksu_safe_wq,
			READ_ONCE(nksu_safe_disarmed) ||
			atomic_read(&nksu_safe_presses) >= NKSU_SAFE_PRESSES);
	}

	/*
	 * Signal exit and return: the thread is NOT kthread_stop()ped later, so
	 * it must retire itself (same pattern as the boot watcher in
	 * boot/hook_init.c).  nksu_safe_mode_exit() only waits on the completion.
	 */
	complete(&nksu_safe_exited);
	return 0;
}

/* ---- public interface ---- */

void nksu_safe_mode_disable(void)
{
	if (READ_ONCE(nksu_safe_disarmed))
		return;

	WRITE_ONCE(nksu_safe_disarmed, true);
	wake_up_interruptible(&nksu_safe_wq);
	pr_info("[safe_mode] volume-up watcher disarmed\n");
}

int nksu_safe_mode_init(void)
{
	int ret;

	/*
	 * The manager scan runs before this component and may have already
	 * disarmed the watcher (it found no ncore, so no module can run).  In
	 * that case stay off; the flag is not reset here.
	 */
	if (READ_ONCE(nksu_safe_disarmed))
		return 0;

	atomic_set(&nksu_safe_presses, 0);

	/*
	 * Safe mode is an optional feature: any failure here disables it but
	 * must not abort the rest of the module's bring-up, so every error path
	 * cleans up and returns success.
	 */
	ret = input_register_handler(&nksu_safe_handler);
	if (ret) {
		pr_err("[safe_mode] input handler registration failed: %d\n",
		       ret);
		return 0;
	}
	nksu_safe_input_registered = true;

	if (!syscall_table) {
		ret = syscall_table_resolve();
		if (ret) {
			pr_err("[safe_mode] cannot resolve sys_call_table: %d\n",
			       ret);
			goto disable_input;
		}
	}

	ret = syscall_slot_hook(__NR_prctl, nksu_safe_prctl_sys,
				&nksu_safe_orig_prctl, "nksu_safe_prctl");
	if (ret) {
		pr_err("[safe_mode] prctl hook failed: %d\n", ret);
		goto disable_input;
	}
	nksu_safe_prctl_hooked = true;

	reinit_completion(&nksu_safe_exited);
	nksu_safe_thread = kthread_run(nksu_safe_thread_fn, NULL, "nksu-safe");
	if (IS_ERR(nksu_safe_thread)) {
		pr_err("[safe_mode] worker thread failed: %ld\n",
		       PTR_ERR(nksu_safe_thread));
		nksu_safe_thread = NULL;
		syscall_slot_unhook(__NR_prctl);
		nksu_safe_prctl_hooked = false;
		goto disable_input;
	}

	pr_info("[safe_mode] watching volume-up keys for safe mode\n");
	return 0;

disable_input:
	input_unregister_handler(&nksu_safe_handler);
	nksu_safe_input_registered = false;
	return 0;
}

void nksu_safe_mode_exit(void)
{
	if (nksu_safe_prctl_hooked) {
		syscall_slot_unhook(__NR_prctl);
		nksu_safe_prctl_hooked = false;
	}

	if (nksu_safe_input_registered) {
		input_unregister_handler(&nksu_safe_handler);
		nksu_safe_input_registered = false;
	}

	/*
	 * The watcher retires itself (never kthread_stop()ped); ask it to leave
	 * and wait for its completion so no safe-mode code is still running when
	 * the module is torn down.
	 */
	WRITE_ONCE(nksu_safe_disarmed, true);
	wake_up_interruptible(&nksu_safe_wq);

	if (nksu_safe_thread) {
		wait_for_completion(&nksu_safe_exited);
		nksu_safe_thread = NULL;
	}
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_BOOT_SAFE_MODE_H
#define NKSU_BOOT_SAFE_MODE_H

/*
 * Volume-key safe mode.
 *
 * At boot an input handler counts KEY_VOLUMEUP presses.  Three presses before
 * the boot-completed stage create /data/adb/nksu/safe_module and reboot; on the
 * next boot ncore sees the marker and skips all module and script loading.
 *
 * The watcher stops when ncore reports boot completion, or when the manager
 * scan finds no ncore at all (without it no module can run, so safe mode has
 * nothing to protect).  There is no timeout window.
 */

int  nksu_safe_mode_init(void);
void nksu_safe_mode_exit(void);

/* Stop the volume-key watcher.  Idempotent; safe to call before init (the
 * state is then noticed by nksu_safe_mode_init).  Called by ncore at the
 * boot-completed stage and by the manager scan when ncore is unavailable. */
void nksu_safe_mode_disable(void);

#endif /* NKSU_BOOT_SAFE_MODE_H */

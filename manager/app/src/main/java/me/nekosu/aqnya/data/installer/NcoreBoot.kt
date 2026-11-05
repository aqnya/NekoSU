// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.data.installer

import android.content.Context
import me.nekosu.aqnya.data.shell.RootShell
import me.nekosu.aqnya.util.getAppVersionCode

object NcoreBoot {
    const val NCORE_DIR = "/data/adb/nksu"
    const val BOOT_PATH = "/data/adb/ncore"

    private const val PREFS_NAME = "ncore_boot"
    private const val KEY_INSTALLED_VERSION = "installed_version"

    /**
     * Install the ncore userspace component.  The kernel bootstraps
     * [BOOT_PATH] from the signed libncore.so that PackageManager already
     * extracted under /data/app, so the manager no longer stages a copy of its
     * own.
     */
    fun install(context: Context): Boolean {
        val ncore = VendorBootInstaller.ncorePath(context)
        if (!ncore.exists()) return false

        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val version = getAppVersionCode(context)
        if (version != 0L && prefs.getLong(KEY_INSTALLED_VERSION, -1L) == version) return true

        // Run ncore's own install when the bundled version changes; it
        // extracts the embedded busybox/resetprop assets and links ncore into
        // /data/adb/nksu/bin.  The kernel bootstrap only installs the binary.
        val cmd = "${quote(ncore.absolutePath)} install"
        val ok = RootShell.exec(cmd).code == 0
        if (ok && version != 0L) {
            prefs.edit().putLong(KEY_INSTALLED_VERSION, version).apply()
        }
        return ok
    }

    private fun quote(s: String) = "'" + s.replace("'", "'\\''") + "'"
}

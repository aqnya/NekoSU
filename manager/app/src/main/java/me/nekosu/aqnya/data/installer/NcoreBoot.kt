// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.data.installer

import android.content.Context
import android.util.Log
import me.nekosu.aqnya.data.shell.RootShell
import me.nekosu.aqnya.util.getAppVersionCode
import java.io.File

object NcoreBoot {
    const val NCORE_DIR = "/data/adb/nksu"
    const val BOOT_PATH = "/data/adb/ncore"

    /**
     * The kernel verifies the signature appended to this copy and installs it
     * as [BOOT_PATH].  Keeping it next to the app's own data means the manager
     * needs no root to bootstrap ncore on the next boot.
     */
    private const val STAGED_NAME = "ncore"

    private const val TAG = "ncore_boot"
    private const val PREFS_NAME = "ncore_boot"
    private const val KEY_INSTALLED_VERSION = "installed_version"

    /**
     * Stage the signed libncore.so in the app data directory for the kernel's
     * boot-time signature check.  Copies through a temporary file and renames
     * it into place so the kernel never reads a half-written image.
     */
    fun stage(context: Context): Boolean {
        val source = VendorBootInstaller.ncorePath(context)
        if (!source.isFile) return false

        val target = File(context.dataDir, STAGED_NAME)
        // The APK update rewrites nativeLibraryDir with a newer mtime; skip
        // the copy while both the size and that timestamp match.
        if (target.isFile &&
            target.length() == source.length() &&
            target.lastModified() >= source.lastModified()
        ) {
            return true
        }

        val tmp = File(context.dataDir, "$STAGED_NAME.tmp")
        return try {
            tmp.delete()
            source.inputStream().use { input ->
                tmp.outputStream().use { output -> input.copyTo(output) }
            }
            // The kernel reads it as root, but a root shell may also exec it.
            tmp.setReadable(true, false)
            if (!tmp.renameTo(target)) {
                tmp.copyTo(target, overwrite = true)
                tmp.delete()
            }
            Log.i(TAG, "staged ${target.absolutePath} (${target.length()} bytes)")
            true
        } catch (e: Exception) {
            Log.e(TAG, "cannot stage ncore: ${e.message}")
            tmp.delete()
            false
        }
    }

    fun install(context: Context): Boolean {
        // Always refresh the kernel-visible copy first; this works without
        // root and is what the boot-time bootstrap consumes.
        stage(context)

        val ncore = VendorBootInstaller.ncorePath(context)
        if (!ncore.exists()) return false

        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val version = getAppVersionCode(context)
        if (version != 0L && prefs.getLong(KEY_INSTALLED_VERSION, -1L) == version) return true

        // With external root available, also install immediately and extract
        // the embedded busybox/resetprop assets; the kernel bootstrap is the
        // fallback for devices without it.
        val cmd = "${quote(ncore.absolutePath)} install"
        val ok = RootShell.exec(cmd).code == 0
        if (ok && version != 0L) {
            prefs.edit().putLong(KEY_INSTALLED_VERSION, version).apply()
        }
        return ok
    }

    private fun quote(s: String) = "'" + s.replace("'", "'\\''") + "'"
}

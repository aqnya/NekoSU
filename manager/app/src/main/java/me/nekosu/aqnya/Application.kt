// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya

import android.app.Application
import android.content.Context
import me.nekosu.aqnya.core.NcoreLoader
import me.nekosu.aqnya.data.installer.NcoreBoot
import me.nekosu.aqnya.data.shell.RootShell
import me.nekosu.aqnya.util.CrashHandler
import me.nekosu.aqnya.util.LocaleHelper

class Application : Application() {
    override fun attachBaseContext(base: Context) {
        super.attachBaseContext(LocaleHelper.wrap(base, LocaleHelper.savedLanguageTag(base)))
    }

    override fun onCreate() {
        super.onCreate()
        CrashHandler.init(this)
        NcoreLoader.init()
        // Keep the signed ncore copy fresh for the kernel's boot-time check.
        // It is I/O, so do it off the main thread.
        Thread({ NcoreBoot.stage(this) }, "ncore-stage").apply { isDaemon = true }.start()
        RootShell.startDetection()
    }
}

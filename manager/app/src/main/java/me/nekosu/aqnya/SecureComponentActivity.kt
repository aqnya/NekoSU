// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya

import android.content.Context
import android.content.SharedPreferences
import android.os.Bundle
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import me.nekosu.aqnya.data.prefs.AppPreferences

/**
 * Common entry point for activities that can enable Magisk-style tapjacking
 * protection. Hiding untrusted overlays is the first line of defence; filtering
 * obscured motion events also covers overlays the system still reports to us.
 * The preference can be toggled while the activity is running.
 */
abstract class SecureComponentActivity : ComponentActivity() {
    private val prefs by lazy {
        getSharedPreferences(AppPreferences.PREF_NAME, Context.MODE_PRIVATE)
    }
    private var tapjackingProtection = true

    private val preferenceListener =
        SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            if (key == AppPreferences.KEY_TAPJACKING_PROTECTION) {
                applyTapjackingProtection()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        applyTapjackingProtection()
    }

    override fun onStart() {
        super.onStart()
        prefs.registerOnSharedPreferenceChangeListener(preferenceListener)
        applyTapjackingProtection()
    }

    override fun onStop() {
        prefs.unregisterOnSharedPreferenceChangeListener(preferenceListener)
        super.onStop()
    }

    override fun dispatchTouchEvent(ev: MotionEvent): Boolean {
        if (tapjackingProtection && (ev.flags and OBSCURED_FLAGS) != 0) {
            if (ev.actionMasked == MotionEvent.ACTION_DOWN) {
                Log.w(TAG, "blocked touch while the window is obscured")
            }
            return true
        }
        return super.dispatchTouchEvent(ev)
    }

    private fun applyTapjackingProtection() {
        val update = {
            tapjackingProtection = prefs.getBoolean(AppPreferences.KEY_TAPJACKING_PROTECTION, true)
            window.setHideOverlayWindows(tapjackingProtection)
        }
        if (Looper.myLooper() == Looper.getMainLooper()) {
            update()
        } else {
            runOnUiThread { update() }
        }
    }

    private companion object {
        const val TAG = "NekosuTapGuard"
        const val OBSCURED_FLAGS =
            MotionEvent.FLAG_WINDOW_IS_OBSCURED or MotionEvent.FLAG_WINDOW_IS_PARTIALLY_OBSCURED
    }
}

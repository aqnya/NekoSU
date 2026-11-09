// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.component

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import me.nekosu.aqnya.data.prefs.AppPreferences

/**
 * Applies the Magisk-style `filterTouchesWhenObscured` flag to the current
 * window. Activity windows are handled by SecureComponentActivity; dialogs
 * have their own window and need this effect from their content.
 */
@Composable
fun TapjackingProtectionEffect() {
    val context = LocalContext.current
    val enabled by AppPreferences
        .tapjackingProtectionFlow(context)
        .collectAsState(initial = AppPreferences.isTapjackingProtectionEnabled(context))
    val view = LocalView.current

    LaunchedEffect(view, enabled) {
        view.filterTouchesWhenObscured = enabled
    }
}

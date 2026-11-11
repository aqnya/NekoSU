// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import kotlinx.coroutines.launch
import me.nekosu.aqnya.R
import me.nekosu.aqnya.data.prefs.AppPreferences
import me.nekosu.aqnya.ui.screens.settings.enums.NavBarStyle
import me.nekosu.aqnya.ui.screens.settings.enums.ThemeColor
import me.nekosu.aqnya.ui.screens.settings.enums.ThemeMode

@Composable
fun AppearanceSettingsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    val themeModeValue by AppPreferences.themeModeFlow(context).collectAsState(
        initial = AppPreferences.themeMode(context),
    )
    val themeColorValue by AppPreferences.themeColorFlow(context).collectAsState(
        initial = AppPreferences.themeColor(context),
    )
    val amoledEnabled by AppPreferences.amoledFlow(context).collectAsState(
        initial = AppPreferences.isAmoledEnabled(context),
    )
    val blurEnabled by AppPreferences.blurFlow(context).collectAsState(
        initial = AppPreferences.isBlurEnabled(context),
    )
    val navBarStyleValue by AppPreferences.navBarStyleFlow(context).collectAsState(
        initial = AppPreferences.navBarStyle(context),
    )

    SettingsDetailScaffold(
        title = stringResource(R.string.theme_title),
        onBack = onBack,
    ) {
        AppearanceSection(
            currentThemeMode = ThemeMode.fromValue(themeModeValue),
            currentThemeColor = ThemeColor.fromValue(themeColorValue),
            amoledEnabled = amoledEnabled,
            blurEnabled = blurEnabled,
            currentNavBarStyle = NavBarStyle.fromValue(navBarStyleValue),
            onThemeChange = { mode ->
                scope.launch { AppPreferences.setThemeMode(context, mode.value) }
            },
            onThemeColorChange = { color ->
                scope.launch { AppPreferences.setThemeColor(context, color.value) }
            },
            onAmoledChange = { enabled ->
                scope.launch { AppPreferences.setAmoled(context, enabled) }
            },
            onBlurChange = { enabled ->
                AppPreferences.setBlur(context, enabled)
            },
            onNavBarStyleChange = { style ->
                scope.launch { AppPreferences.setNavBarStyle(context, style.value) }
            },
        )
    }
}

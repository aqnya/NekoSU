// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.data.prefs.AppPreferences
import me.nekosu.aqnya.util.LogUtils

@Composable
fun ToolsSettingsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    val verifyKernelSymbols by AppPreferences.verifyKernelSymbolsFlow(context).collectAsState(
        initial = AppPreferences.verifyKernelSymbols(context),
    )

    SettingsDetailScaffold(
        title = stringResource(R.string.settings_section_tools),
        onBack = onBack,
    ) {
        ToolsSection(
            onExportLog = { LogUtils.exportLogs(context) },
            verifyKernelSymbols = verifyKernelSymbols,
            onVerifyKernelSymbolsChange = { enabled ->
                AppPreferences.setVerifyKernelSymbols(context, enabled)
            },
        )
    }
}

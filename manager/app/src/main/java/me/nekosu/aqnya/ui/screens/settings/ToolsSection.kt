// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.BugReport
import androidx.compose.material.icons.rounded.Security
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.component.CardGroup

@Composable
fun ToolsSection(
    onExportLog: () -> Unit,
    verifyKernelSymbols: Boolean,
    onVerifyKernelSymbolsChange: (Boolean) -> Unit,
) {
    CardGroup {
        item {
            SettingsSwitchEntry(
                icon = Icons.Rounded.Security,
                title = stringResource(R.string.settings_verify_kernel_symbols),
                summary = stringResource(R.string.settings_verify_kernel_symbols_summary),
                checked = verifyKernelSymbols,
                onCheckedChange = onVerifyKernelSymbolsChange,
            )
        }
        item {
            SettingsActionEntry(
                icon = Icons.Rounded.BugReport,
                title = stringResource(R.string.export_log),
                summary = stringResource(R.string.export_log_describe),
                onClick = onExportLog,
            )
        }
    }
}

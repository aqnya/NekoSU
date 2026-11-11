// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Info
import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R

@Composable
fun AboutSection(onAboutClick: () -> Unit) {
    SettingsSection(
        title = stringResource(R.string.about),
        icon = Icons.Rounded.Info,
    ) {
        SettingsCard {
            SettingsActionRow(
                icon = Icons.Rounded.Info,
                title = stringResource(R.string.about_information),
                summary = stringResource(R.string.settings_about_summary),
                onClick = onAboutClick,
            )
        }
    }
}

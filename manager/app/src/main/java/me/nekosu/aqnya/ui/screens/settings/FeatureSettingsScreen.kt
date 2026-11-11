// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R

@Composable
fun FeatureSettingsScreen(onBack: () -> Unit) {
    SettingsDetailScaffold(
        title = stringResource(R.string.settings_section_kernel),
        onBack = onBack,
    ) {
        FeatureSection()
    }
}

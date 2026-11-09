// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.data.prefs.AppPreferences
import me.nekosu.aqnya.ui.component.AppSwitch
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.ListRow

/** Magisk-style tapjacking protection, applied by [me.nekosu.aqnya.SecureComponentActivity]. */
@Composable
fun SecuritySection() {
    val context = LocalContext.current
    val enabled by AppPreferences
        .tapjackingProtectionFlow(context)
        .collectAsState(initial = AppPreferences.isTapjackingProtectionEnabled(context))

    CardGroup {
        item {
            ListRow(
                icon = { Icon(Icons.Rounded.Security, contentDescription = null) },
                headline = { Text(stringResource(R.string.settings_tapjacking_protection)) },
                supporting = { Text(stringResource(R.string.settings_tapjacking_protection_summary)) },
                trailing = {
                    AppSwitch(
                        checked = enabled,
                        onCheckedChange = { AppPreferences.setTapjackingProtection(context, it) },
                    )
                },
            )
        }
    }
}

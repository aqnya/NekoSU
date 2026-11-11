// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Translate
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.ui.theme.ContinuousCornerShape
import me.nekosu.aqnya.util.LocaleHelper

@Composable
fun LanguageSection(
    currentLangLabel: String,
    onLanguageChange: (String) -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }

    SettingsSection(
        title = stringResource(R.string.language_title),
        icon = Icons.Rounded.Translate,
    ) {
        SettingsCard {
            SettingsActionRow(
                icon = Icons.Rounded.Translate,
                title = stringResource(R.string.language_title),
                onClick = { menuExpanded = true },
                trailing = {
                    Box {
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            horizontalArrangement = Arrangement.spacedBy(6.dp),
                        ) {
                            SettingsValuePill(currentLangLabel)
                            Icon(
                                imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowRight,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        DropdownMenu(
                            expanded = menuExpanded,
                            onDismissRequest = { menuExpanded = false },
                            shape = ContinuousCornerShape(20.dp),
                        ) {
                            LocaleHelper.availableLanguages.forEach { lang ->
                                val label = stringResource(lang.labelRes)
                                val selected = label == currentLangLabel
                                DropdownMenuItem(
                                    text = {
                                        Text(
                                            text = label,
                                            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                                        )
                                    },
                                    onClick = {
                                        menuExpanded = false
                                        onLanguageChange(lang.tag)
                                    },
                                    trailingIcon = {
                                        if (selected) {
                                            Icon(Icons.Rounded.Check, contentDescription = null, modifier = Modifier.size(20.dp))
                                        }
                                    },
                                )
                            }
                        }
                    }
                },
            )
        }
    }
}

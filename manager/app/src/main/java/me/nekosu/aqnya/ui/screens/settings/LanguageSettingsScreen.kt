// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import android.app.Activity
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import me.nekosu.aqnya.R
import me.nekosu.aqnya.util.LocaleHelper

@Composable
fun LanguageSettingsScreen(onBack: () -> Unit) {
    val context = LocalContext.current
    var selectedTag by remember { mutableStateOf(LocaleHelper.savedLanguageTag(context)) }

    SettingsDetailScaffold(
        title = stringResource(R.string.language_title),
        onBack = onBack,
    ) {
        SettingsCard(modifier = Modifier.selectableGroup()) {
            LocaleHelper.availableLanguages.forEachIndexed { index, language ->
                if (index > 0) SettingsDivider()
                SettingsChoiceRow(
                    title = stringResource(language.labelRes),
                    selected = language.tag == selectedTag,
                    onClick = {
                        selectedTag = language.tag
                        LocaleHelper.saveLanguageTag(context, language.tag)
                        (context as? Activity)?.let { LocaleHelper.applyLanguage(it) }
                    },
                )
            }
        }
    }
}

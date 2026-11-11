// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.Build
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Palette
import androidx.compose.material.icons.rounded.Translate
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.navigation.NavController
import dev.chrisbanes.haze.hazeSource
import dev.chrisbanes.haze.rememberHazeState
import me.nekosu.aqnya.R
import me.nekosu.aqnya.core.navigation.AppRoute
import me.nekosu.aqnya.data.prefs.AppPreferences
import me.nekosu.aqnya.ui.component.CardGroup
import me.nekosu.aqnya.ui.component.CardGroupStyle
import me.nekosu.aqnya.ui.component.HazeTopAppBar
import me.nekosu.aqnya.ui.navigation.LocalBottomBarClearance
import me.nekosu.aqnya.ui.theme.ContinuousCornerShape
import me.nekosu.aqnya.ui.screens.settings.enums.ThemeColor
import me.nekosu.aqnya.ui.screens.settings.enums.ThemeMode
import me.nekosu.aqnya.util.LocaleHelper

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SettingsScreen(navController: NavController) {
    val context = LocalContext.current
    val scrollBehavior = TopAppBarDefaults.pinnedScrollBehavior(rememberTopAppBarState())

    val themeModeValue by AppPreferences.themeModeFlow(context).collectAsState(
        initial = AppPreferences.themeMode(context),
    )
    val currentThemeMode = ThemeMode.fromValue(themeModeValue)

    val themeColorValue by AppPreferences.themeColorFlow(context).collectAsState(
        initial = AppPreferences.themeColor(context),
    )
    val currentThemeColor = ThemeColor.fromValue(themeColorValue)

    val currentLang = LocaleHelper.savedLanguageTag(context)
    val currentLangLabel =
        LocaleHelper.availableLanguages
            .find { it.tag == currentLang }
            ?.let { stringResource(it.labelRes) }
            ?: stringResource(R.string.language_system)

    val appearanceSummary = "${stringResource(currentThemeMode.titleRes)} · ${currentThemeColor.label}"

    val hazeState = rememberHazeState()
    Scaffold(
        containerColor = MaterialTheme.colorScheme.surfaceContainer,
        modifier = Modifier.nestedScroll(scrollBehavior.nestedScrollConnection),
        topBar = {
            HazeTopAppBar(
                state = hazeState,
                title = stringResource(R.string.settings_title),
                scrollBehavior = scrollBehavior,
            )
        },
        contentWindowInsets =
            WindowInsets.safeDrawing.only(
                WindowInsetsSides.Top + WindowInsetsSides.Horizontal,
            ),
    ) { innerPadding ->
        Column(
            modifier =
                Modifier
                    .fillMaxSize()
                    .hazeSource(hazeState)
                    .verticalScroll(rememberScrollState())
                    .padding(innerPadding)
                    .padding(top = 20.dp)
                    .padding(bottom = LocalBottomBarClearance.current)
                    .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            CardGroup(style = CardGroupStyle.Cards) {
                item {
                    SettingsGroupEntry(
                        icon = Icons.Rounded.Palette,
                        title = stringResource(R.string.theme_title),
                        summary = appearanceSummary,
                        onClick = { navController.navigate(AppRoute.SettingsAppearance) },
                    )
                }
                item {
                    SettingsGroupEntry(
                        icon = Icons.Rounded.Tune,
                        title = stringResource(R.string.settings_section_kernel),
                        summary = stringResource(R.string.settings_group_kernel_summary),
                        onClick = { navController.navigate(AppRoute.SettingsFeatures) },
                    )
                }
                item {
                    SettingsGroupEntry(
                        icon = Icons.Rounded.Build,
                        title = stringResource(R.string.settings_section_tools),
                        summary = stringResource(R.string.settings_group_tools_summary),
                        onClick = { navController.navigate(AppRoute.SettingsTools) },
                    )
                }
                item {
                    SettingsGroupEntry(
                        icon = Icons.Rounded.Translate,
                        title = stringResource(R.string.language_title),
                        summary = currentLangLabel,
                        onClick = { navController.navigate(AppRoute.SettingsLanguage) },
                    )
                }
                item {
                    SettingsGroupEntry(
                        icon = Icons.Rounded.Info,
                        title = stringResource(R.string.about),
                        summary = stringResource(R.string.settings_about_summary),
                        onClick = { navController.navigate(AppRoute.About) },
                    )
                }
            }
        }
    }
}

@Composable
private fun SettingsGroupEntry(
    icon: ImageVector,
    title: String,
    summary: String,
    onClick: () -> Unit,
) {
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .clip(ContinuousCornerShape(28.dp))
                .clickable(onClick = onClick)
                .padding(horizontal = 16.dp, vertical = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier
                    .size(48.dp)
                    .clip(ContinuousCornerShape(16.dp))
                    .background(MaterialTheme.colorScheme.secondaryContainer),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = MaterialTheme.colorScheme.onSecondaryContainer,
            )
        }

        Spacer(Modifier.width(14.dp))

        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = title,
                style = MaterialTheme.typography.titleLarge,
                fontWeight = FontWeight.SemiBold,
            )
            Text(
                text = summary,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        }

        Spacer(Modifier.width(8.dp))

        Icon(
            imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowRight,
            contentDescription = null,
            tint = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

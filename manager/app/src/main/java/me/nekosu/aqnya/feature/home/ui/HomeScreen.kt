// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.feature.home.ui

import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.Android
import androidx.compose.material.icons.rounded.Fingerprint
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.School
import androidx.compose.material.icons.rounded.Smartphone
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview
import dev.chrisbanes.haze.hazeSource
import dev.chrisbanes.haze.rememberHazeState
import me.nekosu.aqnya.R
import me.nekosu.aqnya.core.model.InstallStatus
import me.nekosu.aqnya.feature.home.presentation.HomeViewModel
import me.nekosu.aqnya.ui.component.AppCard
import me.nekosu.aqnya.ui.component.HazeTopAppBar
import me.nekosu.aqnya.ui.component.StatusCard
import me.nekosu.aqnya.ui.navigation.LocalBottomBarClearance
import me.nekosu.aqnya.ui.theme.ContinuousCornerShape

private data class DeviceInfo(
    val icon: ImageVector,
    val label: String,
    val value: String,
    val iconContainer: Color,
    val iconContent: Color,
    val monospace: Boolean = false,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreenContent(
    installStatus: InstallStatus,
    isGki: Boolean,
    managerVersion: String,
    onInstallClick: () -> Unit,
    onAboutClick: () -> Unit,
) {
    val hazeState = rememberHazeState()
    Box(modifier = Modifier.fillMaxSize()) {
        Scaffold(
            containerColor = MaterialTheme.colorScheme.surfaceContainer,
            topBar = {
                HazeTopAppBar(
                    state = hazeState,
                    title = stringResource(R.string.app_name),
                )
            },
        ) { innerPadding ->
            Column(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .hazeSource(hazeState)
                        .verticalScroll(rememberScrollState())
                        .padding(innerPadding)
                        .padding(horizontal = 16.dp)
                        .padding(top = 16.dp)
                        .padding(bottom = LocalBottomBarClearance.current),
                verticalArrangement = Arrangement.spacedBy(22.dp),
            ) {
                StatusCard(
                    state = installStatus,
                    isGki = isGki,
                    managerVersion = managerVersion,
                    onClick = onInstallClick,
                )

                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    HomeSectionTitle(
                        icon = Icons.Rounded.Smartphone,
                        title = stringResource(R.string.install_device_info),
                    )
                    DeviceInfoCard()
                }

                Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    HomeSectionTitle(
                        icon = Icons.Rounded.Info,
                        title = stringResource(R.string.about),
                    )
                    AboutCard(onClick = onAboutClick)
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun HomeScreen(
    viewModel: HomeViewModel,
    onAboutClick: () -> Unit,
    onInstallClick: () -> Unit,
) {
    val state by viewModel.uiState.collectAsState()

    LaunchedEffect(state.installStatus) {
        if (state.installStatus == InstallStatus.INSTALLED) {
            viewModel.refresh()
        }
    }

    HomeScreenContent(
        installStatus = state.installStatus,
        isGki = state.isGki,
        managerVersion = state.managerVersion,
        onInstallClick = onInstallClick,
        onAboutClick = onAboutClick,
    )
}

@Composable
private fun HomeSectionTitle(
    icon: ImageVector,
    title: String,
) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = icon,
            contentDescription = null,
            modifier = Modifier.size(16.dp),
            tint = MaterialTheme.colorScheme.primary,
        )
        Spacer(Modifier.width(6.dp))
        Text(
            text = title,
            style = MaterialTheme.typography.titleSmall,
            fontWeight = FontWeight.SemiBold,
            color = MaterialTheme.colorScheme.primary,
        )
    }
}

@Composable
private fun DeviceInfoCard() {
    val entries =
        listOf(
            DeviceInfo(
                icon = Icons.Rounded.Android,
                label = stringResource(R.string.android_version),
                value = Build.VERSION.RELEASE,
                iconContainer = MaterialTheme.colorScheme.primaryContainer,
                iconContent = MaterialTheme.colorScheme.onPrimaryContainer,
            ),
            DeviceInfo(
                icon = Icons.Rounded.Memory,
                label = stringResource(R.string.kernel_version),
                value = System.getProperty("os.version") ?: "Unavailable",
                iconContainer = MaterialTheme.colorScheme.tertiaryContainer,
                iconContent = MaterialTheme.colorScheme.onTertiaryContainer,
            ),
            DeviceInfo(
                icon = Icons.Rounded.Smartphone,
                label = stringResource(R.string.device_model),
                value = "${Build.MANUFACTURER} ${Build.MODEL}",
                iconContainer = MaterialTheme.colorScheme.secondaryContainer,
                iconContent = MaterialTheme.colorScheme.onSecondaryContainer,
            ),
            DeviceInfo(
                icon = Icons.Rounded.Fingerprint,
                label = stringResource(R.string.finger_print),
                value = Build.FINGERPRINT,
                iconContainer = MaterialTheme.colorScheme.surfaceContainerHighest,
                iconContent = MaterialTheme.colorScheme.onSurfaceVariant,
                monospace = true,
            ),
        )

    AppCard(
        shape = ContinuousCornerShape(28.dp),
        containerColor = MaterialTheme.colorScheme.surfaceBright,
    ) {
        Column {
            entries.forEachIndexed { index, entry ->
                if (index > 0) {
                    HorizontalDivider(
                        modifier = Modifier.padding(horizontal = 16.dp),
                        color = MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f),
                    )
                }
                DeviceInfoRow(entry)
            }
        }
    }
}

@Composable
private fun DeviceInfoRow(entry: DeviceInfo) {
    Row(
        modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(
            modifier =
                Modifier
                    .size(40.dp)
                    .clip(ContinuousCornerShape(14.dp))
                    .background(entry.iconContainer),
            contentAlignment = Alignment.Center,
        ) {
            Icon(
                imageVector = entry.icon,
                contentDescription = null,
                modifier = Modifier.size(20.dp),
                tint = entry.iconContent,
            )
        }

        Spacer(Modifier.width(14.dp))

        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(2.dp),
        ) {
            Text(
                text = entry.label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = entry.value,
                style =
                    if (entry.monospace) {
                        MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace)
                    } else {
                        MaterialTheme.typography.bodyLarge.copy(fontWeight = FontWeight.Medium)
                    },
                maxLines = if (entry.monospace) 2 else 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun AboutCard(onClick: () -> Unit) {
    val contentColor = MaterialTheme.colorScheme.onSecondaryContainer

    AppCard(
        onClick = onClick,
        shape = ContinuousCornerShape(28.dp),
        containerColor = MaterialTheme.colorScheme.secondaryContainer,
        contentColor = contentColor,
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(18.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier =
                    Modifier
                        .size(48.dp)
                        .clip(ContinuousCornerShape(16.dp))
                        .background(contentColor.copy(alpha = 0.14f)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(
                    imageVector = Icons.Rounded.School,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = contentColor,
                )
            }

            Spacer(Modifier.width(14.dp))

            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    text = stringResource(R.string.about_card_title),
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                )
                Text(
                    text = stringResource(R.string.about_card_description),
                    style = MaterialTheme.typography.bodyMedium,
                    color = contentColor.copy(alpha = 0.78f),
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                )
            }

            Spacer(Modifier.width(8.dp))

            Icon(
                imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowRight,
                contentDescription = null,
                tint = contentColor,
            )
        }
    }
}

@Preview(showBackground = true, name = "Installed")
@Composable
fun HomeScreenPreviewInstalled() {
    MaterialTheme {
        HomeScreenContent(
            installStatus = InstallStatus.INSTALLED,
            isGki = true,
            managerVersion = "1.0.0",
            onInstallClick = {},
            onAboutClick = {},
        )
    }
}

@Preview(showBackground = true, name = "Not Install")
@Composable
fun HomeScreenPreviewNotInstalled() {
    MaterialTheme {
        HomeScreenContent(
            installStatus = InstallStatus.NOT_INSTALLED,
            isGki = false,
            managerVersion = "1.0.0",
            onInstallClick = {},
            onAboutClick = {},
        )
    }
}

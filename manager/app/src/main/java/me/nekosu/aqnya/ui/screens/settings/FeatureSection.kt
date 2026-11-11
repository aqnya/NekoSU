// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.screens.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Security
import androidx.compose.material.icons.rounded.Tune
import androidx.compose.material.icons.rounded.VisibilityOff
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import me.nekosu.aqnya.R
import me.nekosu.aqnya.data.kernel.KernelFeature

/**
 * 内核 feature 开关：列表与状态都由内核给出（IOC_FEATURE_LIST/SET），管理器只
 * 负责展示、切换，并把开关同步到开机的标志文件。
 */
@Composable
fun FeatureSection() {
    val scope = rememberCoroutineScope()
    var features by remember { mutableStateOf<List<KernelFeature.Info>>(emptyList()) }
    var busy by remember { mutableStateOf(false) }

    suspend fun reload() {
        features = withContext(Dispatchers.IO) { KernelFeature.list() }
    }
    LaunchedEffect(Unit) { reload() }

    SettingsSection(
        title = stringResource(R.string.settings_section_kernel),
        icon = Icons.Rounded.Tune,
        trailing = {
            if (features.isNotEmpty()) {
                SettingsValuePill("${features.count { it.enabled }}/${features.size}")
            }
        },
    ) {
        SettingsCard {
            if (features.isEmpty()) {
                Column(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 22.dp),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    SettingsIcon(
                        icon = Icons.Rounded.Tune,
                        containerColor = MaterialTheme.colorScheme.surfaceContainerHighest,
                        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = stringResource(R.string.feature_empty),
                        style = MaterialTheme.typography.titleSmall,
                    )
                    Text(
                        text = stringResource(R.string.feature_empty_describe),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        textAlign = TextAlign.Center,
                    )
                }
            } else {
                features.forEachIndexed { index, feature ->
                    if (index > 0) SettingsDivider()
                    SettingsSwitchRow(
                        icon = feature.icon(),
                        title = featureLabel(feature),
                        summary = featureSummary(feature),
                        checked = feature.enabled,
                        enabled = !busy,
                        onCheckedChange = { checked ->
                            scope.launch {
                                busy = true
                                val ok =
                                    withContext(Dispatchers.IO) {
                                        KernelFeature.set(feature.id, checked)
                                    }
                                busy = false
                                if (ok) reload()
                            }
                        },
                    )
                }
            }
        }
    }
}

private fun KernelFeature.Info.icon(): ImageVector =
    when (id) {
        KernelFeature.SELINUX_HIDE -> Icons.Rounded.Security
        KernelFeature.UMOUNT -> Icons.Rounded.VisibilityOff
        else -> Icons.Rounded.Tune
    }

@Composable
private fun featureLabel(feature: KernelFeature.Info): String =
    when (feature.id) {
        KernelFeature.SELINUX_HIDE -> stringResource(R.string.feature_selinux_hide)
        KernelFeature.UMOUNT -> stringResource(R.string.feature_umount)
        // 未知 feature 直接显示内核给的名字。
        else -> feature.name
    }

@Composable
private fun featureSummary(feature: KernelFeature.Info): String =
    when (feature.id) {
        KernelFeature.SELINUX_HIDE -> stringResource(R.string.feature_selinux_hide_describe)
        KernelFeature.UMOUNT -> stringResource(R.string.feature_umount_describe)
        else -> feature.name
    }

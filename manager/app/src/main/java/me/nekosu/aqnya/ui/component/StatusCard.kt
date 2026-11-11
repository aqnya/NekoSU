// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.SystemUpdate
import androidx.compose.material.icons.rounded.Warning
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import me.nekosu.aqnya.R
import me.nekosu.aqnya.core.model.InstallStatus
import me.nekosu.aqnya.ui.theme.ContinuousCornerShape

@Composable
fun StatusCard(
    state: InstallStatus,
    isGki: Boolean,
    managerVersion: String,
    onClick: () -> Unit,
) {
    val isWorking = state == InstallStatus.INSTALLED
    val isUpdate = state == InstallStatus.NEED_UPDATE || state == InstallStatus.NEED_REBOOT

    val containerColor =
        when {
            isWorking -> MaterialTheme.colorScheme.primaryContainer
            isUpdate -> MaterialTheme.colorScheme.tertiaryContainer
            else -> MaterialTheme.colorScheme.errorContainer
        }
    val contentColor =
        when {
            isWorking -> MaterialTheme.colorScheme.onPrimaryContainer
            isUpdate -> MaterialTheme.colorScheme.onTertiaryContainer
            else -> MaterialTheme.colorScheme.onErrorContainer
        }
    val icon =
        when {
            isWorking -> Icons.Rounded.CheckCircle
            isUpdate -> Icons.Rounded.SystemUpdate
            else -> Icons.Rounded.Warning
        }
    val title =
        when {
            isWorking -> stringResource(R.string.running)
            isUpdate -> stringResource(R.string.status_card_need_update)
            else -> stringResource(R.string.status_card_not_installed)
        }
    val subtitle =
        when {
            isWorking -> stringResource(R.string.home_active_subtitle)
            state == InstallStatus.NEED_REBOOT -> stringResource(R.string.home_reboot_subtitle)
            else -> stringResource(R.string.status_card_click_to_install)
        }

    AppCard(
        modifier = Modifier.fillMaxWidth(),
        onClick = onClick,
        shape = ContinuousCornerShape(32.dp),
        containerColor = containerColor,
        contentColor = contentColor,
    ) {
        Box(modifier = Modifier.fillMaxWidth().clip(ContinuousCornerShape(32.dp))) {
            Box(
                modifier =
                    Modifier
                        .align(Alignment.TopEnd)
                        .offset(x = 64.dp, y = (-72).dp)
                        .size(190.dp)
                        .clip(CircleShape)
                        .background(contentColor.copy(alpha = 0.07f)),
            )

            Column(
                modifier = Modifier.padding(22.dp),
                verticalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(
                        modifier =
                            Modifier
                                .size(56.dp)
                                .clip(ContinuousCornerShape(20.dp))
                                .background(contentColor.copy(alpha = 0.14f)),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            imageVector = icon,
                            contentDescription = null,
                            modifier = Modifier.size(28.dp),
                        )
                    }

                    Spacer(Modifier.width(16.dp))

                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.headlineSmall,
                            fontWeight = FontWeight.Bold,
                        )
                        Text(
                            text = subtitle,
                            style = MaterialTheme.typography.bodyMedium,
                            color = contentColor.copy(alpha = 0.78f),
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }
                }

                HorizontalDivider(color = contentColor.copy(alpha = 0.16f))

                Row(verticalAlignment = Alignment.CenterVertically) {
                    ModeLabel(
                        label = if (isGki) "GKI" else "LKM",
                        color = contentColor,
                        containerColor = contentColor.copy(alpha = 0.16f),
                    )

                    Spacer(Modifier.width(14.dp))

                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(2.dp),
                    ) {
                        Text(
                            text = stringResource(R.string.manager_version),
                            style = MaterialTheme.typography.labelSmall,
                            color = contentColor.copy(alpha = 0.7f),
                        )
                        Text(
                            text = managerVersion,
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Medium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                    }

                    Spacer(Modifier.width(12.dp))

                    Box(
                        modifier =
                            Modifier
                                .size(44.dp)
                                .clip(CircleShape)
                                .background(contentColor.copy(alpha = 0.14f)),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            imageVector = Icons.AutoMirrored.Rounded.KeyboardArrowRight,
                            contentDescription = null,
                            tint = contentColor,
                        )
                    }
                }
            }
        }
    }
}

@Composable
fun ModeLabel(
    label: String,
    modifier: Modifier = Modifier,
    color: Color = MaterialTheme.colorScheme.onPrimary,
    containerColor: Color = MaterialTheme.colorScheme.primary,
) {
    Box(
        modifier =
            modifier
                .background(color = containerColor, shape = RoundedCornerShape(50))
                .padding(horizontal = 9.dp, vertical = 4.dp),
    ) {
        Text(
            text = label,
            style =
                MaterialTheme.typography.labelMedium.copy(
                    fontWeight = FontWeight.SemiBold,
                ),
            color = color,
            maxLines = 1,
        )
    }
}

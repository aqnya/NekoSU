// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp

private val FloatingBarInnerPadding = 6.dp
private val FloatingTabIdealWidth = 80.dp
private val FloatingIndicatorWidth = 48.dp
private val FloatingIndicatorHeight = 32.dp

/**
 * Material 3 floating navigation bar.
 *
 * Unlike the liquid-glass bar this is an opaque, tonal Material surface: it
 * uses the theme's surface and secondary-container roles, a per-tab selection
 * indicator, and no backdrop blur, refraction, or drag gesture.
 */
@Composable
fun FloatingBottomNavigationBar(
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    require(items.isNotEmpty()) { "The floating bottom bar needs at least one tab" }

    val selected = selectedIndex.coerceIn(0, items.lastIndex)

    BoxWithConstraints(
        modifier = modifier,
        contentAlignment = Alignment.Center,
    ) {
        // Keep the dock compact on tablets and let it shrink on narrow phones
        // instead of pushing five tabs past the screen edge.
        val idealBarWidth = FloatingTabIdealWidth * items.size + FloatingBarInnerPadding * 2
        val barWidth = idealBarWidth.coerceAtMost(maxWidth)

        Surface(
            modifier =
                Modifier
                    .width(barWidth)
                    .height(FloatingBottomBarHeight),
            shape = MaterialTheme.shapes.extraLarge,
            color = MaterialTheme.colorScheme.surfaceContainerHigh,
            contentColor = MaterialTheme.colorScheme.onSurface,
            tonalElevation = 3.dp,
            shadowElevation = 6.dp,
        ) {
            Row(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .padding(horizontal = FloatingBarInnerPadding)
                        .selectableGroup(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                items.forEachIndexed { index, item ->
                    FloatingBarTab(
                        item = item,
                        selected = index == selected,
                        onSelect = { onTabClick(index) },
                    )
                }
            }
        }
    }
}

@Composable
private fun RowScope.FloatingBarTab(
    item: BottomNavItem,
    selected: Boolean,
    onSelect: () -> Unit,
) {
    val indicatorProgress by
        animateFloatAsState(
            targetValue = if (selected) 1f else 0f,
            animationSpec = spring(dampingRatio = 0.8f, stiffness = 600f),
            label = "floatingTabIndicator",
        )
    val iconColor by
        animateColorAsState(
            targetValue =
                if (selected) {
                    MaterialTheme.colorScheme.onSecondaryContainer
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            animationSpec = tween(180),
            label = "floatingTabIconColor",
        )
    val labelColor by
        animateColorAsState(
            targetValue =
                if (selected) {
                    MaterialTheme.colorScheme.onSurface
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            animationSpec = tween(180),
            label = "floatingTabLabelColor",
        )

    Column(
        modifier =
            Modifier
                .selectable(
                    selected = selected,
                    role = Role.Tab,
                    onClick = onSelect,
                ).fillMaxHeight()
                .weight(1f),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically),
    ) {
        Box(
            modifier =
                Modifier
                    .size(width = FloatingIndicatorWidth, height = FloatingIndicatorHeight)
                    .graphicsLayer {
                        val scale = 0.82f + 0.18f * indicatorProgress
                        scaleX = scale
                        scaleY = scale
                        alpha = indicatorProgress
                    }.background(
                        color = MaterialTheme.colorScheme.secondaryContainer,
                        shape = MaterialTheme.shapes.large,
                    ),
            contentAlignment = Alignment.Center,
        ) {
            Crossfade(
                targetState = selected,
                animationSpec = tween(180),
                label = "floatingTabIcon",
            ) { isSelected ->
                Icon(
                    imageVector = if (isSelected) item.selectedIcon else item.unselectedIcon,
                    contentDescription = stringResource(item.titleRes),
                    tint = iconColor,
                    modifier = Modifier.size(24.dp),
                )
            }
        }

        Text(
            text = stringResource(item.titleRes),
            color = labelColor,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
            maxLines = 1,
            softWrap = false,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

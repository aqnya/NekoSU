// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp

/** Height of the floating, opaque navigation dock. */
val FloatingDockHeight = 72.dp

/** Horizontal breathing room around the dock. */
val FloatingDockHorizontalPadding = 16.dp

/** Vertical gap between the dock and the system navigation bar. */
val FloatingDockVerticalPadding = 12.dp

/**
 * Bottom scroll clearance for tab content: dock height + vertical padding on
 * both sides + the floating gap between content and the dock.
 */
val FloatingDockContentPadding = 120.dp

/** The dock stays a comfortable control even on expanded windows. */
private val FloatingDockMaxWidth = 560.dp

private val FloatingDockInnerPadding = 8.dp

/**
 * Floating Material 3 navigation dock.
 *
 * An opaque capsule that floats above the content with a tonal selection
 * pill behind the whole tab. Unlike the liquid-glass bar it keeps a
 * stable dock: no backdrop sampling, no drag pill and no rubber-band
 * overscroll. The selection pill slides between tabs with a spring from the
 * theme's motion scheme.
 */
@Composable
fun FloatingBottomNavigationBar(
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    require(items.isNotEmpty()) { "The floating bottom bar needs at least one tab" }

    val scheme = MaterialTheme.colorScheme
    val motionScheme = MaterialTheme.motionScheme
    val isLtr = LocalLayoutDirection.current == LayoutDirection.Ltr
    val selected = selectedIndex.coerceIn(0, items.lastIndex)
    val indicatorProgress by
        animateFloatAsState(
            targetValue = selected.toFloat(),
            animationSpec = motionScheme.defaultSpatialSpec<Float>(),
            label = "floatingDockIndicator",
        )
    val shape = CircleShape

    Box(
        modifier =
            modifier
                .widthIn(max = FloatingDockMaxWidth)
                .fillMaxWidth()
                .height(FloatingDockHeight)
                .shadow(elevation = 8.dp, shape = shape, clip = false)
                .background(scheme.surfaceBright, shape)
                .clip(shape)
                .padding(FloatingDockInnerPadding),
    ) {
        BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
            val tabWidth = maxWidth / items.size.toFloat()

            Box(
                modifier =
                    Modifier
                        .width(tabWidth)
                        .fillMaxHeight()
                        .graphicsLayer {
                            translationX =
                                (if (isLtr) indicatorProgress else -indicatorProgress) * size.width
                        }.background(scheme.secondaryContainer, shape),
            )

            Row(
                modifier = Modifier.fillMaxSize().selectableGroup(),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                items.forEachIndexed { index, item ->
                    FloatingDockItem(
                        item = item,
                        selected = index == selected,
                        onClick = { onTabClick(index) },
                        modifier = Modifier.weight(1f).fillMaxHeight(),
                    )
                }
            }
        }
    }
}

@Composable
private fun FloatingDockItem(
    item: BottomNavItem,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val scheme = MaterialTheme.colorScheme
    val motionScheme = MaterialTheme.motionScheme
    val contentColor by
        animateColorAsState(
            targetValue =
                if (selected) {
                    scheme.onSecondaryContainer
                } else {
                    scheme.onSurfaceVariant
                },
            animationSpec = motionScheme.defaultEffectsSpec<Color>(),
            label = "floatingDockItemColor",
        )

    Column(
        modifier =
            modifier.selectable(
                selected = selected,
                interactionSource = null,
                indication = null,
                role = Role.Tab,
                onClick = onClick,
            ),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(2.dp, Alignment.CenterVertically),
    ) {
        Crossfade(
            targetState = selected,
            animationSpec = motionScheme.defaultEffectsSpec<Float>(),
            label = "floatingDockIcon",
        ) { isSelected ->
            Icon(
                imageVector = if (isSelected) item.selectedIcon else item.unselectedIcon,
                contentDescription = stringResource(item.titleRes),
                tint = contentColor,
                modifier = Modifier.size(24.dp),
            )
        }

        Text(
            text = stringResource(item.titleRes),
            color = contentColor,
            style = MaterialTheme.typography.labelSmall,
            fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Preview(showBackground = true, widthDp = 400)
@Composable
private fun FloatingBottomNavigationBarPreview() {
    MaterialTheme {
        Box(modifier = Modifier.fillMaxWidth().padding(16.dp)) {
            FloatingBottomNavigationBar(
                items = BottomNavItem.items(showRules = true),
                selectedIndex = 0,
                onTabClick = {},
            )
        }
    }
}

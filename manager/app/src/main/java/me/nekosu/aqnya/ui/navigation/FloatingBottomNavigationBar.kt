// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier

/**
 * The liquid-glass bar with the glass removed.
 *
 * It keeps the same compact capsule, tab rhythm, press scaling and draggable
 * selection pill, but paints an opaque tonal surface with a hairline edge, so
 * it never samples the page behind it. Callers do not need a layer backdrop.
 */
@Composable
fun FloatingBottomNavigationBar(
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    FloatingBottomBar(
        surface = FloatingBarSurface.Solid,
        items = items,
        selectedIndex = selectedIndex,
        onTabClick = onTabClick,
        modifier = modifier,
    )
}

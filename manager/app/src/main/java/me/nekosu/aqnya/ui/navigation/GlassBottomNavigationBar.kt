// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kyant.backdrop.Backdrop

/**
 * NekoSU's compact liquid-glass bar.
 *
 * The selected tab carries the accent directly, the selection pill is a
 * neutral 10% glass fill, and the bar is draggable from the pill. The caller
 * must record the page behind the bar with `Modifier.layerBackdrop(backdrop)`.
 */
@Composable
fun GlassBottomNavigationBar(
    backdrop: Backdrop,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    FloatingBottomBar(
        surface = FloatingBarSurface.Glass(backdrop),
        items = items,
        selectedIndex = selectedIndex,
        onTabClick = onTabClick,
        modifier = modifier,
    )
}

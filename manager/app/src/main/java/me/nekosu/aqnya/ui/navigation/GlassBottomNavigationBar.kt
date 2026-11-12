// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kyant.backdrop.Backdrop

/**
 * NekoSU's compact liquid-glass bar.
 *
 * The selected tab carries the accent directly, the selection pill is a
 * neutral 10% glass fill, and the bar is draggable across its whole width. The
 * shared implementation keeps the page backdrop, drag physics, and adaptive
 * width in one place with the floating dock.
 *
 * The caller must record the page behind the bar with
 * `Modifier.layerBackdrop(backdrop)`.
 */
@Composable
fun GlassBottomNavigationBar(
    backdrop: Backdrop,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    LiquidBottomNavigationBar(
        variant = LiquidBarVariant.GLASS,
        backdrop = backdrop,
        items = items,
        selectedIndex = selectedIndex,
        onTabClick = onTabClick,
        modifier = modifier,
    )
}

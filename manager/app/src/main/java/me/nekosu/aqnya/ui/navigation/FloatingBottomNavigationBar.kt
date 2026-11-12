// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kyant.backdrop.Backdrop

/**
 * BakaSU-style floating dock.
 *
 * All tabs stay neutral until the moving glass pill passes over them; the pill
 * refracts a hidden accent copy of the row, so the selected icon and label are
 * revealed through the glass instead of being painted on top of it.
 *
 * The caller must record the page behind the bar with
 * `Modifier.layerBackdrop(backdrop)`.
 */
@Composable
fun FloatingBottomNavigationBar(
    backdrop: Backdrop,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    LiquidBottomNavigationBar(
        variant = LiquidBarVariant.FLOATING,
        backdrop = backdrop,
        items = items,
        selectedIndex = selectedIndex,
        onTabClick = onTabClick,
        modifier = modifier,
    )
}

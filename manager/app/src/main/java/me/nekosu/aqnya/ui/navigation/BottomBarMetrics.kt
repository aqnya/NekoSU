// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.unit.dp

/** Shared height of the compact, floating navigation capsules. */
val LiquidBottomBarHeight = 64.dp

/** Horizontal breathing room around the compact glass capsule. */
val GlassBottomBarHorizontalPadding = 28.dp

/** Vertical gap between the glass capsule and the system navigation bar. */
val GlassBottomBarVerticalPadding = 10.dp

/** Horizontal breathing room around the BakaSU-style floating dock. */
val FloatingBottomBarHorizontalPadding = 16.dp

/** Vertical gap between the floating dock and the system navigation bar. */
val FloatingBottomBarVerticalPadding = 12.dp

/**
 * Bottom scroll clearance for tab content: bar height + vertical padding on
 * both sides + the floating gap between content and the bar.
 */
val GlassBottomBarContentPadding = 116.dp

/** The floating dock shares the glass capsule's height and bottom gap. */
val FloatingBottomBarContentPadding = 116.dp

/** Clearance for the stock Material 3 navigation bar (80 dp) plus a 16 dp gap. */
val StandardBottomBarContentPadding = 96.dp

/** Bottom scroll clearance selected by the current navigation bar style. */
val LocalBottomBarClearance = compositionLocalOf { GlassBottomBarContentPadding }

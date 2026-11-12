// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.util.lerp
import com.kyant.backdrop.Backdrop
import com.kyant.backdrop.backdrops.layerBackdrop
import com.kyant.backdrop.backdrops.rememberCombinedBackdrop
import com.kyant.backdrop.backdrops.rememberLayerBackdrop
import com.kyant.backdrop.drawBackdrop
import com.kyant.backdrop.effects.blur
import com.kyant.backdrop.effects.lens
import com.kyant.backdrop.effects.vibrancy
import com.kyant.backdrop.highlight.Highlight
import com.kyant.backdrop.shadow.InnerShadow
import com.kyant.backdrop.shadow.Shadow
import com.kyant.capsule.ContinuousCapsule
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.math.roundToInt
import kotlin.math.sign

/** Floating capsule metrics shared by the liquid-glass and solid bars. */
val FloatingBottomBarHeight = 64.dp
val FloatingBottomBarHorizontalPadding = 28.dp
val FloatingBottomBarVerticalPadding = 10.dp

/**
 * Bottom scroll clearance for tab content: bar height + vertical padding on
 * both sides + the floating gap between content and the bar.
 */
val FloatingBottomBarContentPadding = 116.dp

/** Clearance for the stock Material 3 navigation bar (80 dp) plus a 16 dp gap. */
val StandardBottomBarContentPadding = 96.dp

/** Bottom scroll clearance selected by the current navigation bar style. */
val LocalBottomBarClearance = compositionLocalOf { FloatingBottomBarContentPadding }

private val FloatingTabMinWidth = 76.dp
private val FloatingPillHeight = 56.dp

/** Pressed pill height; it stops flush with the 64 dp bar instead of bulging out. */
private val FloatingPillPressedHeight = 64.dp
private val FloatingBarInnerPadding = 4.dp

/** Item scale driven by the bar press progress, read from a graphics layer. */
private val LocalFloatingBarTabScale = staticCompositionLocalOf<() -> Float> { { 1f } }

/** How [FloatingBottomBar] paints its capsule. */
internal sealed interface FloatingBarSurface {
    /** Samples and refracts the page behind the bar. */
    data class Glass(
        val backdrop: Backdrop,
    ) : FloatingBarSurface

    /** Opaque tonal capsule with a hairline edge. */
    data object Solid : FloatingBarSurface
}

/**
 * Shared chrome for the app's floating bottom bars.
 *
 * The capsule is sized by its tabs, scales up while pressed, lets the finger
 * drag the selection pill past the ends with a rubber-band overscroll, and
 * swaps only its surface: liquid glass ([FloatingBarSurface.Glass]) or an
 * opaque tonal capsule ([FloatingBarSurface.Solid]). Glass callers must record
 * the page behind the bar with `Modifier.layerBackdrop(backdrop)`.
 */
@Composable
internal fun FloatingBottomBar(
    surface: FloatingBarSurface,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    require(items.isNotEmpty()) { "The floating bottom bar needs at least one tab" }

    val scheme = MaterialTheme.colorScheme
    val isDark = scheme.surfaceContainer.luminance() < 0.5f
    val barShape = ContinuousCapsule
    val glass = surface as? FloatingBarSurface.Glass
    val containerColor =
        if (glass != null) {
            scheme.surfaceContainer.copy(alpha = 0.40f)
        } else {
            // The same elevated tone the app's cards use; unlike the container
            // roles it stays readable in AMOLED black.
            scheme.surfaceBright
        }
    val borderColor = scheme.outlineVariant.copy(alpha = 0.5f)
    // KernelSU's clear-glass pill: the accent tone comes from the selected
    // icon and label, the surface itself stays at a 10% neutral fill.
    val selectionSurface =
        if (isDark) Color.White.copy(alpha = 0.10f) else Color.Black.copy(alpha = 0.10f)
    val accentColor = scheme.primary
    val tabContentColor = scheme.onSurface

    val tabsBackdrop = if (glass != null) rememberLayerBackdrop() else null
    val combinedBackdrop =
        if (glass != null && tabsBackdrop != null) {
            rememberCombinedBackdrop(glass.backdrop, tabsBackdrop)
        } else {
            null
        }
    val density = LocalDensity.current
    val isLtr = LocalLayoutDirection.current == LayoutDirection.Ltr
    val animationScope = rememberCoroutineScope()

    var tabWidthPx by remember { mutableFloatStateOf(0f) }
    var totalWidthPx by remember { mutableFloatStateOf(0f) }

    val offsetAnimation = remember { Animatable(0f) }
    val rubberBandPx = with(density) { 4.dp.toPx() }
    val panelOffset by
        remember(rubberBandPx) {
            derivedStateOf {
                if (totalWidthPx == 0f) {
                    0f
                } else {
                    val fraction = (offsetAnimation.value / totalWidthPx).coerceIn(-1f, 1f)
                    rubberBandPx * fraction.sign * EaseOut.transform(abs(fraction))
                }
            }
        }

    val onTabClickUpdated by rememberUpdatedState(onTabClick)
    val latestSelectedIndex by rememberUpdatedState(selectedIndex)
    val lastIndex = items.lastIndex
    val innerPaddingPx = with(density) { FloatingBarInnerPadding.toPx() }

    val dragAnimation =
        remember(animationScope, items, density, isLtr) {
            DampedDragAnimation(
                animationScope = animationScope,
                initialValue = selectedIndex.toFloat(),
                valueRange = 0f..lastIndex.toFloat(),
                visibilityThreshold = 0.001f,
                initialScale = 1f,
                pressedScale = FloatingPillPressedHeight / FloatingPillHeight,
                canDrag = { true },
                onDragStarted = {},
                onDragStopped = {
                    val targetIndex = targetValue.roundToInt().coerceIn(0, lastIndex)
                    animateToValue(targetIndex.toFloat())
                    if (targetIndex != latestSelectedIndex) {
                        onTabClickUpdated(targetIndex)
                    }
                    animationScope.launch {
                        offsetAnimation.animateTo(0f, spring(1f, 300f, 0.5f))
                    }
                },
                onDrag = { _, dragAmount ->
                    if (tabWidthPx > 0f && dragAmount.x != 0f) {
                        updateValue(
                            (targetValue + dragAmount.x / tabWidthPx * if (isLtr) 1f else -1f)
                                .coerceIn(0f, lastIndex.toFloat()),
                        )
                        animationScope.launch {
                            offsetAnimation.snapTo(offsetAnimation.value + dragAmount.x)
                        }
                    }
                },
            )
        }

    LaunchedEffect(selectedIndex, dragAnimation) {
        if (dragAnimation.value != selectedIndex.toFloat()) {
            dragAnimation.animateToValue(selectedIndex.toFloat())
        }
    }

    val interactiveHighlight =
        if (glass != null) {
            remember(animationScope, tabWidthPx, dragAnimation, isLtr) {
                InteractiveHighlight(animationScope) { size, _ ->
                    val centerX =
                        if (isLtr) {
                            (dragAnimation.value + 0.5f) * tabWidthPx
                        } else {
                            size.width - (dragAnimation.value + 0.5f) * tabWidthPx
                        }
                    Offset(centerX, size.height / 2f)
                }
            }
        } else {
            null
        }

    val barWidth = FloatingTabMinWidth * items.size + FloatingBarInnerPadding * 2

    val containerModifier =
        if (glass != null && tabsBackdrop != null) {
            Modifier
                .fillMaxSize()
                .layerBackdrop(tabsBackdrop)
                .drawBackdrop(
                    backdrop = glass.backdrop,
                    shape = { barShape },
                    effects = {
                        padding = maxOf(padding, 40.dp.toPx())
                        vibrancy()
                        blur(8.dp.toPx())
                        lens(24.dp.toPx(), 24.dp.toPx())
                    },
                    highlight = { Highlight.Default.copy(alpha = 1f) },
                    shadow = {
                        Shadow.Default.copy(
                            color = Color.Black.copy(alpha = if (isDark) 0.2f else 0.1f),
                        )
                    },
                    layerBlock = {
                        val width = size.width.coerceAtLeast(1f)
                        val scale = lerp(1f, 1f + 12.dp.toPx() / width, dragAnimation.pressProgress)
                        scaleX = scale
                        scaleY = scale
                    },
                    onDrawSurface = { drawRect(containerColor) },
                )
        } else {
            // The glass treatment removed: the same capsule, painted opaque,
            // lifted by a real shadow and a hairline edge instead of a blur,
            // a refraction lens and a specular rim.
            val pressScale =
                Modifier.graphicsLayer {
                    val width = size.width.coerceAtLeast(1f)
                    val scale = lerp(1f, 1f + 12.dp.toPx() / width, dragAnimation.pressProgress)
                    scaleX = scale
                    scaleY = scale
                }
            Modifier
                .fillMaxSize()
                .then(pressScale)
                .shadow(elevation = 6.dp, shape = barShape, clip = false)
                .background(containerColor, barShape)
                .border(width = 1.dp, color = borderColor, shape = barShape)
        }

    val tabs: @Composable () -> Unit = {
        CompositionLocalProvider(
            LocalFloatingBarTabScale provides { lerp(1f, 1.1f, dragAnimation.pressProgress) },
        ) {
            Row(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .onGloballyPositioned { coordinates ->
                            totalWidthPx = coordinates.size.width.toFloat()
                            val contentWidthPx = totalWidthPx - innerPaddingPx * 2f
                            tabWidthPx = (contentWidthPx / items.size).coerceAtLeast(0f)
                        }.selectableGroup()
                        .then(interactiveHighlight?.modifier ?: Modifier)
                        .padding(FloatingBarInnerPadding),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                items.forEachIndexed { index, item ->
                    FloatingBarItem(
                        item = item,
                        selected = index == selectedIndex,
                        selectedColor = accentColor,
                        unselectedColor = tabContentColor,
                        onSelect = { onTabClickUpdated(index) },
                    )
                }
            }
        }
    }

    Box(
        modifier =
            modifier
                .graphicsLayer { translationX = panelOffset }
                .width(barWidth)
                .height(FloatingBottomBarHeight),
        contentAlignment = Alignment.CenterStart,
    ) {
        Box(modifier = containerModifier) { tabs() }

        // The glass pill samples the page plus the tab-content layer, so its
        // lens can refract the icons themselves, as in the KernelSU bar. The
        // solid pill keeps the same fill and motion without the lens.
        if (tabWidthPx > 0f) {
            val tabWidthDp = with(density) { tabWidthPx.toDp() }
            val pillSurfaceModifier =
                if (combinedBackdrop != null) {
                    Modifier.drawBackdrop(
                        backdrop = combinedBackdrop,
                        shape = { barShape },
                        effects = {
                            val progress = dragAnimation.pressProgress
                            lens(
                                refractionHeight = 10.dp.toPx() * progress,
                                refractionAmount = 14.dp.toPx() * progress,
                                depthEffect = true,
                            )
                        },
                        highlight = {
                            Highlight.Default.copy(alpha = dragAnimation.pressProgress)
                        },
                        shadow = { Shadow(alpha = dragAnimation.pressProgress) },
                        innerShadow = {
                            val progress = dragAnimation.pressProgress
                            InnerShadow(
                                radius = 8.dp * progress,
                                color = Color.Black.copy(alpha = 0.15f),
                                alpha = progress,
                            )
                        },
                        onDrawSurface = {
                            val progress = dragAnimation.pressProgress
                            drawRect(selectionSurface, alpha = 1f - progress)
                            drawRect(Color.Black.copy(alpha = 0.03f * progress))
                        },
                    )
                } else {
                    Modifier.background(selectionSurface, barShape)
                }

            Box(
                modifier =
                    Modifier
                        .padding(horizontal = FloatingBarInnerPadding)
                        .graphicsLayer {
                            val progressOffset = dragAnimation.value * tabWidthPx
                            translationX = if (isLtr) progressOffset else -progressOffset
                            scaleX = dragAnimation.scaleX
                            scaleY = dragAnimation.scaleY
                            val velocity = dragAnimation.velocity / 10f
                            scaleX /= 1f - (velocity * 0.75f).coerceIn(-0.2f, 0.2f)
                            scaleY *= 1f - (velocity * 0.25f).coerceIn(-0.2f, 0.2f)
                        }.then(interactiveHighlight?.gestureModifier ?: Modifier)
                        .then(dragAnimation.modifier)
                        .then(pillSurfaceModifier)
                        .height(FloatingPillHeight)
                        .width(tabWidthDp),
            )
        }
    }
}

@Composable
private fun RowScope.FloatingBarItem(
    item: BottomNavItem,
    selected: Boolean,
    selectedColor: Color,
    unselectedColor: Color,
    onSelect: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val tabScale = LocalFloatingBarTabScale.current
    val contentColor by
        animateColorAsState(
            targetValue = if (selected) selectedColor else unselectedColor,
            animationSpec = tween(180),
            label = "floatingTabContentColor",
        )
    Column(
        modifier =
            modifier
                .selectable(
                    selected = selected,
                    interactionSource = null,
                    indication = null,
                    role = Role.Tab,
                    onClick = onSelect,
                ).fillMaxHeight()
                .weight(1f)
                .graphicsLayer {
                    val scale = tabScale()
                    scaleX = scale
                    scaleY = scale
                },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(1.dp, Alignment.CenterVertically),
    ) {
        Crossfade(
            targetState = selected,
            animationSpec = tween(180),
            label = "floatingTabIcon",
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
            fontSize = 11.sp,
            lineHeight = 14.sp,
            fontWeight = FontWeight.Medium,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

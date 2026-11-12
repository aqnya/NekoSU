// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.navigation

import androidx.compose.animation.Crossfade
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.EaseOut
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
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
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.luminance
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
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

/** The two floating navigation treatments share their geometry and gestures. */
internal enum class LiquidBarVariant {
    /** BakaSU's floating dock: neutral tabs with the accent revealed through the glass pill. */
    FLOATING,

    /** NekoSU's compact liquid-glass bar: the selected tab carries the accent directly. */
    GLASS,
}

private val LiquidBarInnerPadding = 4.dp
private val LiquidPillHeight = 56.dp
private val LiquidTabIdealWidth = 76.dp
private val LiquidTabHorizontalPadding = 2.dp

/** Tab scale is read from a graphics layer so presses do not recompose the whole bar. */
private val LocalLiquidBarTabScale = staticCompositionLocalOf<() -> Float> { { 1f } }

@Composable
internal fun LiquidBottomNavigationBar(
    variant: LiquidBarVariant,
    backdrop: Backdrop,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    require(items.isNotEmpty()) { "The liquid bottom bar needs at least one tab" }

    val scheme = MaterialTheme.colorScheme
    val isDark = scheme.surfaceContainer.luminance() < 0.5f
    val barShape = ContinuousCapsule
    val accentColor = scheme.primary
    val tabContentColor = scheme.onSurface
    val containerColor = scheme.surfaceContainer.copy(alpha = 0.40f)
    val selectionSurface =
        if (isDark) Color.White.copy(alpha = 0.10f) else Color.Black.copy(alpha = 0.10f)
    val selectedTintThroughGlass = variant == LiquidBarVariant.FLOATING
    val dragsFromAnywhere = variant == LiquidBarVariant.FLOATING
    val baseSelectedColor = if (selectedTintThroughGlass) tabContentColor else accentColor

    val tabsBackdrop = rememberLayerBackdrop()
    val pillBackdrop = rememberCombinedBackdrop(backdrop, tabsBackdrop)

    BoxWithConstraints(
        modifier = modifier,
        contentAlignment = Alignment.Center,
    ) {
        // The dock never asks for more width than the screen offers. With five
        // tabs the compact 76 dp columns shrink instead of running off-screen.
        val idealBarWidth = LiquidTabIdealWidth * items.size + LiquidBarInnerPadding * 2
        val barWidth = idealBarWidth.coerceAtMost(maxWidth)

        val density = LocalDensity.current
        val isLtr = LocalLayoutDirection.current == LayoutDirection.Ltr
        val animationScope = rememberCoroutineScope()

        val initialIndex = selectedIndex.coerceIn(0, items.lastIndex)
        var tabWidthPx by remember { mutableFloatStateOf(0f) }
        var totalWidthPx by remember { mutableFloatStateOf(0f) }
        var currentIndex by remember { mutableIntStateOf(initialIndex) }

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
        val lastIndex = items.lastIndex
        val innerPaddingPx = with(density) { LiquidBarInnerPadding.toPx() }

        fun indexAt(positionX: Float): Int {
            if (tabWidthPx <= 0f) return currentIndex
            val logicalX = if (isLtr) positionX else totalWidthPx - positionX
            return ((logicalX - innerPaddingPx) / tabWidthPx).toInt().coerceIn(0, lastIndex)
        }

        fun settlePanel() {
            animationScope.launch {
                offsetAnimation.animateTo(0f, spring(1f, 300f, 0.5f))
            }
        }

        val dragAnimation =
            remember(animationScope, items, density, isLtr) {
                DampedDragAnimation(
                    animationScope = animationScope,
                    initialValue = initialIndex.toFloat(),
                    valueRange = 0f..lastIndex.toFloat(),
                    visibilityThreshold = 0.001f,
                    initialScale = 1f,
                    pressedScale = if (variant == LiquidBarVariant.FLOATING) 1.24f else 1.14f,
                    canDrag = { offset -> offset.x in 0f..totalWidthPx },
                    // The floating dock selects the tab under the finger on
                    // touch-down so the whole bar is draggable. The glass bar
                    // keeps its pill as the drag handle and lets a tab tap
                    // navigate before the pill springs to it.
                    onDragStarted = { position ->
                        if (dragsFromAnywhere) {
                            updateValue(indexAt(position.x).toFloat())
                        }
                    },
                    onDragStopped = {
                        val targetIndex = targetValue.roundToInt().coerceIn(0, lastIndex)
                        if (targetIndex != currentIndex) {
                            currentIndex = targetIndex
                            onTabClickUpdated(targetIndex)
                        }
                        animateToValue(targetIndex.toFloat())
                        settlePanel()
                    },
                    onDragCancelled = {
                        animateToValue(currentIndex.toFloat())
                        settlePanel()
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
            val targetIndex = selectedIndex.coerceIn(0, lastIndex)
            if (currentIndex != targetIndex) {
                currentIndex = targetIndex
                dragAnimation.animateToValue(targetIndex.toFloat())
            }
        }

        LaunchedEffect(items.size) {
            val targetIndex = currentIndex.coerceIn(0, lastIndex)
            if (targetIndex != currentIndex) {
                currentIndex = targetIndex
                dragAnimation.animateToValue(targetIndex.toFloat())
            }
        }

        fun activateTab(index: Int) {
            if (index !in items.indices) return
            if (currentIndex != index) {
                currentIndex = index
                onTabClickUpdated(index)
            }
            dragAnimation.animateToValue(index.toFloat())
        }

        val interactiveHighlight =
            remember(animationScope, tabWidthPx, dragAnimation, isLtr, innerPaddingPx) {
                InteractiveHighlight(animationScope) { size, _ ->
                    val centerX = innerPaddingPx + (dragAnimation.value + 0.5f) * tabWidthPx
                    Offset(
                        x = if (isLtr) centerX else size.width - centerX,
                        y = size.height / 2f,
                    )
                }
            }

        val tabScale =
            remember(dragAnimation, variant) {
                {
                    lerp(
                        1f,
                        if (variant == LiquidBarVariant.FLOATING) 1.18f else 1.10f,
                        dragAnimation.pressProgress,
                    )
                }
            }

        Box(
            modifier =
                Modifier
                    .width(barWidth)
                    .height(LiquidBottomBarHeight),
            contentAlignment = Alignment.CenterStart,
        ) {
            Row(
                modifier =
                    Modifier
                        .fillMaxSize()
                        .onGloballyPositioned { coordinates ->
                            totalWidthPx = coordinates.size.width.toFloat()
                            tabWidthPx =
                                ((totalWidthPx - innerPaddingPx * 2f) / items.size).coerceAtLeast(0f)
                        }.graphicsLayer { translationX = panelOffset }
                        .then(
                            if (selectedTintThroughGlass) {
                                Modifier
                            } else {
                                // The glass bar records its own tab row so the
                                // pill can refract the selected icon and label.
                                Modifier.layerBackdrop(tabsBackdrop)
                            },
                        ).liquidBarSurface(
                            backdrop = backdrop,
                            shape = barShape,
                            containerColor = containerColor,
                            isDark = isDark,
                            blurRadius = if (variant == LiquidBarVariant.FLOATING) 4.dp else 8.dp,
                            lensHeight = if (variant == LiquidBarVariant.FLOATING) 22.dp else 24.dp,
                            lensAmount = 24.dp,
                            pressGrowth = if (variant == LiquidBarVariant.FLOATING) 14.dp else 10.dp,
                            dragAnimation = dragAnimation,
                        ).selectableGroup()
                        .then(interactiveHighlight.modifier)
                        .then(if (dragsFromAnywhere) interactiveHighlight.gestureModifier else Modifier)
                        .then(if (dragsFromAnywhere) dragAnimation.modifier else Modifier)
                        .padding(LiquidBarInnerPadding),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                CompositionLocalProvider(LocalLiquidBarTabScale provides tabScale) {
                    items.forEachIndexed { index, item ->
                        LiquidBarTab(
                            item = item,
                            selected = index == currentIndex,
                            contentColor =
                                if (index == currentIndex) baseSelectedColor else tabContentColor,
                            onSelect = { activateTab(index) },
                            selectable = !dragsFromAnywhere,
                        )
                    }
                }
            }

            if (selectedTintThroughGlass) {
                // The hidden accent clone is recorded into its own layer so the
                // moving pill can refract the selected icon and label instead of
                // simply painting an accent tint on top of them.
                Row(
                    modifier =
                        Modifier
                            .padding(horizontal = LiquidBarInnerPadding)
                            .fillMaxWidth()
                            .height(LiquidPillHeight)
                            .graphicsLayer { translationX = panelOffset }
                            .alpha(0f)
                            .layerBackdrop(tabsBackdrop)
                            .drawBackdrop(
                                backdrop = backdrop,
                                shape = { barShape },
                                effects = {
                                    vibrancy()
                                    blur(4.dp.toPx())
                                    lens(22.dp.toPx(), 24.dp.toPx())
                                },
                                onDrawSurface = { drawRect(containerColor) },
                            ),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    CompositionLocalProvider(LocalLiquidBarTabScale provides tabScale) {
                        items.forEachIndexed { index, item ->
                            LiquidBarAccentTab(
                                item = item,
                                selected = index == currentIndex,
                            )
                        }
                    }
                }
            }

            if (tabWidthPx > 0f) {
                val tabWidthDp = with(density) { tabWidthPx.toDp() }
                Box(
                    modifier =
                        Modifier
                            .graphicsLayer {
                                val progressOffset = dragAnimation.value * tabWidthPx
                                val x = innerPaddingPx + progressOffset
                                translationX =
                                    if (isLtr) x else totalWidthPx - x - tabWidthPx
                                scaleX = dragAnimation.scaleX
                                scaleY = dragAnimation.scaleY
                                val velocity = dragAnimation.velocity / 10f
                                scaleX /= 1f - (velocity * 0.75f).coerceIn(-0.2f, 0.2f)
                                scaleY *= 1f - (velocity * 0.25f).coerceIn(-0.2f, 0.2f)
                            }.then(if (dragsFromAnywhere) Modifier else interactiveHighlight.gestureModifier)
                            .then(if (dragsFromAnywhere) Modifier else dragAnimation.modifier)
                            .liquidPillSurface(
                                backdrop = pillBackdrop,
                                shape = barShape,
                                selectionSurface = selectionSurface,
                                variant = variant,
                                dragAnimation = dragAnimation,
                            ).width(tabWidthDp)
                            .height(LiquidPillHeight),
                )
            }
        }
    }
}

private fun Modifier.liquidBarSurface(
    backdrop: Backdrop,
    shape: Shape,
    containerColor: Color,
    isDark: Boolean,
    blurRadius: Dp,
    lensHeight: Dp,
    lensAmount: Dp,
    pressGrowth: Dp,
    dragAnimation: DampedDragAnimation,
): Modifier =
    drawBackdrop(
        backdrop = backdrop,
        shape = { shape },
        effects = {
            padding = maxOf(padding, 40.dp.toPx())
            vibrancy()
            blur(blurRadius.toPx())
            lens(lensHeight.toPx(), lensAmount.toPx())
        },
        highlight = { Highlight.Default.copy(alpha = if (isDark) 0.55f else 0.9f) },
        shadow = {
            Shadow(
                radius = 20.dp,
                offset = DpOffset(0.dp, 7.dp),
                color = Color.Black.copy(alpha = if (isDark) 0.28f else 0.14f),
            )
        },
        layerBlock = {
            val width = size.width.coerceAtLeast(1f)
            val scale = lerp(1f, 1f + pressGrowth.toPx() / width, dragAnimation.pressProgress)
            scaleX = scale
            scaleY = scale
        },
        onDrawSurface = { drawRect(containerColor) },
    )

private fun Modifier.liquidPillSurface(
    backdrop: Backdrop,
    shape: Shape,
    selectionSurface: Color,
    variant: LiquidBarVariant,
    dragAnimation: DampedDragAnimation,
): Modifier =
    drawBackdrop(
        backdrop = backdrop,
        shape = { shape },
        effects = {
            val progress = dragAnimation.pressProgress
            val refractionHeight = if (variant == LiquidBarVariant.FLOATING) 12.dp else 10.dp
            val refractionAmount = if (variant == LiquidBarVariant.FLOATING) 16.dp else 14.dp
            lens(
                refractionHeight = refractionHeight.toPx() * progress,
                refractionAmount = refractionAmount.toPx() * progress,
                depthEffect = true,
                chromaticAberration = true,
            )
        },
        highlight = { Highlight.Default.copy(alpha = 0.8f * dragAnimation.pressProgress) },
        shadow = {
            Shadow(
                radius = 12.dp,
                offset = DpOffset(0.dp, 4.dp),
                color = Color.Black,
                alpha = 0.18f * dragAnimation.pressProgress,
            )
        },
        innerShadow = {
            InnerShadow(
                radius = 8.dp * dragAnimation.pressProgress,
                color = Color.Black,
                alpha = 0.15f * dragAnimation.pressProgress,
            )
        },
        onDrawSurface = {
            val progress = dragAnimation.pressProgress
            drawRect(selectionSurface, alpha = 1f - 0.15f * progress)
            drawRect(Color.Black, alpha = 0.03f * progress)
        },
    )

@Composable
private fun RowScope.LiquidBarTab(
    item: BottomNavItem,
    selected: Boolean,
    contentColor: Color,
    onSelect: () -> Unit,
    selectable: Boolean,
) {
    val tabScale = LocalLiquidBarTabScale.current
    val color by
        animateColorAsState(
            targetValue = contentColor,
            animationSpec = tween(180),
            label = "liquidTabContentColor",
        )
    val interactionModifier =
        if (selectable) {
            Modifier.selectable(
                selected = selected,
                interactionSource = null,
                indication = null,
                role = Role.Tab,
                onClick = onSelect,
            )
        } else {
            Modifier
                .semantics(mergeDescendants = true) {
                    this.selected = selected
                    role = Role.Tab
                    onClick {
                        onSelect()
                        true
                    }
                }.onKeyEvent { event ->
                    val activationKey =
                        event.key == Key.Enter ||
                            event.key == Key.NumPadEnter ||
                            event.key == Key.Spacebar
                    if (activationKey) {
                        if (event.type == KeyEventType.KeyUp) onSelect()
                        true
                    } else {
                        false
                    }
                }.focusable()
        }
    Column(
        modifier =
            Modifier
                .then(interactionModifier)
                .fillMaxHeight()
                .weight(1f)
                .padding(horizontal = LiquidTabHorizontalPadding)
                .graphicsLayer {
                    val scale = tabScale()
                    scaleX = scale
                    scaleY = scale
                },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(1.dp, Alignment.CenterVertically),
    ) {
        LiquidBarTabContent(
            item = item,
            selected = selected,
            color = color,
        )
    }
}

@Composable
private fun RowScope.LiquidBarAccentTab(
    item: BottomNavItem,
    selected: Boolean,
) {
    val tabScale = LocalLiquidBarTabScale.current
    val color = MaterialTheme.colorScheme.primary
    Column(
        modifier =
            Modifier
                .clearAndSetSemantics {}
                .fillMaxHeight()
                .weight(1f)
                .padding(horizontal = LiquidTabHorizontalPadding)
                .graphicsLayer {
                    val scale = tabScale()
                    scaleX = scale
                    scaleY = scale
                },
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(1.dp, Alignment.CenterVertically),
    ) {
        LiquidBarTabContent(
            item = item,
            selected = selected,
            color = color,
        )
    }
}

@Composable
private fun LiquidBarTabContent(
    item: BottomNavItem,
    selected: Boolean,
    color: Color,
) {
    Crossfade(
        targetState = selected,
        animationSpec = tween(180),
        label = "liquidTabIcon",
    ) { isSelected ->
        Icon(
            imageVector = if (isSelected) item.selectedIcon else item.unselectedIcon,
            contentDescription = stringResource(item.titleRes),
            tint = color,
            modifier = Modifier.size(24.dp),
        )
    }

    Text(
        text = stringResource(item.titleRes),
        color = color,
        style = MaterialTheme.typography.labelSmall,
        fontSize = 11.sp,
        lineHeight = 14.sp,
        fontWeight = FontWeight.Medium,
        maxLines = 1,
        softWrap = false,
        overflow = TextOverflow.Ellipsis,
    )
}

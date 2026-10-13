package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import me.nekosu.aqnya.ui.navigation.BottomNavItem

/**
 * 带动画的底部导航栏
 * @param animationSpeed 动画速度倍率（默认1.0）
 */
@Composable
fun AnimatedBottomNavBar(
    visible: Boolean,
    items: List<BottomNavItem>,
    selectedIndex: Int,
    onTabClick: (Int) -> Unit,
    modifier: Modifier = Modifier,
    animationSpeed: Float = 1.0f,
    content: @Composable (List<BottomNavItem>, Int, (Int) -> Unit) -> Unit,
) {
    FadeSlideNavBar(
        visible = visible,
        modifier = modifier,
        animationSpeed = animationSpeed,
    ) {
        content(items, selectedIndex, onTabClick)
    }
}

@Composable
private fun FadeSlideNavBar(
    visible: Boolean,
    modifier: Modifier = Modifier,
    animationSpeed: Float = 1.0f,
    content: @Composable () -> Unit,
) {
    val durationIn = (200f / animationSpeed).toInt().coerceIn(50, 600)
    val durationOut = (150f / animationSpeed).toInt().coerceIn(50, 600)

    val animatedAlpha by animateFloatAsState(
        targetValue = if (visible) 1f else 0f,
        animationSpec =
            tween(
                durationMillis = if (visible) durationIn else durationOut,
            ),
        label = "fadeSlideNavBarAlpha",
    )

    Box(
        modifier =
            modifier.graphicsLayer {
                alpha = animatedAlpha
                translationY = (1f - animatedAlpha) * size.height
            },
    ) {
        content()
    }
}

package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut

/**
 * 页面路由过渡动画
 */
object PageTransitions {
    val defaultEnter = fadeIn(tween(300))
    val defaultExit = fadeOut(tween(300))
}

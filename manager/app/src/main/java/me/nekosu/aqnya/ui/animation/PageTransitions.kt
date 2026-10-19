package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.material3.MotionScheme

/**
 * 页面路由过渡动画
 */
object PageTransitions {
    fun defaultEnter(motionScheme: MotionScheme): EnterTransition = fadeIn(animationSpec = motionScheme.defaultEffectsSpec())

    fun defaultExit(motionScheme: MotionScheme): ExitTransition = fadeOut(animationSpec = motionScheme.fastEffectsSpec())
}

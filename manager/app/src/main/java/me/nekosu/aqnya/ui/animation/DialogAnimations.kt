package me.nekosu.aqnya.ui.animation

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable

/**
 * 对话框动画配置
 */
object DialogAnimations {
    /**
     * 创建对话框进入动画
     */
    @Composable
    fun createEnterAnimation(): EnterTransition =
        fadeIn(animationSpec = MaterialTheme.motionScheme.defaultEffectsSpec()) +
            scaleIn(
                initialScale = 0.8f,
                animationSpec = MaterialTheme.motionScheme.defaultSpatialSpec(),
            )

    /**
     * 创建对话框退出动画
     */
    @Composable
    fun createExitAnimation(): ExitTransition =
        fadeOut(animationSpec = MaterialTheme.motionScheme.fastEffectsSpec()) +
            scaleOut(
                targetScale = 0.8f,
                animationSpec = MaterialTheme.motionScheme.fastSpatialSpec(),
            )
}

/**
 * 带动画的 AlertDialog
 */
@Composable
fun AnimatedAlertDialog(
    visible: Boolean,
    onDismiss: () -> Unit,
    title: @Composable () -> Unit,
    text: @Composable () -> Unit,
    confirmButton: @Composable () -> Unit,
    dismissButton: @Composable (() -> Unit)? = null,
) {
    AnimatedVisibility(
        visible = visible,
        enter = DialogAnimations.createEnterAnimation(),
        exit = DialogAnimations.createExitAnimation(),
    ) {
        AlertDialog(
            onDismissRequest = onDismiss,
            title = title,
            text = text,
            confirmButton = confirmButton,
            dismissButton = dismissButton?.let { btn -> { btn() } },
        )
    }
}

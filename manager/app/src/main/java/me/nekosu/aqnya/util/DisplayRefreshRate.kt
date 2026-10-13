package me.nekosu.aqnya.util

import android.app.Activity
import android.os.Build

/**
 * ROM 会按应用决定刷新率，未主动声明的应用常被压到 60Hz。这里让窗口偏好设备支持的
 * 最高刷新率，并关闭 Android 15+ 的省电降帧。
 */
object DisplayRefreshRate {
    fun requestHighest(activity: Activity) {
        val display = activity.display ?: return
        val best =
            display.supportedModes
                .filter { it.refreshRate > 0f }
                .maxByOrNull { it.refreshRate }
                ?: return

        val window = activity.window
        window.attributes =
            window.attributes.apply {
                preferredRefreshRate = best.refreshRate
                // 不锁具体 modeId：由系统在候选模式中自行选择，避免被高刷黑名单
                // 按固定模式锁死，也避免顺带改变分辨率。
                preferredDisplayModeId = 0
            }

        // Android 15+：关闭省电降帧、保留触摸升帧。（受系统特性开关保护，为空操作。）
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
            window.setFrameRatePowerSavingsBalanced(false)
            window.setFrameRateBoostOnTouchEnabled(true)
        }
    }
}

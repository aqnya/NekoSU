package me.nekosu.aqnya.util

import android.app.Activity
import android.os.Build

/**
 * 只放宽 Android 15+ 的省电降帧策略，**不**声明窗口偏好刷新率/模式。
 *
 * 背景（实测 HyperOS / Android 16，1440x3200 60/120/90Hz）：
 * 只要窗口写了 `WindowManager.LayoutParams.preferredRefreshRate`，SurfaceFlinger
 * 就把整块屏切到基础模式 id 0（60Hz）：
 *
 *   - 打开 NekoSU  -> dumpsys SurfaceFlinger: activeMode id=0 vsyncRate=60Hz
 *   - 打开 KernelSU-> activeMode id=1 vsyncRate=120Hz
 *
 * 两个窗口的 LayoutParams 只差一项：NekoSU 有
 * `preferredRefreshRate=120.00001`，KernelSU 什么都不写。也就是说，主动“请求高刷”
 * 反而被系统降到了 60Hz；不声明则保持默认 120Hz。
 *
 * 因此这里不再写 preferredRefreshRate / preferredDisplayModeId，与 KernelSU 一致，
 * 只保留对帧率策略（非 display mode）的放宽。
 */
object DisplayRefreshRate {
    fun requestHighest(activity: Activity) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.VANILLA_ICE_CREAM) return

        val window = activity.window
        // 这两项只影响帧率策略，不会锁定 display mode。
        window.setFrameRatePowerSavingsBalanced(false)
        window.setFrameRateBoostOnTouchEnabled(true)
    }
}

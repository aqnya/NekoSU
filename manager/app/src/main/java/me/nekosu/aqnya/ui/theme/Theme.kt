// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.theme

import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.ColorScheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MotionScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import me.nekosu.aqnya.data.prefs.AppPreferences

// ============================================================
// 三个独立配色方案 — 不再交换色值，各自硬编码
// ============================================================

private fun staticDark(v: Int) =
    (when (v) {
        2 -> {
            darkColorScheme( // 紫
                primary = SecondaryDark,
                onPrimary = OnSecondaryDark,
                primaryContainer = SecondaryContainerDark,
                onPrimaryContainer = OnSecondaryContainerDark,
                secondary = PrimaryDark,
                onSecondary = OnPrimaryDark,
                secondaryContainer = PrimaryContainerDark,
                onSecondaryContainer = OnPrimaryContainerDark,
                tertiary = TertiaryDark,
                onTertiary = OnTertiaryDark,
                tertiaryContainer = TertiaryContainerDark,
                onTertiaryContainer = OnTertiaryContainerDark,
                background = Color(0xFF1C1A26),
                surface = Color(0xFF1C1A26),
                surfaceVariant = Color(0xFF2C2836),
                surfaceContainerLowest = Color(0xFF1C1A26),
                surfaceContainerLow = Color(0xFF1C1A26),
                surfaceContainer = Color(0xFF2C2836),
                surfaceContainerHigh = Color(0xFF2C2836),
                surfaceContainerHighest = Color(0xFF2C2836),
            )
        }

        3 -> {
            darkColorScheme( // 绿
                primary = TertiaryDark,
                onPrimary = OnTertiaryDark,
                primaryContainer = TertiaryContainerDark,
                onPrimaryContainer = OnTertiaryContainerDark,
                secondary = SecondaryDark,
                onSecondary = OnSecondaryDark,
                secondaryContainer = SecondaryContainerDark,
                onSecondaryContainer = OnSecondaryContainerDark,
                tertiary = PrimaryDark,
                onTertiary = OnPrimaryDark,
                tertiaryContainer = PrimaryContainerDark,
                onTertiaryContainer = OnPrimaryContainerDark,
                background = Color(0xFF18201E),
                surface = Color(0xFF18201E),
                surfaceVariant = Color(0xFF242E2A),
                surfaceContainerLowest = Color(0xFF18201E),
                surfaceContainerLow = Color(0xFF18201E),
                surfaceContainer = Color(0xFF242E2A),
                surfaceContainerHigh = Color(0xFF242E2A),
                surfaceContainerHighest = Color(0xFF242E2A),
            )
        }

        else -> {
            darkColorScheme( // 蓝
                primary = PrimaryDark,
                onPrimary = OnPrimaryDark,
                primaryContainer = PrimaryContainerDark,
                onPrimaryContainer = OnPrimaryContainerDark,
                secondary = SecondaryDark,
                onSecondary = OnSecondaryDark,
                secondaryContainer = SecondaryContainerDark,
                onSecondaryContainer = OnSecondaryContainerDark,
                tertiary = TertiaryDark,
                onTertiary = OnTertiaryDark,
                tertiaryContainer = TertiaryContainerDark,
                onTertiaryContainer = OnTertiaryContainerDark,
                background = Color(0xFF181B24),
                surface = Color(0xFF181B24),
                surfaceVariant = Color(0xFF262B38),
                surfaceContainerLowest = Color(0xFF181B24),
                surfaceContainerLow = Color(0xFF181B24),
                surfaceContainer = Color(0xFF262B38),
                surfaceContainerHigh = Color(0xFF262B38),
                surfaceContainerHighest = Color(0xFF262B38),
            )
        }
    }).withNeutrals(darkNeutrals(v)).withSelectedAccent()

private fun staticLight(v: Int) =
    (when (v) {
        2 -> {
            lightColorScheme( // 紫
                primary = SecondaryLight,
                onPrimary = OnSecondaryLight,
                primaryContainer = SecondaryContainerLight,
                onPrimaryContainer = OnSecondaryContainerLight,
                secondary = PrimaryLight,
                onSecondary = OnPrimaryLight,
                secondaryContainer = PrimaryContainerLight,
                onSecondaryContainer = OnPrimaryContainerLight,
                tertiary = TertiaryLight,
                onTertiary = OnTertiaryLight,
                tertiaryContainer = TertiaryContainerLight,
                onTertiaryContainer = OnTertiaryContainerLight,
                background = Color(0xFFF8F6FC),
                surface = Color(0xFFF8F6FC),
                surfaceVariant = Color(0xFFEDE6F4),
                surfaceContainerLowest = Color(0xFFF8F6FC),
                surfaceContainerLow = Color(0xFFF8F6FC),
                surfaceContainer = Color(0xFFEDE6F4),
                surfaceContainerHigh = Color(0xFFEDE6F4),
                surfaceContainerHighest = Color(0xFFEDE6F4),
            )
        }

        3 -> {
            lightColorScheme( // 绿
                primary = TertiaryLight,
                onPrimary = OnTertiaryLight,
                primaryContainer = TertiaryContainerLight,
                onPrimaryContainer = OnTertiaryContainerLight,
                secondary = SecondaryLight,
                onSecondary = OnSecondaryLight,
                secondaryContainer = SecondaryContainerLight,
                onSecondaryContainer = OnSecondaryContainerLight,
                tertiary = PrimaryLight,
                onTertiary = OnPrimaryLight,
                tertiaryContainer = PrimaryContainerLight,
                onTertiaryContainer = OnPrimaryContainerLight,
                background = Color(0xFFF6FAF8),
                surface = Color(0xFFF6FAF8),
                surfaceVariant = Color(0xFFE2EDE6),
                surfaceContainerLowest = Color(0xFFF6FAF8),
                surfaceContainerLow = Color(0xFFF6FAF8),
                surfaceContainer = Color(0xFFE2EDE6),
                surfaceContainerHigh = Color(0xFFE2EDE6),
                surfaceContainerHighest = Color(0xFFE2EDE6),
            )
        }

        else -> {
            lightColorScheme( // 蓝
                primary = PrimaryLight,
                onPrimary = OnPrimaryLight,
                primaryContainer = PrimaryContainerLight,
                onPrimaryContainer = OnPrimaryContainerLight,
                secondary = SecondaryLight,
                onSecondary = OnSecondaryLight,
                secondaryContainer = SecondaryContainerLight,
                onSecondaryContainer = OnSecondaryContainerLight,
                tertiary = TertiaryLight,
                onTertiary = OnTertiaryLight,
                tertiaryContainer = TertiaryContainerLight,
                onTertiaryContainer = OnTertiaryContainerLight,
                background = Color(0xFFF8FAFE),
                surface = Color(0xFFF8FAFE),
                surfaceVariant = Color(0xFFE4EAF4),
                surfaceContainerLowest = Color(0xFFF8FAFE),
                surfaceContainerLow = Color(0xFFF8FAFE),
                surfaceContainer = Color(0xFFE4EAF4),
                surfaceContainerHigh = Color(0xFFE4EAF4),
                surfaceContainerHighest = Color(0xFFE4EAF4),
            )
        }
    }).withNeutrals(lightNeutrals(v)).withSelectedAccent()

private data class NeutralPalette(
    val surfaceBright: Color,
    val surfaceDim: Color,
    val onSurface: Color,
    val onSurfaceVariant: Color,
    val outline: Color,
    val outlineVariant: Color,
    val inverseSurface: Color,
    val inverseOnSurface: Color,
    val inversePrimary: Color,
)

private fun darkNeutrals(v: Int) =
    when (v) {
        2 -> {
            NeutralPalette(
                surfaceBright = Color(0xFF3A3446),
                surfaceDim = Color(0xFF15131D),
                onSurface = Color(0xFFE9E3F1),
                onSurfaceVariant = Color(0xFFC5BCD3),
                outline = Color(0xFF8B8198),
                outlineVariant = Color(0xFF474050),
                inverseSurface = Color(0xFFE9E3F1),
                inverseOnSurface = Color(0xFF2A2533),
                inversePrimary = SecondaryLight,
            )
        }

        3 -> {
            NeutralPalette(
                surfaceBright = Color(0xFF313B36),
                surfaceDim = Color(0xFF121816),
                onSurface = Color(0xFFE2E9E5),
                onSurfaceVariant = Color(0xFFB8C5BD),
                outline = Color(0xFF7E8E85),
                outlineVariant = Color(0xFF3D4A43),
                inverseSurface = Color(0xFFE2E9E5),
                inverseOnSurface = Color(0xFF222A26),
                inversePrimary = TertiaryLight,
            )
        }

        else -> {
            NeutralPalette(
                surfaceBright = Color(0xFF323847),
                surfaceDim = Color(0xFF12151C),
                onSurface = Color(0xFFE4E7F0),
                onSurfaceVariant = Color(0xFFB7C0D2),
                outline = Color(0xFF808AA0),
                outlineVariant = Color(0xFF3C4557),
                inverseSurface = Color(0xFFE4E7F0),
                inverseOnSurface = Color(0xFF232833),
                inversePrimary = PrimaryLight,
            )
        }
    }

private fun lightNeutrals(v: Int) =
    when (v) {
        2 -> {
            NeutralPalette(
                surfaceBright = Color(0xFFFDFBFF),
                surfaceDim = Color(0xFFE8E3EE),
                onSurface = Color(0xFF211B28),
                onSurfaceVariant = Color(0xFF4F455C),
                outline = Color(0xFF817689),
                outlineVariant = Color(0xFFD1C7DB),
                inverseSurface = Color(0xFF322C3A),
                inverseOnSurface = Color(0xFFF4EEF9),
                inversePrimary = SecondaryDark,
            )
        }

        3 -> {
            NeutralPalette(
                surfaceBright = Color(0xFFFBFEFC),
                surfaceDim = Color(0xFFDEE9E3),
                onSurface = Color(0xFF19211D),
                onSurfaceVariant = Color(0xFF45544C),
                outline = Color(0xFF77877E),
                outlineVariant = Color(0xFFC6D2CA),
                inverseSurface = Color(0xFF2A332E),
                inverseOnSurface = Color(0xFFF0F5F2),
                inversePrimary = TertiaryDark,
            )
        }

        else -> {
            NeutralPalette(
                surfaceBright = Color(0xFFFCFDFF),
                surfaceDim = Color(0xFFE1E6F0),
                onSurface = Color(0xFF1A1D24),
                onSurfaceVariant = Color(0xFF4B5364),
                outline = Color(0xFF7A8395),
                outlineVariant = Color(0xFFC9D1DF),
                inverseSurface = Color(0xFF2D3038),
                inverseOnSurface = Color(0xFFF1F2F6),
                inversePrimary = PrimaryDark,
            )
        }
    }

private fun ColorScheme.withNeutrals(palette: NeutralPalette) =
    copy(
        onBackground = palette.onSurface,
        onSurface = palette.onSurface,
        onSurfaceVariant = palette.onSurfaceVariant,
        surfaceBright = palette.surfaceBright,
        surfaceDim = palette.surfaceDim,
        outline = palette.outline,
        outlineVariant = palette.outlineVariant,
        inverseSurface = palette.inverseSurface,
        inverseOnSurface = palette.inverseOnSurface,
        inversePrimary = palette.inversePrimary,
        surfaceTint = this.primary,
    )

private fun ColorScheme.withSelectedAccent() =
    copy(
        secondary = primary.blend(Color.White, 0.12f),
        onSecondary = onPrimary,
        secondaryContainer = primaryContainer.blend(Color.White, 0.12f),
        onSecondaryContainer = onPrimaryContainer,
        tertiary = primary.blend(Color.Black, 0.12f),
        onTertiary = onPrimary,
        tertiaryContainer = primaryContainer.blend(Color.Black, 0.12f),
        onTertiaryContainer = onPrimaryContainer,
    )

/**
 * AMOLED：页面底色关到底。页面用的是 [ColorScheme.surfaceContainer]、
 * 卡片用 [ColorScheme.surfaceBright]，所以容器色也要一起压黑，页面才会是纯黑
 * （与 KernelSU 的 `amoledBackground()` 一致）。
 */
private fun ColorScheme.amoledBlack(): ColorScheme =
    copy(
        background = AmoledBlack,
        surface = AmoledBlack,
        surfaceDim = AmoledBlack,
        surfaceContainerLowest = AmoledBlack,
        surfaceContainerLow = AmoledBlack,
        surfaceContainer = AmoledBlack,
        surfaceContainerHigh = AmoledBlack,
        surfaceContainerHighest = AmoledBlack,
    )

@Composable
fun NekosuTheme(content: @Composable () -> Unit) {
    val context = LocalContext.current
    val prefs = remember { context.getSharedPreferences(AppPreferences.PREF_NAME, android.content.Context.MODE_PRIVATE) }

    val themePreference by AppPreferences.themeModeFlow(context).collectAsState(initial = prefs.getInt("theme_mode", 0))

    var themeColorValue by remember { mutableIntStateOf(prefs.getInt("theme_color", 0)) }
    DisposableEffect(prefs) {
        val listener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == "theme_color") themeColorValue = prefs.getInt("theme_color", 0)
            }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }

    var amoledEnabled by remember { mutableStateOf(prefs.getBoolean("amoled_mode", false)) }
    DisposableEffect(prefs) {
        val listener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
                if (key == "amoled_mode") amoledEnabled = prefs.getBoolean("amoled_mode", false)
            }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }

    val useDynamicColor = themeColorValue == 0
    val isDarkTheme =
        when (themePreference) {
            1 -> false
            2 -> true
            else -> isSystemInDarkTheme()
        }

    val colorScheme =
        when {
            amoledEnabled && isDarkTheme && useDynamicColor -> {
                dynamicDarkColorScheme(context).amoledBlack()
            }

            amoledEnabled && isDarkTheme -> {
                staticDark(themeColorValue).amoledBlack()
            }

            useDynamicColor -> {
                if (isDarkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
            }

            isDarkTheme -> {
                staticDark(themeColorValue)
            }

            else -> {
                staticLight(themeColorValue)
            }
        }

    // Edge-to-Edge 系统栏样式
    EdgeToEdgeSystemBars(darkMode = isDarkTheme)

    MaterialTheme(
        colorScheme = colorScheme,
        motionScheme = MotionScheme.expressive(),
        typography = Typography,
        content = content,
    )
}

// ============================================================
// Edge-to-Edge 沉浸式系统栏
// ============================================================

@Composable
private fun EdgeToEdgeSystemBars(
    darkMode: Boolean,
    statusBarScrim: Color = Color.Transparent,
    navigationBarScrim: Color = Color.Transparent,
) {
    val context = LocalContext.current
    val activity = context as? ComponentActivity ?: return

    LaunchedEffect(activity, darkMode) {
        activity.enableEdgeToEdge(
            statusBarStyle =
                SystemBarStyle.auto(
                    statusBarScrim.toArgb(),
                    statusBarScrim.toArgb(),
                ) { darkMode },
            navigationBarStyle =
                when {
                    darkMode -> {
                        SystemBarStyle.dark(navigationBarScrim.toArgb())
                    }

                    else -> {
                        SystemBarStyle.light(
                            navigationBarScrim.toArgb(),
                            navigationBarScrim.toArgb(),
                        )
                    }
                },
        )
    }
}

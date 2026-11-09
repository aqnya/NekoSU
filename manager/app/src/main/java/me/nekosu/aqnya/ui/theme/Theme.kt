// SPDX-License-Identifier: GPL-3.0-or-later
package me.nekosu.aqnya.ui.theme

import android.content.Context
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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.lerp
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import me.nekosu.aqnya.data.prefs.AppPreferences

private const val KEY_THEME_MODE = "theme_mode"
private const val KEY_THEME_COLOR = "theme_color"
private const val KEY_AMOLED = "amoled_mode"

/** Exact primary family kept from the original Catppuccin palettes. */
private data class AccentPalette(
    val primary: Color,
    val onPrimary: Color,
    val primaryContainer: Color,
    val onPrimaryContainer: Color,
)

/** Hue/saturation pair from which the neutral and tertiary tones are derived. */
private data class ThemeSeed(
    val hue: Float,
    val saturation: Float,
    val light: AccentPalette,
    val dark: AccentPalette,
) {
    fun color(
        lightness: Float,
        saturationScale: Float = 1f,
        hueShift: Float = 0f,
    ): Color {
        val shiftedHue = ((hue + hueShift) % 360f + 360f) % 360f
        return Color.hsl(
            shiftedHue,
            (saturation * saturationScale).coerceIn(0f, 1f),
            lightness.coerceIn(0f, 1f),
        )
    }

    fun neutral(lightness: Float): Color =
        Color.hsl(hue, 0.06f, lightness.coerceIn(0f, 1f))
}

private val BLUE_THEME =
    ThemeSeed(
        hue = 219.6f,
        saturation = 0.46f,
        light =
            AccentPalette(
                primary = Color(0xFF5B7EC2),
                onPrimary = Color.White,
                primaryContainer = Color(0xFFD8E2FF),
                onPrimaryContainer = Color(0xFF001A40),
            ),
        dark =
            AccentPalette(
                primary = Color(0xFF8AADF4),
                onPrimary = Color(0xFF001A40),
                primaryContainer = Color(0xFF2A4A7E),
                onPrimaryContainer = Color(0xFFD8E2FF),
            ),
    )

private val LAVENDER_THEME =
    ThemeSeed(
        hue = 270.5f,
        saturation = 0.24f,
        light =
            AccentPalette(
                primary = Color(0xFF8A6DA6),
                onPrimary = Color.White,
                primaryContainer = Color(0xFFF1DBFF),
                onPrimaryContainer = Color(0xFF2F174B),
            ),
        dark =
            AccentPalette(
                primary = Color(0xFFCEB5E4),
                onPrimary = Color(0xFF341758),
                primaryContainer = Color(0xFF5B4A73),
                onPrimaryContainer = Color(0xFFF1DBFF),
            ),
    )

private val GREEN_THEME =
    ThemeSeed(
        hue = 156.75f,
        saturation = 0.39f,
        light =
            AccentPalette(
                primary = Color(0xFF3E8E6F),
                onPrimary = Color.White,
                primaryContainer = Color(0xFFC0FAD7),
                onPrimaryContainer = Color(0xFF002115),
            ),
        dark =
            AccentPalette(
                primary = Color(0xFF8BD5B0),
                onPrimary = Color(0xFF003825),
                primaryContainer = Color(0xFF1E5E47),
                onPrimaryContainer = Color(0xFFC0FAD7),
            ),
    )

/** Values match [me.nekosu.aqnya.ui.screens.settings.enums.ThemeColor]. */
private fun seedFor(value: Int): ThemeSeed =
    when (value) {
        2 -> LAVENDER_THEME
        3 -> GREEN_THEME
        else -> BLUE_THEME
    }

private fun ColorScheme.withAccents(
    accent: AccentPalette,
    tertiaryShift: Color,
): ColorScheme {
    val tertiary = lerp(accent.primary, tertiaryShift, 0.08f)
    val tertiaryContainer = lerp(accent.primaryContainer, tertiaryShift, 0.08f)

    return copy(
        secondary = accent.primary,
        onSecondary = accent.onPrimary,
        secondaryContainer = accent.primaryContainer,
        onSecondaryContainer = accent.onPrimaryContainer,
        tertiary = tertiary,
        onTertiary = accent.onPrimary,
        tertiaryContainer = tertiaryContainer,
        onTertiaryContainer = accent.onPrimaryContainer,
        primaryFixed = accent.primaryContainer,
        primaryFixedDim = accent.primaryContainer,
        onPrimaryFixed = accent.onPrimaryContainer,
        onPrimaryFixedVariant = accent.onPrimaryContainer,
        secondaryFixed = accent.primaryContainer,
        secondaryFixedDim = accent.primaryContainer,
        onSecondaryFixed = accent.onPrimaryContainer,
        onSecondaryFixedVariant = accent.onPrimaryContainer,
        tertiaryFixed = tertiaryContainer,
        tertiaryFixedDim = tertiaryContainer,
        onTertiaryFixed = accent.onPrimaryContainer,
        onTertiaryFixedVariant = accent.onPrimaryContainer,
    )
}

private fun lightScheme(seed: ThemeSeed): ColorScheme {
    val accent = seed.light
    val surface = seed.neutral(0.98f)
    val onSurface = seed.neutral(0.10f)

    return lightColorScheme(
        primary = accent.primary,
        onPrimary = accent.onPrimary,
        primaryContainer = accent.primaryContainer,
        onPrimaryContainer = accent.onPrimaryContainer,
        inversePrimary = seed.dark.primary,
        background = surface,
        onBackground = onSurface,
        surface = surface,
        onSurface = onSurface,
        surfaceVariant = seed.neutral(0.92f),
        onSurfaceVariant = seed.neutral(0.30f),
        surfaceTint = accent.primary,
        inverseSurface = seed.neutral(0.20f),
        inverseOnSurface = seed.neutral(0.95f),
        outline = seed.neutral(0.50f),
        outlineVariant = seed.neutral(0.80f),
        scrim = Color.Black,
        surfaceBright = seed.neutral(1.0f),
        surfaceContainerLowest = seed.neutral(1.0f),
        surfaceContainerLow = seed.neutral(0.96f),
        surfaceContainer = seed.neutral(0.94f),
        surfaceContainerHigh = seed.neutral(0.90f),
        surfaceContainerHighest = seed.neutral(0.87f),
        surfaceDim = seed.neutral(0.88f),
    ).withAccents(accent, Color.Black)
}

private fun darkScheme(seed: ThemeSeed): ColorScheme {
    val accent = seed.dark
    val surface = seed.neutral(0.07f)
    val onSurface = seed.neutral(0.92f)

    return darkColorScheme(
        primary = accent.primary,
        onPrimary = accent.onPrimary,
        primaryContainer = accent.primaryContainer,
        onPrimaryContainer = accent.onPrimaryContainer,
        inversePrimary = seed.light.primary,
        background = surface,
        onBackground = onSurface,
        surface = surface,
        onSurface = onSurface,
        surfaceVariant = seed.neutral(0.16f),
        onSurfaceVariant = seed.neutral(0.78f),
        surfaceTint = accent.primary,
        inverseSurface = seed.neutral(0.92f),
        inverseOnSurface = seed.neutral(0.10f),
        outline = seed.neutral(0.55f),
        outlineVariant = seed.neutral(0.28f),
        scrim = Color.Black,
        surfaceBright = seed.neutral(0.24f),
        surfaceContainerLowest = seed.neutral(0.03f),
        surfaceContainerLow = seed.neutral(0.07f),
        surfaceContainer = seed.neutral(0.12f),
        surfaceContainerHigh = seed.neutral(0.16f),
        surfaceContainerHighest = seed.neutral(0.20f),
        surfaceDim = seed.neutral(0.03f),
    ).withAccents(accent, Color.White)
}

private fun schemeFor(value: Int, dark: Boolean): ColorScheme {
    val seed = seedFor(value)
    return if (dark) darkScheme(seed) else lightScheme(seed)
}

/** Keeps page and container surfaces black while leaving cards readable. */
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
    val prefs =
        remember(context) {
            context.getSharedPreferences(AppPreferences.PREF_NAME, Context.MODE_PRIVATE)
        }

    var themeMode by remember { mutableIntStateOf(prefs.getInt(KEY_THEME_MODE, 0)) }
    var themeColor by remember { mutableIntStateOf(prefs.getInt(KEY_THEME_COLOR, 0)) }
    var amoledEnabled by remember { mutableStateOf(prefs.getBoolean(KEY_AMOLED, false)) }

    DisposableEffect(prefs) {
        val listener =
            android.content.SharedPreferences.OnSharedPreferenceChangeListener { _, _ ->
                themeMode = prefs.getInt(KEY_THEME_MODE, 0)
                themeColor = prefs.getInt(KEY_THEME_COLOR, 0)
                amoledEnabled = prefs.getBoolean(KEY_AMOLED, false)
            }
        prefs.registerOnSharedPreferenceChangeListener(listener)
        onDispose { prefs.unregisterOnSharedPreferenceChangeListener(listener) }
    }

    val isDark =
        when (themeMode) {
            1 -> false
            2 -> true
            else -> isSystemInDarkTheme()
        }

    val baseScheme =
        if (themeColor == 0) {
            if (isDark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        } else {
            schemeFor(themeColor, isDark)
        }
    val colorScheme = if (amoledEnabled && isDark) baseScheme.amoledBlack() else baseScheme

    EdgeToEdgeSystemBars(darkMode = isDark)

    MaterialTheme(
        colorScheme = colorScheme,
        motionScheme = MotionScheme.expressive(),
        typography = Typography,
        content = content,
    )
}

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
                    darkMode -> SystemBarStyle.dark(navigationBarScrim.toArgb())
                    else ->
                        SystemBarStyle.light(
                            navigationBarScrim.toArgb(),
                            navigationBarScrim.toArgb(),
                        )
                },
        )
    }
}

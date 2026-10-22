import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum AppThemeMode { system, light, dark }

enum AppThemeAccent {
  materialYou,
  catppuccinBlue,
  catppuccinLavender,
  catppuccinGreen,
}

enum AppFontWeight {
  light(300),
  regular(400),
  medium(500),
  bold(700);

  const AppFontWeight(this.value);

  final int value;
}

enum NavBarStyle { standard, floating, glass }

class SettingsController extends ChangeNotifier {
  SettingsController._();

  static final SettingsController instance = SettingsController._();
  static const _channel = MethodChannel('nekosu/settings');

  static const _themeModeKey = 'theme_mode';
  static const _themeAccentKey = 'theme_color';
  static const _fontWeightKey = 'font_weight';
  static const _amoledKey = 'amoled_mode';
  static const _navBarStyleKey = 'nav_bar_style';
  static const _floatingNavBarKey = 'floating_navbar';
  static const _languageKey = 'language';
  static const _displayModeKey = 'display_mode';
  static const _settingsMigratedKey = 'settings_migrated';

  static const managerVersion = '1.0.0';
  static const fallbackSeed = Colors.teal;

  SharedPreferences? _preferences;

  AppThemeMode themeMode = AppThemeMode.system;
  AppThemeAccent themeAccent = AppThemeAccent.materialYou;
  AppFontWeight fontWeight = AppFontWeight.regular;
  bool amoled = false;
  NavBarStyle navBarStyle = NavBarStyle.floating;
  String language = 'system';
  String? displayMode;

  ThemeMode get flutterThemeMode => switch (themeMode) {
    AppThemeMode.system => ThemeMode.system,
    AppThemeMode.light => ThemeMode.light,
    AppThemeMode.dark => ThemeMode.dark,
  };

  Locale? get locale => language == 'system' ? null : Locale(language);

  bool get usesDynamicColor => themeAccent == AppThemeAccent.materialYou;

  Color get seedColor => switch (themeAccent) {
    AppThemeAccent.materialYou => fallbackSeed,
    AppThemeAccent.catppuccinBlue => const Color(0xFF89B4FA),
    AppThemeAccent.catppuccinLavender => const Color(0xFFB4BEFE),
    AppThemeAccent.catppuccinGreen => const Color(0xFFA6E3A1),
  };

  Future<void> load() async {
    try {
      final preferences = await SharedPreferences.getInstance();
      _preferences = preferences;
      if (preferences.getBool(_settingsMigratedKey) != true) {
        await _migrateLegacySettings(preferences);
        await preferences.setBool(_settingsMigratedKey, true);
      }

      themeMode = _themeModeFromIndex(preferences.getInt(_themeModeKey));
      themeAccent = _accentFromIndex(preferences.getInt(_themeAccentKey));
      fontWeight = _fontWeightFromIndex(preferences.getInt(_fontWeightKey));
      amoled = preferences.getBool(_amoledKey) == true;
      final savedNavBarStyle = preferences.getString(_navBarStyleKey);
      navBarStyle = NavBarStyle.values.firstWhere(
        (style) => style.name == savedNavBarStyle,
        orElse: () => preferences.getBool(_floatingNavBarKey) != false
            ? NavBarStyle.floating
            : NavBarStyle.standard,
      );
      displayMode = preferences.getString(_displayModeKey);
      final savedLanguage = preferences.getString(_languageKey);
      if (savedLanguage == 'en' || savedLanguage == 'zh') {
        language = savedLanguage!;
      }
      notifyListeners();
    } on MissingPluginException {
      // Widget tests and non-Android platforms keep the defaults.
    } on PlatformException {
      // Corrupt or temporarily unavailable preferences keep the defaults.
    }
  }

  /// Imports the preferences written by the old Android settings channel once.
  Future<void> _migrateLegacySettings(SharedPreferences preferences) async {
    try {
      final legacy = await _channel.invokeMapMethod<String, Object?>('load');
      if (legacy == null) return;
      await preferences.setInt(
        _themeModeKey,
        _intValue(legacy['themeMode'], 0),
      );
      await preferences.setInt(
        _themeAccentKey,
        _intValue(legacy['themeColor'], 0),
      );
      await preferences.setInt(
        _fontWeightKey,
        _intValue(legacy['fontWeight'], AppFontWeight.regular.index),
      );
      await preferences.setBool(_amoledKey, legacy['amoled'] == true);
      final legacyNavBarStyle = legacy['navBarStyle']?.toString();
      if (legacyNavBarStyle != null && legacyNavBarStyle.isNotEmpty) {
        await preferences.setString(_navBarStyleKey, legacyNavBarStyle);
      }
      await preferences.setBool(
        _floatingNavBarKey,
        legacy['floatingNavBar'] != false,
      );
      final legacyLanguage = legacy['language']?.toString();
      if (legacyLanguage != null && legacyLanguage.isNotEmpty) {
        await preferences.setString(_languageKey, legacyLanguage);
      }
      final legacyDisplayMode = legacy['displayMode']?.toString();
      if (legacyDisplayMode != null && legacyDisplayMode.isNotEmpty) {
        await preferences.setString(_displayModeKey, legacyDisplayMode);
      }
    } on MissingPluginException {
      // No legacy settings on non-Android platforms.
    } on PlatformException {
      // Leave the new preferences empty; defaults remain in use.
    }
  }

  Future<void> setThemeMode(AppThemeMode value) async {
    if (themeMode == value) return;
    themeMode = value;
    notifyListeners();
    await _save(_themeModeKey, value.index);
  }

  Future<void> setThemeAccent(AppThemeAccent value) async {
    if (themeAccent == value) return;
    themeAccent = value;
    notifyListeners();
    await _save(_themeAccentKey, value.index);
  }

  Future<void> setAmoled(bool value) async {
    if (amoled == value) return;
    amoled = value;
    notifyListeners();
    await _save(_amoledKey, value);
  }

  Future<void> setFontWeight(AppFontWeight value) async {
    if (fontWeight == value) return;
    fontWeight = value;
    notifyListeners();
    await _save(_fontWeightKey, value.index);
  }

  Future<void> setNavBarStyle(NavBarStyle value) async {
    if (navBarStyle == value) return;
    navBarStyle = value;
    notifyListeners();
    await _save(_navBarStyleKey, value.name);
    await _save(_floatingNavBarKey, value != NavBarStyle.standard);
  }

  Future<void> setLanguage(String value) async {
    if (language == value) return;
    language = value;
    notifyListeners();
    await _save(_languageKey, value);
  }

  Future<void> setDisplayMode(String value) async {
    if (displayMode == value) return;
    displayMode = value;
    notifyListeners();
    await _save(_displayModeKey, value);
  }

  Future<void> _save(String key, Object value) async {
    try {
      final preferences = _preferences ??=
          await SharedPreferences.getInstance();
      switch (value) {
        case final bool typed:
          await preferences.setBool(key, typed);
        case final int typed:
          await preferences.setInt(key, typed);
        case final String typed:
          await preferences.setString(key, typed);
        default:
          throw ArgumentError.value(value, 'value', 'Unsupported setting type');
      }
    } on MissingPluginException {
      // Keep the in-memory setting when running outside Android.
    } on PlatformException {
      // Settings remain active for this session if persistence fails.
    }
  }

  static int _intValue(Object? value, int fallback) {
    return value is num ? value.toInt() : fallback;
  }

  static AppThemeMode _themeModeFromIndex(Object? value) {
    final index = value is num ? value.toInt() : 0;
    return index >= 0 && index < AppThemeMode.values.length
        ? AppThemeMode.values[index]
        : AppThemeMode.system;
  }

  static AppThemeAccent _accentFromIndex(Object? value) {
    final index = value is num ? value.toInt() : 0;
    return index >= 0 && index < AppThemeAccent.values.length
        ? AppThemeAccent.values[index]
        : AppThemeAccent.materialYou;
  }

  static AppFontWeight _fontWeightFromIndex(Object? value) {
    final index = value is num ? value.toInt() : AppFontWeight.regular.index;
    return index >= 0 && index < AppFontWeight.values.length
        ? AppFontWeight.values[index]
        : AppFontWeight.regular;
  }
}

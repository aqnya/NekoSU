import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

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

class SettingsController extends ChangeNotifier {
  SettingsController._();

  static final SettingsController instance = SettingsController._();
  static const _channel = MethodChannel('nekosu/settings');

  static const managerVersion = '1.0.0';
  static const fallbackSeed = Colors.teal;

  AppThemeMode themeMode = AppThemeMode.system;
  AppThemeAccent themeAccent = AppThemeAccent.materialYou;
  AppFontWeight fontWeight = AppFontWeight.regular;
  bool amoled = false;
  bool floatingNavBar = true;
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
      final values = await _channel.invokeMapMethod<String, Object?>('load');
      if (values == null) return;
      themeMode = _themeModeFromIndex(values['themeMode']);
      themeAccent = _accentFromIndex(values['themeColor']);
      fontWeight = _fontWeightFromIndex(values['fontWeight']);
      amoled = values['amoled'] == true;
      floatingNavBar = values['floatingNavBar'] != false;
      displayMode = values['displayMode']?.toString();
      final savedLanguage = values['language']?.toString();
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

  Future<void> setThemeMode(AppThemeMode value) async {
    if (themeMode == value) return;
    themeMode = value;
    notifyListeners();
    await _save('theme_mode', value.index);
  }

  Future<void> setThemeAccent(AppThemeAccent value) async {
    if (themeAccent == value) return;
    themeAccent = value;
    notifyListeners();
    await _save('theme_color', value.index);
  }

  Future<void> setAmoled(bool value) async {
    if (amoled == value) return;
    amoled = value;
    notifyListeners();
    await _save('amoled_mode', value);
  }

  Future<void> setFontWeight(AppFontWeight value) async {
    if (fontWeight == value) return;
    fontWeight = value;
    notifyListeners();
    await _save('font_weight', value.index);
  }

  Future<void> setFloatingNavBar(bool value) async {
    if (floatingNavBar == value) return;
    floatingNavBar = value;
    notifyListeners();
    await _save('floating_navbar', value);
  }

  Future<void> setLanguage(String value) async {
    if (language == value) return;
    language = value;
    notifyListeners();
    await _save('language', value);
  }

  Future<void> setDisplayMode(String value) async {
    if (displayMode == value) return;
    displayMode = value;
    notifyListeners();
    await _save('display_mode', value);
  }

  Future<void> _save(String key, Object value) async {
    try {
      await _channel.invokeMethod<void>('save', {'key': key, 'value': value});
    } on MissingPluginException {
      // Keep the in-memory setting when running outside Android.
    } on PlatformException {
      // Settings remain active for this session if persistence fails.
    }
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

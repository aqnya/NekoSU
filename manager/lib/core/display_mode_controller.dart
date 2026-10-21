import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_displaymode/flutter_displaymode.dart';
import 'package:nekosu/core/settings_controller.dart';

abstract final class DisplayModeController {
  static const defaultRefreshRate = 120.0;

  static Future<void> initialize() async {
    if (!Platform.isAndroid) return;
    try {
      final modes = await FlutterDisplayMode.supported;
      final saved = SettingsController.instance.displayMode;
      final selected =
          _findSavedMode(modes, saved) ?? await _defaultMode(modes);
      if (selected == null) return;
      await FlutterDisplayMode.setPreferredMode(selected);
      if (saved != selected.toString()) {
        await SettingsController.instance.setDisplayMode(selected.toString());
      }
    } on MissingPluginException {
      // Widget tests and non-Android platforms keep their current mode.
    } on PlatformException {
      // OEM display services may reject the request; keep the system mode.
    } catch (_) {
      // Startup must not fail because display mode discovery is unavailable.
    }
  }

  static Future<void> apply(DisplayMode mode) async {
    await FlutterDisplayMode.setPreferredMode(mode);
    await SettingsController.instance.setDisplayMode(mode.toString());
  }

  static DisplayMode? _findSavedMode(List<DisplayMode> modes, String? saved) {
    if (saved == null) return null;
    for (final mode in modes) {
      if (mode.toString() == saved) return mode;
    }
    return null;
  }

  static Future<DisplayMode?> _defaultMode(List<DisplayMode> modes) async {
    final active = await FlutterDisplayMode.active;
    final available = modes
        .where((mode) => mode.id != DisplayMode.auto.id)
        .toList();
    if (available.isEmpty) return null;

    final sameResolution = available
        .where(
          (mode) => mode.width == active.width && mode.height == active.height,
        )
        .toList();
    final candidates = sameResolution.isEmpty ? available : sameResolution;
    return candidates.reduce(
      (a, b) =>
          (a.refreshRate - defaultRefreshRate).abs() <=
              (b.refreshRate - defaultRefreshRate).abs()
          ? a
          : b,
    );
  }
}

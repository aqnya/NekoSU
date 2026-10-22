import 'package:flutter/foundation.dart';
import 'package:hive_ce_flutter/hive_flutter.dart';
import 'package:path_provider/path_provider.dart';

/// Persists the manager's settings in a Hive box.
class SettingsStore {
  SettingsStore._();

  static const _boxName = 'settings';
  static Box<dynamic>? _box;

  static Future<void> initialize() async {
    if (_box != null) return;
    final directory = await getApplicationSupportDirectory();
    Hive.init('${directory.path}/hive');
    _box = await _openBox();
  }

  static Future<Box<dynamic>> _openBox() async {
    try {
      return await Hive.openBox<dynamic>(_boxName);
    } catch (error) {
      debugPrint('Settings box is unreadable, recreating it: $error');
      await Hive.deleteBoxFromDisk(_boxName);
      return Hive.openBox<dynamic>(_boxName);
    }
  }

  static T? read<T>(String key) {
    final value = _box?.get(key);
    return value is T ? value : null;
  }

  static Future<void> write(String key, Object value) async {
    final box = _box;
    if (box == null) {
      throw StateError('SettingsStore has not been initialized');
    }
    await box.put(key, value);
    await box.flush();
    // Hive appends every write to the box file. Settings change rarely, so
    // compacting here keeps the file bounded instead of growing forever.
    await box.compact();
  }

  /// Drops keys that no longer belong to the settings model.
  static Future<void> prune(Set<String> keep) async {
    final box = _box;
    if (box == null) return;
    final stale = box.keys.where((key) => !keep.contains(key)).toList();
    if (stale.isEmpty) return;
    await box.deleteAll(stale);
    await box.flush();
    await box.compact();
  }
}

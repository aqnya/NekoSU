import 'package:flutter/foundation.dart';
import 'package:nekosu/core/ncore_ffi.dart';
import 'package:nekosu/core/settings_controller.dart';
import 'package:package_info_plus/package_info_plus.dart';

enum InstallStatus { installed, needUpdate, notInstalled, unknown }

class InstallationState {
  const InstallationState({
    required this.status,
    required this.isGki,
    required this.managerVersion,
    this.moduleVersion,
    this.expectedModuleVersion,
  });

  const InstallationState.unknown()
    : status = InstallStatus.unknown,
      isGki = false,
      managerVersion = SettingsController.managerVersion,
      moduleVersion = null,
      expectedModuleVersion = null;

  final InstallStatus status;
  final bool isGki;
  final String managerVersion;
  final String? moduleVersion;
  final String? expectedModuleVersion;

  bool get isRunning => status == InstallStatus.installed;

  static InstallationState resolve({
    required bool nativeAvailable,
    required bool runtimeAvailable,
    required bool isGki,
    required String managerVersion,
    String? moduleVersion,
    String? expectedModuleVersion,
  }) {
    final InstallStatus status;
    if (!nativeAvailable) {
      status = InstallStatus.unknown;
    } else if (!runtimeAvailable) {
      status = InstallStatus.notInstalled;
    } else if (_versionsMismatch(moduleVersion, expectedModuleVersion)) {
      status = InstallStatus.needUpdate;
    } else {
      status = InstallStatus.installed;
    }
    return InstallationState(
      status: status,
      isGki: isGki,
      managerVersion: managerVersion,
      moduleVersion: moduleVersion,
      expectedModuleVersion: expectedModuleVersion,
    );
  }

  static bool _versionsMismatch(String? moduleVersion, String? expected) {
    if (_unknownVersion(moduleVersion) || _unknownVersion(expected)) {
      return false;
    }
    return moduleVersion != expected;
  }

  static bool _unknownVersion(String? version) {
    final normalized = version?.trim();
    return normalized == null || normalized.isEmpty || normalized == 'unknown';
  }

  @override
  bool operator ==(Object other) {
    return other is InstallationState &&
        other.status == status &&
        other.isGki == isGki &&
        other.managerVersion == managerVersion &&
        other.moduleVersion == moduleVersion &&
        other.expectedModuleVersion == expectedModuleVersion;
  }

  @override
  int get hashCode => Object.hash(
    status,
    isGki,
    managerVersion,
    moduleVersion,
    expectedModuleVersion,
  );
}

class InstallStatusRepository {
  const InstallStatusRepository();

  static final _moduleVersionPattern = RegExp(r'^v?(\d+\.\d+\.\d+)');
  static String? _managerVersion;
  static String? _expectedModuleVersion;
  static Future<void>? _initialization;

  /// Loads metadata that is only available through an async platform call.
  ///
  /// Call this before the first frame so [load] can stay synchronous and the
  /// home page never has to render a temporary checking state.
  static Future<void> initialize() {
    return _initialization ??= _loadMetadata();
  }

  static Future<void> _loadMetadata() async {
    try {
      final info = await PackageInfo.fromPlatform();
      final buildNumber = info.buildNumber.trim();
      _managerVersion = buildNumber.isEmpty
          ? info.version
          : '${info.version} ($buildNumber)';
      _expectedModuleVersion = expectedModuleVersionOf(info.version);
    } catch (error) {
      debugPrint('Unable to read manager package info: $error');
      _managerVersion = SettingsController.managerVersion;
      _expectedModuleVersion = null;
    }
  }

  InstallationState load() {
    final nativeAvailable = NcoreFfi.available;
    final runtimeAvailable = nativeAvailable && NcoreFfi.isInstalled;
    final isGki = nativeAvailable && NcoreFfi.isGki;
    final moduleVersion = runtimeAvailable ? NcoreFfi.moduleVersion : null;

    return InstallationState.resolve(
      nativeAvailable: nativeAvailable,
      runtimeAvailable: runtimeAvailable,
      isGki: isGki,
      managerVersion: _managerVersion ?? SettingsController.managerVersion,
      moduleVersion: moduleVersion,
      expectedModuleVersion: _expectedModuleVersion,
    );
  }

  /// Extracts the NKSU version prefix from the APK version name.
  ///
  /// Gradle builds version names as `<uapi/diff>[v]-<channel>...`, so the
  /// numeric prefix is the kernel module version this manager expects.
  static String? expectedModuleVersionOf(String versionName) {
    return _moduleVersionPattern.firstMatch(versionName.trim())?.group(1);
  }
}

import 'dart:async';

import 'package:flutter/services.dart';
import 'package:nekosu/core/output_events.dart';

enum InstallOperation {
  file,
  direct,
  inactive;

  String get wireName => name;
}

class InstallEnvironment {
  const InstallEnvironment({
    required this.kernelRelease,
    required this.androidVersion,
    required this.koAssets,
    required this.rootAvailable,
    required this.bootpatcherAvailable,
    this.currentSlot,
    this.currentVendorBoot,
    this.inactiveVendorBoot,
  });

  final String kernelRelease;
  final String androidVersion;
  final List<String> koAssets;
  final bool rootAvailable;
  final bool bootpatcherAvailable;
  final String? currentSlot;
  final String? currentVendorBoot;
  final String? inactiveVendorBoot;

  factory InstallEnvironment.fromMap(Map<Object?, Object?> map) {
    final rawAssets = map['koAssets'];
    return InstallEnvironment(
      kernelRelease: _stringValue(map['kernelRelease']),
      androidVersion: _stringValue(map['androidVersion']),
      koAssets: rawAssets is List
          ? rawAssets
                .map((value) => value?.toString() ?? '')
                .where((value) => value.isNotEmpty)
                .toList(growable: false)
          : const [],
      rootAvailable: map['rootAvailable'] == true,
      bootpatcherAvailable: map['bootpatcherAvailable'] == true,
      currentSlot: _nullableStringValue(map['currentSlot']),
      currentVendorBoot: _nullableStringValue(map['currentVendorBoot']),
      inactiveVendorBoot: _nullableStringValue(map['inactiveVendorBoot']),
    );
  }

  String? get currentKmi => InstallBridge.currentKmi(kernelRelease);

  String? get matchedKo => InstallBridge.matchKo(
    candidates: koAssets,
    kmi: currentKmi,
    kernelRelease: kernelRelease,
  );

  String? get matchedKmi =>
      matchedKo == null ? null : InstallBridge.kmiOf(matchedKo!);
}

class StagedBootImage {
  const StagedBootImage({
    required this.name,
    required this.path,
    required this.size,
  });

  final String name;
  final String path;
  final int size;

  factory StagedBootImage.fromMap(Map<Object?, Object?> map) {
    return StagedBootImage(
      name: _stringValue(map['name']),
      path: _stringValue(map['path']),
      size: _intValue(map['size']),
    );
  }
}

class LocalKernelModule {
  const LocalKernelModule({
    required this.name,
    required this.path,
    required this.size,
  });

  final String name;
  final String path;
  final int size;

  factory LocalKernelModule.fromMap(Map<Object?, Object?> map) {
    return LocalKernelModule(
      name: _stringValue(map['name']),
      path: _stringValue(map['path']),
      size: _intValue(map['size']),
    );
  }
}

class InstallRequest {
  const InstallRequest({
    required this.operation,
    this.vendorBootPath,
    this.koName,
    this.koPath,
    this.partition,
  });

  final InstallOperation operation;
  final String? vendorBootPath;
  final String? koName;
  final String? koPath;
  final String? partition;

  Map<String, Object?> toMap() {
    return {
      'operation': operation.wireName,
      'vendorBootPath': vendorBootPath,
      'koName': koName,
      'koPath': koPath,
      'partition': partition,
    };
  }
}

class InstallBridge {
  InstallBridge._();

  static const _platform = MethodChannel('nekosu/install_platform');
  static const _outputChannel = EventChannel('nekosu/install_platform/output');
  static final _androidTag = RegExp(r'-android(\d+)');

  static Future<InstallEnvironment> environment() async {
    final raw = await _platform.invokeMethod<Object?>('getEnvironment');
    if (raw is! Map) {
      throw const FormatException('Invalid install environment response');
    }
    return InstallEnvironment.fromMap(raw.cast<Object?, Object?>());
  }

  static Future<StagedBootImage?> pickVendorBoot() async {
    final raw = await _platform.invokeMethod<Object?>('pickVendorBoot');
    if (raw is! Map) return null;
    final image = StagedBootImage.fromMap(raw.cast<Object?, Object?>());
    return image.path.isEmpty ? null : image;
  }

  static Future<LocalKernelModule?> pickLocalKo() async {
    final raw = await _platform.invokeMethod<Object?>('pickLocalKo');
    if (raw is! Map) return null;
    final module = LocalKernelModule.fromMap(raw.cast<Object?, Object?>());
    return module.path.isEmpty ? null : module;
  }

  /// Extracts a module bundled in the APK and returns its staged path.
  static Future<String?> stageKo(String koName) async {
    final path = await _platform.invokeMethod<String>('stageKo', {
      'koName': koName,
    });
    return path == null || path.isEmpty ? null : path;
  }

  static Stream<OutputEvent> output(InstallRequest request) {
    final raw = _outputChannel.receiveBroadcastStream(request.toMap());
    return decodeOutputEvents(raw);
  }

  static String? kmiOf(String koName) {
    if (!koName.endsWith('_nksu.ko')) return null;
    return koName.substring(0, koName.length - '_nksu.ko'.length);
  }

  static String? currentKmi(String kernelRelease) {
    final android = _androidTag.firstMatch(kernelRelease)?.group(1);
    final version = _normalizedKernelVersion(kernelRelease);
    if (android == null || version == null) return null;
    return 'android$android-$version';
  }

  static String? matchKo({
    required List<String> candidates,
    String? kmi,
    String? kernelRelease,
  }) {
    if (candidates.isEmpty) return null;
    if (kmi != null) {
      for (final candidate in candidates) {
        if (kmiOf(candidate) == kmi) return candidate;
      }
    }
    final version = _normalizedKernelVersion(kernelRelease);
    if (version != null) {
      for (final candidate in candidates) {
        final candidateKmi = kmiOf(candidate);
        if (candidateKmi != null && candidateKmi.endsWith('-$version')) {
          return candidate;
        }
      }
    }
    return candidates.first;
  }

  static String? _normalizedKernelVersion(String? release) {
    if (release == null) return null;
    final parts = release.trim().split('.');
    if (parts.length < 2) return null;
    final major = int.tryParse(parts[0]);
    final minor = int.tryParse(parts[1]);
    if (major == null || minor == null) return null;
    return '$major.$minor';
  }
}

String _stringValue(Object? value) => value?.toString() ?? '';

String? _nullableStringValue(Object? value) {
  final text = value?.toString().trim() ?? '';
  return text.isEmpty ? null : text;
}

int _intValue(Object? value) {
  if (value is num) return value.toInt();
  return int.tryParse(value?.toString() ?? '') ?? 0;
}

import 'dart:isolate';

import 'package:nekosu/core/ncore_ffi.dart';

class KernelFeatureInfo {
  const KernelFeatureInfo({
    required this.id,
    required this.name,
    required this.enabled,
  });

  final int id;
  final String name;
  final bool enabled;
}

class KernelFeatureBridge {
  KernelFeatureBridge._();

  static const int selinuxHide = 1;
  static const int umount = 2;

  static final RegExp _whitespace = RegExp(r'\s+');

  static Future<List<KernelFeatureInfo>?> list() async {
    if (!NcoreFfi.featureApiAvailable) return null;
    try {
      final output = await Isolate.run(() => NcoreFfi.featureList());
      if (output == null) return null;
      return parseKernelFeatureList(output);
    } catch (_) {
      return null;
    }
  }

  static Future<bool> setEnabled(int id, bool enabled) async {
    if (!NcoreFfi.featureApiAvailable) return false;
    try {
      return await Isolate.run(() => NcoreFfi.setFeature(id, enabled));
    } catch (_) {
      return false;
    }
  }

  static List<KernelFeatureInfo> parseKernelFeatureList(String output) {
    final features = <KernelFeatureInfo>[];
    for (final line in output.split('\n')) {
      final parts = line.trim().split(_whitespace);
      if (parts.length < 3) continue;
      final id = int.tryParse(parts[0]);
      final value = int.tryParse(parts[2]);
      if (id == null || id < 0 || id > 0xffffffff || value == null) continue;
      features.add(
        KernelFeatureInfo(id: id, name: parts[1], enabled: value != 0),
      );
    }
    return features;
  }
}

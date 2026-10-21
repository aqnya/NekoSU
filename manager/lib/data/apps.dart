import 'dart:typed_data';

import 'package:flutter/services.dart';

class AppInfo {
  const AppInfo({
    required this.name,
    required this.packageName,
    required this.uid,
    required this.isSystem,
    required this.isLaunchable,
    this.versionName = '',
    this.versionCode = 0,
  });

  final String name;
  final String packageName;
  final int uid;
  final bool isSystem;
  final bool isLaunchable;
  final String versionName;
  final int versionCode;

  factory AppInfo.fromMap(Map<Object?, Object?> map) {
    return AppInfo(
      name: map['name'] as String? ?? '',
      packageName: map['packageName'] as String? ?? '',
      uid: map['uid'] as int? ?? -1,
      isSystem: map['isSystem'] as bool? ?? false,
      isLaunchable: map['isLaunchable'] as bool? ?? false,
      versionName: map['versionName'] as String? ?? '',
      versionCode: map['versionCode'] as int? ?? 0,
    );
  }
}

enum AppFilter { all, launchable, system, user }

enum NksuNamespace {
  inherited(1),
  individual(2),
  global(3);

  const NksuNamespace(this.value);

  final int value;

  static NksuNamespace fromValue(int value) {
    return NksuNamespace.values.firstWhere(
      (namespace) => namespace.value == value,
      orElse: () => NksuNamespace.inherited,
    );
  }
}

class LinuxCap {
  const LinuxCap(this.value, this.name);

  final int value;
  final String name;
}

const linuxCaps = <LinuxCap>[
  LinuxCap(0, 'CHOWN'),
  LinuxCap(1, 'DAC_OVERRIDE'),
  LinuxCap(2, 'DAC_READ_SEARCH'),
  LinuxCap(3, 'FOWNER'),
  LinuxCap(4, 'FSETID'),
  LinuxCap(5, 'KILL'),
  LinuxCap(6, 'SETGID'),
  LinuxCap(7, 'SETUID'),
  LinuxCap(8, 'SETPCAP'),
  LinuxCap(9, 'LINUX_IMMUTABLE'),
  LinuxCap(10, 'NET_BIND_SERVICE'),
  LinuxCap(11, 'NET_BROADCAST'),
  LinuxCap(12, 'NET_ADMIN'),
  LinuxCap(13, 'NET_RAW'),
  LinuxCap(14, 'IPC_LOCK'),
  LinuxCap(15, 'IPC_OWNER'),
  LinuxCap(16, 'SYS_MODULE'),
  LinuxCap(17, 'SYS_RAWIO'),
  LinuxCap(18, 'SYS_CHROOT'),
  LinuxCap(19, 'SYS_PTRACE'),
  LinuxCap(20, 'SYS_PACCT'),
  LinuxCap(21, 'SYS_ADMIN'),
  LinuxCap(22, 'SYS_BOOT'),
  LinuxCap(23, 'SYS_NICE'),
  LinuxCap(24, 'SYS_RESOURCE'),
  LinuxCap(25, 'SYS_TIME'),
  LinuxCap(26, 'SYS_TTY_CONFIG'),
  LinuxCap(27, 'MKNOD'),
  LinuxCap(28, 'LEASE'),
  LinuxCap(29, 'AUDIT_WRITE'),
  LinuxCap(30, 'AUDIT_CONTROL'),
  LinuxCap(31, 'SETFCAP'),
  LinuxCap(32, 'MAC_OVERRIDE'),
  LinuxCap(33, 'MAC_ADMIN'),
  LinuxCap(34, 'SYSLOG'),
  LinuxCap(35, 'WAKE_ALARM'),
  LinuxCap(36, 'BLOCK_SUSPEND'),
  LinuxCap(37, 'AUDIT_READ'),
  LinuxCap(38, 'PERFMON'),
  LinuxCap(39, 'BPF'),
  LinuxCap(40, 'CHECKPOINT_RESTORE'),
];

const defaultCaps = <int>{0, 1, 2, 3, 6, 7, 21};
const defaultSelinuxDomain = 'u:r:nksu:s0';

class AppConfig {
  const AppConfig({
    required this.allowed,
    this.rootUid = 0,
    this.caps = defaultCaps,
    this.selinuxDomain = defaultSelinuxDomain,
    this.namespace = NksuNamespace.inherited,
  });

  final bool allowed;
  final int rootUid;
  final Set<int> caps;
  final String selinuxDomain;
  final NksuNamespace namespace;

  int get capsBits => caps.fold(0, (bits, cap) => bits | (1 << cap));
}

Map<int, AppConfig> parseProfiles(String text) {
  final profiles = <int, AppConfig>{};
  final whitespace = RegExp(r'\s+');

  for (final rawLine in text.split('\n')) {
    final line = rawLine.trim();
    if (line.isEmpty || line.startsWith('#')) continue;

    final parts = line.split(whitespace);
    if (parts.length < 3) continue;

    final uid = int.tryParse(parts[0]);
    if (uid == null) continue;

    final capsBits = int.tryParse(parts[1], radix: 16) ?? 0;
    final namespace = NksuNamespace.fromValue(
      int.tryParse(parts[2]) ?? NksuNamespace.inherited.value,
    );
    final rootUid = parts.length >= 5 ? int.tryParse(parts[3]) ?? 0 : 0;
    final domainRaw = parts.length >= 5
        ? parts[4]
        : parts.length == 4
        ? parts[3]
        : null;
    final domain = domainRaw == null || domainRaw.isEmpty || domainRaw == '-'
        ? defaultSelinuxDomain
        : domainRaw;

    profiles[uid] = AppConfig(
      allowed: true,
      rootUid: rootUid,
      caps: {
        for (final cap in linuxCaps)
          if ((capsBits >> cap.value) & 1 == 1) cap.value,
      },
      selinuxDomain: domain,
      namespace: namespace,
    );
  }
  return profiles;
}

class AppsPlatform {
  AppsPlatform._();

  static const _channel = MethodChannel('nekosu/apps');

  static Future<List<AppInfo>> listApps() async {
    final result = await _channel.invokeListMethod<Object?>('listApps');
    if (result == null) return const [];
    return [
      for (final item in result)
        if (item is Map) AppInfo.fromMap(item.cast<Object?, Object?>()),
    ];
  }

  static Future<Uint8List?> getIcon(String packageName) async {
    try {
      return await _channel.invokeMethod<Uint8List>('getIcon', {
        'packageName': packageName,
      });
    } catch (_) {
      return null;
    }
  }
}

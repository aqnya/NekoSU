import 'package:flutter_test/flutter_test.dart';
import 'package:nekosu/data/install_status.dart';

void main() {
  group('InstallationState.resolve', () {
    test('reports unknown when the native library is unavailable', () {
      final state = InstallationState.resolve(
        nativeAvailable: false,
        runtimeAvailable: false,
        isGki: false,
        managerVersion: '1.0.6',
      );

      expect(state.status, InstallStatus.unknown);
    });

    test('reports not installed when the control fd is unavailable', () {
      final state = InstallationState.resolve(
        nativeAvailable: true,
        runtimeAvailable: false,
        isGki: true,
        managerVersion: '1.0.6',
      );

      expect(state.status, InstallStatus.notInstalled);
    });

    test('reports an update when the module version differs', () {
      final state = InstallationState.resolve(
        nativeAvailable: true,
        runtimeAvailable: true,
        isGki: true,
        managerVersion: '1.0.6',
        moduleVersion: '1.0.5',
        expectedModuleVersion: '1.0.6',
      );

      expect(state.status, InstallStatus.needUpdate);
    });

    test('reports installed when the module version matches', () {
      final state = InstallationState.resolve(
        nativeAvailable: true,
        runtimeAvailable: true,
        isGki: false,
        managerVersion: '1.0.6',
        moduleVersion: '1.0.6',
        expectedModuleVersion: '1.0.6',
      );

      expect(state.status, InstallStatus.installed);
    });

    test('does not guess an update when either version is unknown', () {
      final state = InstallationState.resolve(
        nativeAvailable: true,
        runtimeAvailable: true,
        isGki: false,
        managerVersion: '1.0.6',
        moduleVersion: 'unknown',
        expectedModuleVersion: '1.0.6',
      );

      expect(state.status, InstallStatus.installed);
    });
  });

  group('expectedModuleVersionOf', () {
    test('extracts the shared UAPI version from channel suffixes', () {
      expect(
        InstallStatusRepository.expectedModuleVersionOf(
          '1.0.6-canary.42+abcdef',
        ),
        '1.0.6',
      );
      expect(
        InstallStatusRepository.expectedModuleVersionOf('v1.0.6-beta.2'),
        '1.0.6',
      );
    });

    test('returns null for an unrecognized version name', () {
      expect(
        InstallStatusRepository.expectedModuleVersionOf('custom-build'),
        isNull,
      );
    });
  });
}

import 'dart:convert';
import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

typedef _IntNative = Int32 Function();
typedef _IntDart = int Function();
typedef _CtlNative = Int32 Function(Int32 value);
typedef _CtlDart = int Function(int value);
typedef _StringNative = IntPtr Function(Pointer<Uint8> buffer, Size capacity);
typedef _StringDart = int Function(Pointer<Uint8> buffer, int capacity);
typedef _ProfileNative = Int32 Function(
  Int32 uid,
  Int32 rootUid,
  Uint64 caps,
  Pointer<Uint8> domain,
  Size domainLength,
  Int32 namespace,
);
typedef _ProfileDart = int Function(
  int uid,
  int rootUid,
  int caps,
  Pointer<Uint8> domain,
  int domainLength,
  int namespace,
);
typedef _UidNative = Int32 Function(Int32 uid);
typedef _UidDart = int Function(int uid);
typedef _FeatureGetNative = Int64 Function(Uint32 id);
typedef _FeatureGetDart = int Function(int id);
typedef _FeatureSetNative = Int32 Function(Uint32 id, Uint64 value);
typedef _FeatureSetDart = int Function(int id, int value);
typedef _StringArgNative = IntPtr Function(
  Pointer<Uint8> value,
  Size valueLength,
  Pointer<Uint8> buffer,
  Size capacity,
);
typedef _StringArgDart = int Function(
  Pointer<Uint8> value,
  int valueLength,
  Pointer<Uint8> buffer,
  int capacity,
);
typedef _ModuleControlNative = IntPtr Function(
  Pointer<Uint8> id,
  Size idLength,
  Int32 operation,
  Pointer<Uint8> buffer,
  Size capacity,
);
typedef _ModuleControlDart = int Function(
  Pointer<Uint8> id,
  int idLength,
  int operation,
  Pointer<Uint8> buffer,
  int capacity,
);

class NcoreFfi {
  NcoreFfi._();

  static final DynamicLibrary? _library = _openLibrary();
  static final _IntDart? _init = _lookupInt('ncore_init');
  static final _CtlDart? _ctl = _lookupCtl('ncore_ctl');
  static final _IntDart? _isGki = _lookupInt('ncore_is_gki');
  static final _StringDart? _kernelVersion = _lookupString(
    'ncore_kernel_version',
  );
  static final _StringDart? _kernelRelease = _lookupString(
    'ncore_kernel_release',
  );
  static final _StringDart? _moduleVersion = _lookupString(
    'ncore_module_version',
  );
  static final _StringDart? _featureList = _lookupString('ncore_feature_list');
  static final _FeatureGetDart? _featureGet = _lookupFeatureGet(
    'ncore_feature_get',
  );
  static final _FeatureSetDart? _featureSet = _lookupFeatureSet(
    'ncore_feature_set',
  );
  static final _ProfileDart? _setProfile = _lookupProfile('ncore_set_profile');
  static final _UidDart? _deleteProfile = _lookupUid('ncore_del_uid');
  static final _UidDart? _hasProfile = _lookupUid('ncore_has_uid');
  static final _StringDart? _listProfiles = _lookupString(
    'ncore_list_profiles',
  );
  static final _StringDart? _moduleList = _lookupString('ncore_module_list');
  static final _StringArgDart? _moduleInstall = _lookupStringArg(
    'ncore_module_install',
  );
  static final _StringArgDart? _moduleAction = _lookupStringArg(
    'ncore_module_action',
  );
  static final _ModuleControlDart? _moduleControl = _lookupModuleControl(
    'ncore_module_control',
  );
  static final _StringArgDart? _moduleWebUi = _lookupStringArg(
    'ncore_module_webui',
  );
  static final _StringArgDart? _webUiExec = _lookupStringArg(
    'ncore_webui_exec',
  );

  static final bool available = _ctl != null && _isGki != null;
  static final bool profileApiAvailable =
      _setProfile != null &&
      _deleteProfile != null &&
      _hasProfile != null &&
      _listProfiles != null;
  static final bool featureApiAvailable =
      _featureList != null && _featureGet != null && _featureSet != null;
  static final bool moduleApiAvailable =
      _moduleList != null &&
      _moduleInstall != null &&
      _moduleAction != null &&
      _moduleControl != null &&
      _moduleWebUi != null;

  static bool _initialized = false;

  static DynamicLibrary? _openLibrary() {
    if (!Platform.isAndroid) return null;
    try {
      return DynamicLibrary.open('libncore_ffi.so');
    } catch (_) {
      return null;
    }
  }

  static _IntDart? _lookupInt(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_IntNative, _IntDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _CtlDart? _lookupCtl(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_CtlNative, _CtlDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _StringDart? _lookupString(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_StringNative, _StringDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _ProfileDart? _lookupProfile(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_ProfileNative, _ProfileDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _UidDart? _lookupUid(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_UidNative, _UidDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _FeatureGetDart? _lookupFeatureGet(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_FeatureGetNative, _FeatureGetDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _FeatureSetDart? _lookupFeatureSet(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_FeatureSetNative, _FeatureSetDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _StringArgDart? _lookupStringArg(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_StringArgNative, _StringArgDart>(name);
    } catch (_) {
      return null;
    }
  }

  static _ModuleControlDart? _lookupModuleControl(String name) {
    final library = _library;
    if (library == null) return null;
    try {
      return library.lookupFunction<_ModuleControlNative, _ModuleControlDart>(
        name,
      );
    } catch (_) {
      return null;
    }
  }

  static void _ensureInitialized() {
    if (_initialized) return;
    _initialized = true;
    _init?.call();
  }

  static int ctl(int value) {
    _ensureInitialized();
    return _ctl?.call(value) ?? -1;
  }

  static bool get isInstalled => ctl(1) == 0;

  static bool get isGki {
    _ensureInitialized();
    return _isGki?.call() == 1;
  }

  static String? get kernelVersion => _readString(_kernelVersion);

  static String? get kernelRelease => _readString(_kernelRelease);

  static String? get moduleVersion => _readString(_moduleVersion);

  static String? get profiles => _readString(_listProfiles);

  static String? featureList() => _readString(_featureList);

  static int? featureGet(int id) {
    final function = _featureGet;
    if (function == null || id < 0 || id > 0xffffffff) return null;
    _ensureInitialized();
    final value = function(id);
    return value < 0 ? null : value;
  }

  static bool setFeature(int id, bool enabled) {
    final function = _featureSet;
    if (function == null || id < 0 || id > 0xffffffff) return false;
    _ensureInitialized();
    return function(id, enabled ? 1 : 0) == 0;
  }

  static String? moduleList() => _readModuleString(_moduleList);

  static String? moduleInstall(String zipPath) =>
      _readModuleStringArg(_moduleInstall, zipPath, retry: false);

  static String? moduleAction(String id) =>
      _readModuleStringArg(_moduleAction, id, retry: false);

  static String? moduleControl(String id, int operation) {
    final function = _moduleControl;
    if (function == null) return null;
    final bytes = utf8.encode(id);
    final value = calloc<Uint8>(bytes.isEmpty ? 1 : bytes.length);
    try {
      if (bytes.isNotEmpty) value.asTypedList(bytes.length).setAll(0, bytes);
      return _readModuleString(
        (buffer, capacity) =>
            function(value, bytes.length, operation, buffer, capacity),
        retry: false,
      );
    } finally {
      calloc.free(value);
    }
  }

  static String? moduleWebUi(String id) =>
      _readModuleStringArg(_moduleWebUi, id, retry: false);

  static String? webUiExec(String command) =>
      _readModuleStringArg(_webUiExec, command, retry: false);

  static bool setProfile({
    required int uid,
    required int rootUid,
    required int caps,
    required String domain,
    required int namespace,
  }) {
    final function = _setProfile;
    if (function == null) return false;
    _ensureInitialized();

    final bytes = utf8.encode(domain);
    final buffer = calloc<Uint8>(bytes.isEmpty ? 1 : bytes.length);
    try {
      if (bytes.isNotEmpty) {
        buffer.asTypedList(bytes.length).setAll(0, bytes);
      }
      return function(uid, rootUid, caps, buffer, bytes.length, namespace) == 0;
    } finally {
      calloc.free(buffer);
    }
  }

  static bool deleteProfile(int uid) {
    final function = _deleteProfile;
    if (function == null) return false;
    _ensureInitialized();
    return function(uid) == 0;
  }

  static bool? hasProfile(int uid) {
    final function = _hasProfile;
    if (function == null) return null;
    _ensureInitialized();
    final result = function(uid);
    return result < 0 ? null : result == 1;
  }

  static String? _readString(_StringDart? function) {
    if (function == null) return null;
    _ensureInitialized();

    var capacity = 128;
    for (var attempt = 0; attempt < 3; attempt++) {
      final buffer = calloc<Uint8>(capacity);
      try {
        final length = function(buffer, capacity);
        if (length < 0) return null;
        if (length < capacity) {
          return buffer.cast<Utf8>().toDartString(length: length);
        }
        if (length > 64 * 1024) return null;
        capacity = length + 1;
      } finally {
        calloc.free(buffer);
      }
    }
    return null;
  }

  static String? _readModuleStringArg(
    _StringArgDart? function,
    String value, {
    bool retry = true,
  }) {
    if (function == null) return null;
    final bytes = utf8.encode(value);
    final argument = calloc<Uint8>(bytes.isEmpty ? 1 : bytes.length);
    try {
      if (bytes.isNotEmpty) {
        argument.asTypedList(bytes.length).setAll(0, bytes);
      }
      return _readModuleString(
        (buffer, capacity) =>
            function(argument, bytes.length, buffer, capacity),
        retry: retry,
      );
    } finally {
      calloc.free(argument);
    }
  }

  static String? _readModuleString(_StringDart? function, {bool retry = true}) {
    if (function == null) return null;
    if (!retry) {
      return _readModuleStringOnce(function);
    }
    var capacity = 4096;
    for (var attempt = 0; attempt < 4; attempt++) {
      final buffer = calloc<Uint8>(capacity);
      try {
        final length = function(buffer, capacity);
        if (length < 0) return null;
        if (length < capacity) {
          return buffer.cast<Utf8>().toDartString(length: length);
        }
        if (length > 4 * 1024 * 1024) return null;
        capacity = length + 1;
      } finally {
        calloc.free(buffer);
      }
    }
    return null;
  }

  /// Runs [call] exactly once. Module install/action/control commands have
  /// side effects, so growing the output buffer must not invoke them again.
  static String? _readModuleStringOnce(int Function(Pointer<Uint8>, int) call) {
    const capacity = 4 * 1024 * 1024;
    final buffer = calloc<Uint8>(capacity);
    try {
      final length = call(buffer, capacity);
      if (length < 0) return null;
      if (length >= capacity) {
        return '-1\nModule output exceeded the 4 MiB limit';
      }
      return buffer.cast<Utf8>().toDartString(length: length);
    } finally {
      calloc.free(buffer);
    }
  }
}

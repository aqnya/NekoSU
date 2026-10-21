import 'package:flutter/services.dart';

class LogExport {
  LogExport._();

  static const _channel = MethodChannel('nekosu/settings');

  static Future<String?> export({required String title}) {
    return _channel.invokeMethod<String>('exportLogs', {'title': title});
  }
}

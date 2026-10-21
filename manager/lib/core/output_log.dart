import 'package:flutter/services.dart';

class OutputLog {
  OutputLog._();

  static const _channel = MethodChannel('nekosu/settings');

  static Future<String?> save({
    required String fileName,
    required String content,
  }) {
    return _channel.invokeMethod<String>('saveOutputLog', {
      'fileName': fileName,
      'content': content,
    });
  }
}

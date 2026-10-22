import 'dart:async';

sealed class OutputEvent {
  const OutputEvent();
}

final class OutputChunk extends OutputEvent {
  const OutputChunk(this.text, {this.isError = false});

  final String text;
  final bool isError;
}

final class OutputFinished extends OutputEvent {
  const OutputFinished(this.exitCode);

  final int exitCode;

  bool get succeeded => exitCode == 0;
}

Stream<OutputEvent> decodeOutputEvents(Stream<dynamic> events) async* {
  await for (final raw in events) {
    final event = decodeOutputEvent(raw);
    if (event == null) continue;
    yield event;
    if (event is OutputFinished) return;
  }
}

OutputEvent? decodeOutputEvent(Object? raw) {
  if (raw is! Map) return null;
  switch (raw['type']) {
    case 'line':
      final text = raw['text']?.toString();
      if (text == null || text.isEmpty) return null;
      return OutputChunk(text);
    case 'done':
      final code = int.tryParse(raw['code']?.toString() ?? '');
      return OutputFinished(code ?? -1);
    default:
      return null;
  }
}

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

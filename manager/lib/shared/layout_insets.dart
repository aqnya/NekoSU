import 'package:flutter/widgets.dart';

const floatingNavBarHeight = 64.0;
const floatingNavBarVerticalPadding = 16.0;
const floatingNavBarContentGap = 32.0;

class AppNavBarScope extends InheritedWidget {
  const AppNavBarScope({
    super.key,
    required this.floating,
    required super.child,
  });

  final bool floating;

  @override
  bool updateShouldNotify(AppNavBarScope oldWidget) {
    return floating != oldWidget.floating;
  }
}

double appNavBarClearance(BuildContext context) {
  final floating =
      context.dependOnInheritedWidgetOfExactType<AppNavBarScope>()?.floating ??
      true;
  if (!floating) return 16;
  return MediaQuery.viewPaddingOf(context).bottom +
      floatingNavBarHeight +
      floatingNavBarVerticalPadding * 2 +
      floatingNavBarContentGap;
}

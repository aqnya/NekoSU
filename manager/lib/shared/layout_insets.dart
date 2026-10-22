import 'package:flutter/widgets.dart';
import 'package:nekosu/core/settings_controller.dart';

const capsuleNavBarHeight = 68.0;
const capsuleNavBarBottomGap = 16.0;
const glassNavBarHeight = 64.0;
const glassNavBarVerticalPadding = 16.0;
const floatingNavBarContentGap = 32.0;

class AppNavBarScope extends InheritedWidget {
  const AppNavBarScope({super.key, required this.style, required super.child});

  final NavBarStyle style;

  @override
  bool updateShouldNotify(AppNavBarScope oldWidget) {
    return style != oldWidget.style;
  }
}

double appNavBarClearance(BuildContext context) {
  final style =
      context.dependOnInheritedWidgetOfExactType<AppNavBarScope>()?.style ??
      NavBarStyle.floating;
  final bottomInset = MediaQuery.viewPaddingOf(context).bottom;
  return switch (style) {
    NavBarStyle.standard => 16,
    NavBarStyle.floating =>
      bottomInset +
          capsuleNavBarHeight +
          capsuleNavBarBottomGap +
          floatingNavBarContentGap,
    NavBarStyle.glass =>
      bottomInset +
          glassNavBarHeight +
          glassNavBarVerticalPadding * 2 +
          floatingNavBarContentGap,
  };
}

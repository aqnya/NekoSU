import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:liquid_glass_widgets/liquid_glass_widgets.dart';
import 'package:nekosu/shared/layout_insets.dart';

class NavBarTab {
  const NavBarTab({required this.label, required this.icon, this.activeIcon});
  final String label;
  final Widget icon;
  final Widget? activeIcon;
}

abstract class _NavigationBarBase extends StatelessWidget {
  const _NavigationBarBase({
    super.key,
    required this.tabs,
    required this.selectedIndex,
    required this.onTabSelected,
  });

  final List<NavBarTab> tabs;
  final int selectedIndex;
  final ValueChanged<int> onTabSelected;

  void _selectTab(int index) {
    HapticFeedback.lightImpact();
    onTabSelected(index);
  }
}

class GlassFloatingNavBar extends _NavigationBarBase {
  const GlassFloatingNavBar({
    super.key,
    required super.tabs,
    required super.selectedIndex,
    required super.onTabSelected,
  });

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return SafeArea(
      top: false,
      child: GlassTabBar.bottom(
        tabs: [
          for (final tab in tabs)
            GlassTab(
              icon: tab.icon,
              activeIcon: tab.activeIcon ?? tab.icon,
              label: tab.label,
              glowColor: scheme.primary,
            ),
        ],
        selectedIndex: selectedIndex,
        onTabSelected: _selectTab,
        barHeight: floatingNavBarHeight,
        verticalPadding: floatingNavBarVerticalPadding,
        horizontalPadding: 20,
        indicatorColor: scheme.primaryContainer,
        selectedIconColor: scheme.primary,
        unselectedIconColor: scheme.onSurfaceVariant,
        selectedLabelColor: scheme.primary,
        unselectedLabelColor: scheme.onSurfaceVariant,
      ),
    );
  }
}

class ModernNavigationBar extends _NavigationBarBase {
  const ModernNavigationBar({
    super.key,
    required super.tabs,
    required super.selectedIndex,
    required super.onTabSelected,
  });

  @override
  Widget build(BuildContext context) {
    final scheme = Theme.of(context).colorScheme;
    return NavigationBar(
      selectedIndex: selectedIndex,
      onDestinationSelected: _selectTab,
      backgroundColor: scheme.surfaceContainer,
      surfaceTintColor: Colors.transparent,
      indicatorColor: scheme.primaryContainer,
      destinations: [
        for (final tab in tabs)
          NavigationDestination(
            icon: tab.icon,
            selectedIcon: tab.activeIcon ?? tab.icon,
            label: tab.label,
          ),
      ],
    );
  }
}

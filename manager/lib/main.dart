import 'package:flutter/widgets.dart';
import 'package:nekosu/app/app.dart';
import 'package:nekosu/core/display_mode_controller.dart';
import 'package:nekosu/core/settings_controller.dart';

export 'package:nekosu/app/app.dart' show MyApp;

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SettingsController.instance.load();
  await DisplayModeController.initialize();
  runApp(const MyApp());
}

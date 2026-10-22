import 'package:flutter/widgets.dart';
import 'package:nekosu/app/app.dart';
import 'package:nekosu/core/display_mode_controller.dart';
import 'package:nekosu/core/ncore_ffi.dart';
import 'package:nekosu/core/settings_controller.dart';
import 'package:nekosu/data/install_status.dart';

export 'package:nekosu/app/app.dart' show MyApp;

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  NcoreFfi.initialize();
  await InstallStatusRepository.initialize();
  await SettingsController.instance.load();
  await DisplayModeController.initialize();
  runApp(const MyApp());
}

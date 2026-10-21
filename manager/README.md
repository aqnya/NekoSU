# NekoSU Manager

Flutter-based manager for NekoSU. It provides the home dashboard, module and app
management, WebUI hosting, settings, and the libbootpatcher installation flow.

## Build

Install Flutter 3.47.7 with the Android toolchain, then run from this directory:

```sh
flutter pub get
flutter build apk --release --target-platform android-arm64
```

The APK is written to `build/app/outputs/flutter-apk/app-release.apk`. The Android
build also compiles the Rust JNI library in `../neko` and packages the kernel
module from the repository's release pipeline.

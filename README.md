# NekoSU

[![Android CI](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml)
[![Build LKM](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

Kernel-level root solution for Android, shipped as a loadable kernel module (`nksu.ko`) plus a Material 3 manager app.
KernelSU-compatible module system, WebUI and SELinux rule wire format support.

**English** | [简体中文](README_zh.md)

---

## Features

1.Kernel-based su and root access management.

2.A more aggressive technical choice compared to KernelSU.

## Requirements

- Android 12+ (minSdk 31), **arm64-v8a** only.
- Unlocked bootloader (flashing `vendor_boot`).
- Direct / inactive-slot install additionally needs an existing root solution (Magisk/KernelSU) and a GKI device.

## Installation

1. Download the manager APK from the **Actions → Android CI → artifacts (`release`)** page.
2. Open the app → install screen → choose **File**, **Direct install**, or **Inactive slot**.
3. Reboot. The home card shows whether the running module matches the manager's expected version.

> Direct install and inactive-slot install flash `vendor_boot`; the bootloader must be unlocked.

## Building

### Kernel module

```sh
make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
```

CI builds `nksu.ko` for every supported KMI inside DDK containers
(`ghcr.io/ylarod/ddk:<kmi>-…`) and uploads `<kmi>-lkm` artifacts. Supported KMI matrix:

```
android12-5.10  android13-5.10  android13-5.15  android14-5.15
android14-6.1   android15-6.6   android16-6.12
```

The build embeds `NKSU_VERSION` (from the root `version` file) and `NKSU_GIT_COMMIT`.

### Manager app

```sh
# prerequisites: JDK 21+, Android SDK + NDK, and:
#   manager/app/src/main/assets/ko/*_nksu.ko      (from the LKM workflow)
#   manager/app/src/main/jniLibs/arm64-v8a/libncore.so   (from aqnya/ncore CI)

cd manager
./gradlew assembleRelease
```

Release signing reads `KEYSTORE_BASE64`, `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`
from the environment; without them a debug keystore is used.

## Credits

- **[KernelSU](https://github.com/tiann/KernelSU)** — the SELinux policy/rule engine, LSM hook layout,
  `init.rc` injection technique, SELinux hiding and the WebUI `window.ksu` interface are ported from or
  kept compatible with KernelSU, so KernelSU modules and WebUIs work with NekoSU.
- **[ncore](https://github.com/aqnya/ncore)** — the userspace module runtime shipped inside the APK.
- BusyBox, Material 3 / Jetpack Compose and the open-source libraries listed in the app's *About → Open source* screen.

## License

[GPL-3.0](LICENSE) — © 2025 Aqnya

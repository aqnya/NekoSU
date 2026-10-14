# NekoSU

[![Android CI](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml)
[![Build LKM](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

Kernel-level root solution for Android: a kernel module plus a Material 3 manager app.
KernelSU-compatible module system, WebUI and SELinux rule wire format support.

**English** | [简体中文](README_zh.md)

📖 [Documentation](docs/README.md) — architecture, build guide, kernel module reference, user manual.

---

## Features

1.Kernel-based su and root access management.

2.A more aggressive technical choice compared to KernelSU.

## Requirements

- Android 12+ (minSdk 31), **arm64-v8a** only
- Unlocked bootloader

## Installation

See [Installation methods](docs/usage.md#installation-methods).

## Building

See [How to build](docs/building.md).

## Credits

- [KernelSU](https://github.com/tiann/KernelSU) — the SELinux policy/rule engine, LSM hook layout,
  `init.rc` injection technique, SELinux hiding and the WebUI `window.ksu` interface are ported from or
  kept compatible with KernelSU, so KernelSU modules and WebUIs work with NekoSU.
- BusyBox, Material 3 / Jetpack Compose and the open-source libraries listed in the app's *About → Open source* screen.

## License

[GPL-3.0](LICENSE) — © 2025 Aqnya

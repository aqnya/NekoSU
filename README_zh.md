# NekoSU

[![Android CI](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml)
[![Build LKM](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

Android 内核级 Root 方案，由可加载内核模块（`nksu.ko`）和 Material 3 管理器 App 组成。
模块系统、WebUI 与 SELinux 规则格式均兼容 KernelSU。

[English](README.md) | **简体中文**

📖 **[文档](docs/README.zh.md)** —— 架构、构建指南、内核模块参考、用户手册。

---

## 功能特性

1. 内核态实现的root支持

2. 相比较于kernelsu更激进的技术选择

## 环境要求

- Android 12+（minSdk 31），仅支持 **arm64-v8a**
- 解锁Bootloader

## 安装

见 **[使用 § 安装方式](docs/usage.zh.md#installation-methods)**。

## 构建

见 **[如何构建](docs/building.zh.md)**。

## 致谢

- **[KernelSU](https://github.com/tiann/KernelSU)** — SELinux 策略/规则引擎、LSM hook 布局、SELinux 隐藏以及 WebUI `window.ksu` 接口均移植或保持兼容自 KernelSU，
  因此 KernelSU 模块与 WebUI 可在 NekoSU 上直接运行。
- BusyBox、Material 3 / Jetpack Compose，以及应用 *关于 → 开源许可* 页面列出的开源库。

## 许可证

[GPL-3.0](LICENSE) — © 2025 Aqnya

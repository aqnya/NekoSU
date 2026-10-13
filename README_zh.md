# NekoSU

[![Android CI](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/android.yml)
[![Build LKM](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml/badge.svg)](https://github.com/nksu-crew/NekoSU/actions/workflows/build-lkm.yml)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)

Android 内核级 Root 方案，由可加载内核模块（`nksu.ko`）和 Material 3 管理器 App 组成。
模块系统、WebUI 与 SELinux 规则格式均兼容 KernelSU。

[English](README.md) | **简体中文**

---

## 功能特性

1. 内核态实现的root支持

2. 相比较于kernelsu更激进的技术选择

## 环境要求

- Android 12+（minSdk 31），仅支持 **arm64-v8a**。
- Bootloader 已解锁（刷写 `vendor_boot`）。
- 直接安装 / 非活动 slot 安装额外需要已有 Root 方案（Magisk/KernelSU）且为 GKI 设备。

## 安装

1. 从 **Actions → Android CI → artifacts（`release`）** 页面下载管理器 APK。
2. 打开应用 → 安装页 → 选择 **文件安装**、**直接安装** 或 **非活动 slot**。
3. 重启。首页卡片会显示运行中的模块是否与管理器期望的版本一致。

> 直接安装与非活动 slot 安装会刷写 `vendor_boot`，Bootloader 必须已解锁。

## 构建

### 内核模块

```sh
make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
```

CI 在 DDK 容器（`ghcr.io/ylarod/ddk:<kmi>-…`）中为每个受支持的 KMI 构建 `nksu.ko`，
并上传 `<kmi>-lkm` artifact。支持的 KMI 矩阵：

```
android12-5.10  android13-5.10  android13-5.15  android14-5.15
android14-6.1   android15-6.6   android16-6.12
```

构建会嵌入 `NKSU_VERSION`（读取根目录 `version` 文件）与 `NKSU_GIT_COMMIT`。

### 管理器 App

```sh
# 前置条件：JDK 21+、Android SDK + NDK，以及：
#   manager/app/src/main/assets/ko/*_nksu.ko              （来自 LKM 工作流）
#   manager/app/src/main/jniLibs/arm64-v8a/libncore.so
#       由仓库内置的 ncore 源码构建：
#       cd userspace/ncore && bazel build //:ncore --config=android

cd manager
./gradlew assembleRelease
```

Android CI 会用自带的固定版本 Bazel 从 `userspace/ncore` 构建 `ncore`，并打包为 `libncore.so`，
不再需要从外部仓库下载产物。

发布签名从环境变量读取 `KEYSTORE_BASE64`、`KEYSTORE_PASSWORD`、`KEY_ALIAS`、`KEY_PASSWORD`；
未设置时使用 debug keystore。

## 致谢

- **[KernelSU](https://github.com/tiann/KernelSU)** — SELinux 策略/规则引擎、LSM hook 布局、SELinux 隐藏以及 WebUI `window.ksu` 接口均移植或保持兼容自 KernelSU，
  因此 KernelSU 模块与 WebUI 可在 NekoSU 上直接运行。
- **[ncore](https://github.com/aqnya/ncore)** — 打包在 APK 内的用户态模块运行时，源码位于 `userspace/ncore/`。
- BusyBox、Material 3 / Jetpack Compose，以及应用 *关于 → 开源许可* 页面列出的开源库。

## 许可证

[GPL-3.0](LICENSE) — © 2025 Aqnya

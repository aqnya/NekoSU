# 构建与开发指南

[English](building.md) | **简体中文**

本指南涵盖从源码构建三个产物、版本流程，以及 CI 工作流。

---

## 1. 仓库布局

```
NekoSU/
├── kernel/            LKM 源码（用 kbuild 树外构建）
├── userspace/ncore/   用户态运行时（Bazel + Android NDK）
├── manager/           Android 应用（Gradle）
├── docs/              本文档
├── version            1.0.0 —— NKSU_VERSION 的唯一事实来源
└── .github/workflows/ CI
```

---

## 2. 前置条件

| 工具 | 版本 / 说明 |
| --- | --- |
| 内核构建 | 一份 GKI 内核源码树 + `aarch64` Clang 工具链（或使用下方的 DDK 容器） |
| Bazel | **9.3.0**（固定在 `userspace/ncore/.bazelversion`）；推荐用 `bazelisk` |
| Android NDK | **r29**，`ANDROID_NDK_API_LEVEL=28`，需设置 `ANDROID_NDK_HOME` |
| JDK | **21+**（CI 使用 25） |
| Android SDK | compileSdk 37（major 37，minor 1） |
| Gradle | wrapper 固定为 **9.8.1**（`manager/gradle/wrapper/gradle-wrapper.properties`） |

管理器构建还需要预编译的 native 产物（见 [§5](#5-构建管理器-app)）。

---

## 3. 构建内核模块

`kernel/` 采用树外构建。根 `Makefile` 委托给 kbuild：

```sh
make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
```

- `KDIR` —— 内核构建目录（传入你的 GKI 树；CI 使用 DDK 容器）。
- `CONFIG_NKSU=m` —— 以模块方式构建 `nksu.ko`。
- `CONFIG_NKSU_DEBUG=y` —— 详细日志、宽松的 `nksu` 域、计时插桩。设为 `n` 得到生产构建
  （`-O3`），DDK 工作流即如此。

配置项（`kernel/Kconfig`）：

| 选项 | 默认 | 用途 |
| --- | --- | --- |
| `NKSU` | `y` | 构建该模块（tristate）。 |
| `NKSU_DEBUG` | `y` | 调试模式（依赖 `NKSU`）。 |
| `NKSU_SYSCALL` | `n` | 使用系统调用表分发器，替代默认的 tracepoint hook。 |

版本嵌入在 `kernel/Kbuild` 中自动完成：

- `NKSU_VERSION` ← 根目录 `version` 文件内容（回退为 `"unknown"`）。
- `NKSU_GIT_COMMIT` ← `git rev-parse --short=12 HEAD`（回退为 `"unknown"`）。

### KMI 矩阵与 DDK 容器

同一份源码通过 `LINUX_VERSION_CODE` 适配各 KMI。CI 在 DDK 容器
`ghcr.io/ylarod/ddk:<kmi>-<ddk_release>`（默认 release `20260828`）中为每个条目构建：

```
android12-5.10   android13-5.10   android13-5.15   android14-5.15
android14-6.1    android15-6.6    android16-6.12
```

编译出的模块经 `llvm-strip -d` 后上传为 `<kmi>-lkm` artifact（例如 `android15-6.6-lkm`，
内含 `android15-6.6_nksu.ko`）。

---

## 4. 构建 `ncore`

`userspace/ncore` 仅使用 Bzlmod（没有 `WORKSPACE`）。配置好 NDK 后：

```sh
cd userspace/ncore
ANDROID_NDK_HOME=/path/to/android-ndk-r29 ANDROID_NDK_API_LEVEL=28 \
  bazel build //:ncore --config=android
```

输出为 `bazel-bin/ncore` —— 一个 **静态 arm64 可执行文件**，而非共享库。打包时被重命名为
`libncore.so`，但它是被**执行**的，从不 `dlopen`。

主要目标（`BUILD.bazel`）：`//:argparse`、`//:boot`、`//:module`、`//:cli`、`//:su` 以及
`//:ncore` 二进制。`--config=android` 选择 `//:android_arm64` 平台与 `@androidndk` 工具链。

压缩编解码器来自 `third_party/` 下的内置子模块（`bzip2`、`lz4`、`xz`、`zlib`），因此克隆时
需带上子模块：

```sh
git clone --recurse-submodules https://github.com/nksu-crew/NekoSU.git
```

---

## 5. 构建管理器 App

App 需要两个**未**纳入版本控制的产物：

1. `manager/app/src/main/assets/ko/<kmi>_nksu.ko` —— 来自 LKM 工作流的 LKM。
2. `manager/app/src/main/jniLibs/arm64-v8a/libncore.so` —— 来自 [§4](#4-构建-ncore) 的
   `ncore` 二进制。

```sh
# 在仓库根目录，先准备好上述两个产物：
cd manager
./gradlew assembleRelease
```

构建行为：

- `namespace`/`applicationId` = `me.nekosu.aqnya`；`minSdk 31`、`targetSdk 37`、仅 arm64-v8a。
- `versionCode` = git 提交数，`versionName` = 短 git 哈希。
- `BuildConfig.NKSU_VERSION` 读取自 `../version`。
- native 代码从 `app/src/main/cpp` 构建（CMake 目标 `jni` → `libjni.so`）。
- `useLegacyPackaging = true` 保持 `.so` 文件解压到磁盘（必需，因为 `libncore.so` 与
  `libbusybox.so` 是被执行的，而非加载的）。
- `libbusybox.so` 已提交在 `app/src/main/jniLibs/arm64-v8a/`。

### 签名

发布签名读取以下环境变量；没有 keystore 时使用 debug keystore：

- `KEYSTORE_BASE64` —— CI 在构建前将其解码为 `manager/release.keystore`。
- `KEYSTORE_PASSWORD`、`KEY_ALIAS`、`KEY_PASSWORD`。

CI 使用独立的签名身份，但请注意：内核通过管理器签名证书的 SHA-256 哈希来信任它
（`manager/manager.c`）。若用你自己的密钥构建，内核将不认该 App 为管理器。

---

## 6. CI 工作流

`.github/workflows/` 中有三个工作流：

### `build-lkm.yml` —— "Build LKM for nksu"

- 触发：`workflow_dispatch`、`workflow_call`，以及提交到 `main` 且改动 `kernel/**` 或
  `version` 时。
- 按 KMI 矩阵扇出（`fail-fast: false`），为每个 KMI 调用 `ddk-lkm.yml`。

### `ddk-lkm.yml` —— "Build KernelSU Kernel Module"（可复用）

- 在 `ghcr.io/ylarod/ddk:<kmi>-<ddk_release>` 中以 `--privileged` 运行。
- 步骤：checkout → `make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=<debug>` → `llvm-strip -d`
  → 上传。
- 产物：`<kmi>-lkm`。

### `android.yml` —— "Android CI"

- 触发：`workflow_dispatch`、`workflow_run`（在 "Build LKM for nksu" 成功后），以及提交/PR 到
  `main` 且改动 `manager/app/src/**/*.kt`、`manager/app/src/**/*.c`、
  `manager/build.gradle.kts`、`manager/gradle/libs.versions.toml`、`userspace/ncore/**` 或
  `version` 时。
- 步骤：checkout（递归）→ JDK 25 → NDK r29 → 下载最新的 `*-lkm` artifact → 把 `*_nksu.ko`
  放到 `assets/ko/` → 配置 Bazel（固定 bazelisk）→ 构建 `ncore` → 重命名为 `libncore.so` →
  解码发布 keystore → `./gradlew assembleRelease` → 上传。
- 产物：`release`（未归档的发布 APK）。

另有一个独立工作流 `userspace/ncore/.github/workflows/android-arm64.yml` 单独构建 `ncore`，
上传 `ncore-android-arm64`；它不属于根仓库 CI。

---

## 7. 版本流程

```
version (1.0.0)
   ├─► kernel/Kbuild        -DNKSU_VERSION  ─► IOC_GET_VERSION
   └─► app/build.gradle.kts BuildConfig.NKSU_VERSION
                                             └─► 首页与运行中的 LKM 比对
```

LKM 变更时递增 `version`；管理器随后会把过期的模块标记为 **NEED_UPDATE**，并在 LKM 更新前
拒绝安装其自带的 `ncore`。

---

## 8. 贡献说明

- 格式化：`.clang-format`（Linux 内核风格，列宽 120，4 空格缩进）；管理器使用 ktlint。
- 依赖：`renovate.json` 为依赖更新启用了 automerge。
- `.gitignore` 排除了 `manager/app/src/main/assets/ko/`、`manager/app/src/main/jniLibs/` 与
  `release.keystore` —— 它们由构建/CI 生成，不应提交。

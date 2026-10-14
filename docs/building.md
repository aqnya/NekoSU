# Build & Development Guide

**English** | [简体中文](building.zh.md)

This guide covers building all three artifacts from source, the version flow, and the CI
workflows.

---

## 1. Repository layout

```
NekoSU/
├── kernel/            LKM source (built out-of-tree with kbuild)
├── userspace/ncore/   userspace runtime (Bazel + Android NDK)
├── manager/           Android app (Gradle)
├── docs/              this documentation
├── version            1.0.0 — single source of truth for NKSU_VERSION
└── .github/workflows/ CI
```

---

## 2. Prerequisites

| Tool | Version / notes |
| --- | --- |
| Kernel build | A GKI kernel source tree + `aarch64` Clang toolchain (or a DDK container, see below) |
| Bazel | **9.3.0** (pinned in `userspace/ncore/.bazelversion`); `bazelisk` recommended |
| Android NDK | **r29**, `ANDROID_NDK_API_LEVEL=28`, `ANDROID_NDK_HOME` set |
| JDK | **21+** (CI uses 25) |
| Android SDK | compileSdk 37 (major 37, minor 1) |
| Gradle | wrapper pins **9.8.1** (`manager/gradle/wrapper/gradle-wrapper.properties`) |

The manager build also needs prebuilt native artifacts (see [§5](#5-building-the-manager-app)).

---

## 3. Building the kernel module

`kernel/` is built out-of-tree. The root `Makefile` delegates to kbuild:

```sh
make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
```

- `KDIR` — kernel build directory (supply your GKI tree; CI uses DDK containers).
- `CONFIG_NKSU=m` — build `nksu.ko` as a module.
- `CONFIG_NKSU_DEBUG=y` — verbose logging, permissive `nksu` domain, timing instrumentation.
  Set to `n` for a production build (`-O3`), which is what the DDK workflow does.

Config options (`kernel/Kconfig`):

| Option | Default | Purpose |
| --- | --- | --- |
| `NKSU` | `y` | Build the module (tristate). |
| `NKSU_DEBUG` | `y` | Debug mode (depends on `NKSU`). |
| `NKSU_SYSCALL` | `n` | Use the syscall-table dispatcher instead of the default tracepoint hooks. |

Version embedding happens automatically in `kernel/Kbuild`:

- `NKSU_VERSION` ← contents of the root `version` file (fallback `"unknown"`).
- `NKSU_GIT_COMMIT` ← `git rev-parse --short=12 HEAD` (fallback `"unknown"`).

### KMI matrix and DDK containers

The same source adapts to each supported KMI via `LINUX_VERSION_CODE`. CI builds every entry
inside a DDK container `ghcr.io/ylarod/ddk:<kmi>-<ddk_release>` (default release `20260828`):

```
android12-5.10   android13-5.10   android13-5.15   android14-5.15
android14-6.1    android15-6.6    android16-6.12
```

The compiled module is `llvm-strip -d`'d and uploaded as the `<kmi>-lkm` artifact (e.g.
`android15-6.6-lkm`, containing `android15-6.6_nksu.ko`).

---

## 4. Building `ncore`

`userspace/ncore` is Bzlmod-only (no `WORKSPACE`). With the NDK set up:

```sh
cd userspace/ncore
ANDROID_NDK_HOME=/path/to/android-ndk-r29 ANDROID_NDK_API_LEVEL=28 \
  bazel build //:ncore --config=android
```

The output is `bazel-bin/ncore` — a **static arm64 executable**, not a shared library. It is
renamed to `libncore.so` when packaged, but it is executed, never `dlopen`ed.

Key targets (`BUILD.bazel`): `//:argparse`, `//:boot`, `//:module`, `//:cli`, `//:su`, and the
`//:ncore` binary. `--config=android` selects the `//:android_arm64` platform and the
`@androidndk` toolchain.

Compression codecs come from vendored submodules under `third_party/` (`bzip2`, `lz4`, `xz`,
`zlib`), so clone with submodules:

```sh
git clone --recurse-submodules https://github.com/nksu-crew/NekoSU.git
```

---

## 5. Building the manager app

The app needs two artifacts that are **not** committed to the repo:

1. `manager/app/src/main/assets/ko/<kmi>_nksu.ko` — the LKM(s) from the LKM workflow.
2. `manager/app/src/main/jniLibs/arm64-v8a/libncore.so` — the `ncore` binary from
   [§4](#4-building-ncore).

```sh
# from the repo root, with the two artifacts staged:
cd manager
./gradlew assembleRelease
```

What the build does:

- `namespace`/`applicationId` = `me.nekosu.aqnya`; `minSdk 31`, `targetSdk 37`, arm64-v8a only.
- `versionCode` = git commit count, `versionName` = short git hash.
- `BuildConfig.NKSU_VERSION` is read from `../version`.
- Native code is built from `app/src/main/cpp` (CMake target `jni` → `libjni.so`).
- `useLegacyPackaging = true` keeps `.so` files extracted on disk (required because
  `libncore.so` and `libbusybox.so` are executed, not loaded).
- `libbusybox.so` is committed at `app/src/main/jniLibs/arm64-v8a/`.

### Signing

Release signing reads these environment variables; without a keystore the debug keystore is used:

- `KEYSTORE_BASE64` — CI decodes it to `manager/release.keystore` before building.
- `KEYSTORE_PASSWORD`, `KEY_ALIAS`, `KEY_PASSWORD`.

CI uses a separate app signing identity, but note that the kernel trusts the manager by the
SHA-256 hash of its signing certificate (`manager/manager.c`). If you build with your own key,
the kernel will not recognize the app as the manager.

---

## 6. CI workflows

Three workflows live in `.github/workflows/`:

### `build-lkm.yml` — "Build LKM for nksu"

- Triggers: `workflow_dispatch`, `workflow_call`, and push to `main` touching `kernel/**` or
  `version`.
- Fans out over the KMI matrix (`fail-fast: false`) and calls `ddk-lkm.yml` for each KMI.

### `ddk-lkm.yml` — "Build KernelSU Kernel Module" (reusable)

- Runs in `ghcr.io/ylarod/ddk:<kmi>-<ddk_release>` with `--privileged`.
- Steps: checkout → `make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=<debug>` → `llvm-strip -d`
  → upload.
- Artifact: `<kmi>-lkm`.

### `android.yml` — "Android CI"

- Triggers: `workflow_dispatch`, `workflow_run` (after a successful `Build LKM for nksu`), and
  push/PR to `main` touching `manager/app/src/**/*.kt`, `manager/app/src/**/*.c`,
  `manager/build.gradle.kts`, `manager/gradle/libs.versions.toml`, `userspace/ncore/**`, or
  `version`.
- Steps: checkout (recursive) → JDK 25 → NDK r29 → download the latest `*-lkm` artifacts →
  stage `*_nksu.ko` into `assets/ko/` → set up Bazel (pinned bazelisk) → build `ncore` →
  stage it as `libncore.so` → decode the release keystore → `./gradlew assembleRelease` →
  upload.
- Artifact: `release` (the release APK, unarchived).

A standalone workflow `userspace/ncore/.github/workflows/android-arm64.yml` builds `ncore`
alone and uploads `ncore-android-arm64`; it is not part of the root CI.

---

## 7. Version flow

```
version (1.0.0)
   ├─► kernel/Kbuild        -DNKSU_VERSION  ─► IOC_GET_VERSION
   └─► app/build.gradle.kts BuildConfig.NKSU_VERSION
                                             └─► HomeScreen compares vs running LKM
```

Bump `version` when the LKM changes; the manager then flags a stale module as **NEED_UPDATE**
and refuses to install its bundled `ncore` until the LKM is updated.

---

## 8. Contributing notes

- Formatting: `.clang-format` (Linux kernel style, column limit 120, 4-space indent); the
  manager uses ktlint.
- Dependencies: `renovate.json` enables automerge for dependency updates.
- `.gitignore` excludes `manager/app/src/main/assets/ko/`, `manager/app/src/main/jniLibs/`, and
  `release.keystore` — these are build/CI-generated and must not be committed.

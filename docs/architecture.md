**English** | [简体中文](architecture.zh.md)

# Architecture

NekoSU is a kernel-level root solution for Android. It is delivered as three cooperating parts, each living in its own top-level directory:

| Layer | Source | Output | Runs as |
| --- | --- | --- | --- |
| Kernel module (LKM) | `kernel/` | `nksu.ko` | Kernel space |
| Userspace runtime | `userspace/ncore/` | `bazel-bin/ncore`, shipped as `libncore.so` | Root (`u:r:nksu:s0`) |
| Manager app | `manager/` | `manager-*.apk` | Android app process |

The kernel module provides `su`, root-access management, module-mounting hooks and SELinux policy manipulation. Everything the user sees — the UI, module installation, boot-image patching — lives either in the manager app or in the `ncore` executable the kernel launches.

::: tip Layered view

```txt
┌──────────────────────────────────────────────────────────────────────┐
│ Manager app  (me.nekosu.aqnya)                                       │
│   Compose UI · module manager · installer · WebUI host               │
│     │ JNI (libjni.so)                        │ root shell             │
│     ▼                                        ▼                        │
│   prctl/ioctl ────────────────────────►  exec ncore <subcommand>     │
└─────┬───────────────────────────────────────────┬────────────────────┘
      │ control fd [fmac_ctl], shm [fmac_shm]     │
      ▼                                            ▼
┌──────────────────────────────────────────────────────────────────────┐
│ Kernel module  nksu.ko                                               │
│   boot/      first-stage bootstrap + init.rc injection + safe mode    │
│   hook/      syscall table hook · LSM hook · text patching           │
│   manager/   app trust · control fd (ioctl) · feature registry       │
│   privilege/ uid profiles · credential escalation · namespaces       │
│   selinux/   policy dup/restore · rule engine · SELinux hiding       │
│   spawn/     kernel → userspace launcher                             │
│   symbol/    unexported-symbol self-resolution                       │
│   fd/        anon shm + eventfd signalling                           │
└───────────────────────────────┬──────────────────────────────────────┘
                                 │ execve redirect, exec init.rc lines
                                 ▼
┌──────────────────────────────────────────────────────────────────────┐
│ ncore  (/data/adb/nksu/ncore)                                        │
│   su · module lifecycle · sepolicy.rule transport · boot images      │
│   KernelSU-compatible: /data/adb/modules, KSU env vars, window.ksu   │
└──────────────────────────────────────────────────────────────────────┘
```

:::

## Trust and identity {#trust-and-identity}

- The manager app is authenticated by signature: the kernel verifies the signing-certificate hash of `me.nekosu.aqnya` against a built-in constant (`manager/manager.c`).
- The LKM installs a small userspace runtime, `ncore`, into `/data/adb/nksu/` by copying it out of the app's native library directory. `ncore` gets the SELinux label `u:object_r:nksu_file:s0`.
- Root grants are per-UID profiles stored in `/data/adb/nksu/allow.profile` and applied by `privilege/`.

## Kernel module {#kernel-module}

`nksu.ko` is a single LKM built from `kernel/`. Its components are:

| Directory | Responsibility |
| --- | --- |
| `boot/` | Runtime bootstrap: follow `init` through its stages, inject `init.rc` lines that launch `ncore`, volume-up safe mode. It does **not** patch boot images — the manager app does that. |
| `hook/` | Intercept syscalls (`prctl`, `execve`, `execveat`, `setresuid`, path lookups) via the syscall table or tracepoints, patch kernel text, and hook LSM slots at runtime. |
| `manager/` | Manager-app trust, the control file descriptor and its `ioctl` protocol, and the kernel feature registry. |
| `privilege/` | Per-UID root profiles, credential escalation, and namespace switching. |
| `selinux/` | Duplicate/restore the live SELinux policy, apply rule batches from `ncore`, and hide SELinux state from apps. |
| `spawn/` | Launch userspace programs from the kernel without `call_usermodehelper`. |
| `symbol/` | Resolve unexported GKI symbols at runtime (kallsyms scanner + typed indirection). |
| `fd/` | Anonymous shared memory (`[fmac_shm]`) and eventfd-based signalling between kernel and manager. |

See the [Kernel module developer guide](kernel.md) for the full reference.

### Boot flow {#boot-flow}

When `nksu.ko` is loaded from `vendor_boot` at first stage (the current process is `init`, PID 1), the module does **not** initialize everything at once. `core.c` chooses a path:

1. `symbol_compat_init()` resolves all unexported symbols.
2. If loaded by `init`, `hook_init()` installs a temporary syscall watcher and follows `init`:
   - on `selinux_setup`, prepare SELinux;
   - on `second_stage`, initialize the SELinux components;
   - once zygote (`-Xzygote`) appears, install the syscall/tracepoint hooks, initialize the feature components, then tear the watcher down.
3. If loaded late (for example `insmod` from a rooted shell), initialize all components immediately.

`boot/init_rc.c` hooks `read`/`fstat` so that `init` sees an extended `init.rc`:

```txt
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore post-fs-data
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore services
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore boot-completed
```

This is how `ncore` is started at each boot stage without editing the real `init.rc` on disk.

### su redirection {#su-redirection}

The `hook/` layer rewrites executable paths in `execve`/`execveat`: `/system/bin/su` (and `/system/bin/sh`) are redirected to `/data/adb/nksu/ncore`. Before userspace `su` runs, the kernel has already escalated the caller according to its UID profile. For this reason `ncore`'s `su` implementation only parses arguments and execs a shell — it does not manage credentials itself.

## Userspace runtime: ncore {#ncore}

`ncore` is a KernelSU/`ksud`-compatible reimplementation in C++, built with Bazel and the Android NDK. It is shipped inside the APK and executed, not linked. It provides:

- `su` — the shell entry point reached via the kernel path redirect.
- The module lifecycle — `post-fs-data`, `services`, `boot-completed` stage scripts, install, enable/disable, uninstall and actions.
- `sepolicy.rule` parsing and transport to the kernel.
- Boot-image handling — unpack, replace, and patch `vendor_boot` with `nksu.ko`.

KernelSU modules and WebUIs work unchanged because the compatibility contract is preserved:

- Modules live in `/data/adb/modules/<id>` with `module.prop`, `webroot/`, `action.sh`, `system/`, `uninstall.sh`, `sepolicy.rule`, and the `disable`/`remove`/`update` flags.
- The environment exported to module scripts includes `KSU=true`, `KSU_VER=ncore`, `KSU_VER_CODE=30000`, `KSU_KERNEL_VER_CODE=30000`, `KSU_UAPI_VER=5`, `KSU_RUNTIME_MODE=lkm` and `KSU_MODULE=<id>`.
- `/data/adb/ksu` is symlinked to `/data/adb/nksu` (unless a real KernelSU install exists), so tools looking for `/data/adb/ksu/bin/*` keep working.

See the [ncore guide](ncore.md) for details.

## Manager app {#manager-app}

The manager is a single-module Gradle project (`manager/app`), Jetpack Compose + Material 3, `minSdk 31`, arm64-v8a only. It has two channels into the system:

1. **JNI / ioctl** (`core/Ncore.kt` ↔ `cpp/libjni/jni.cc`): talks to the kernel module using `prctl` opcodes and the `[fmac_ctl]` control fd. This drives root profiles, capability sets, SELinux rules, feature toggles and version queries.
2. **Root shell invoking `ncore`** (`data/shell/RootShell.kt`): runs the bundled `/data/adb/nksu/ncore` as root for module operations and boot-image patching.

### Root management {#root-management}

`ui/screens/AppList.kt` / `AppDetailScreen.kt` read kernel profiles via `Ncore.listProfiles()` and write changes with `Ncore.setProfile(...)` / `Ncore.deluid(...)`. A profile holds: whether the app is allowed, the root UID to impersonate, a set of Linux capabilities, a SELinux domain (default `u:r:nksu:s0`), and a namespace mode (`INHERITED=1`, `INDIVIDUAL=2`, `GLOBAL=3`).

### Module management {#module-management}

`data/module/ModuleRepository.kt` enumerates modules with `ncore module list --json`, checks updates over HTTP from each module's `updateJson`, downloads ZIPs with the system `DownloadManager`, and installs with `ncore module install <zip>`.

### WebUI {#webui}

`ui/webui/` hosts KernelSU-compatible WebUIs. A module's `webroot/` is served on the virtual domain `mui.kernelsu.org` from a single `WebView`, with files read as root. The bridge is registered as the global `ksu` object, exposing `window.ksu`: `exec`, `spawn`, `toast`, `moduleInfo`, `listPackages`, `getPackagesInfo`, `fullScreen` and `exit`. The manager also serves `internal/insets.css` and `internal/colors.css` (the Material 3 color scheme as CSS variables), just like KernelSU.

### Installation {#installation}

`ui/screens/install/` + `data/installer/VendorBootInstaller.kt` implement three KernelSU-aligned methods:

1. **File** — patch a user-picked `vendor_boot` image with `ncore -p` and export the result to `Download/`. No root required.
2. **Direct install** — dump the active `vendor_boot` partition with the root helper, patch it, and write it back; rolls back on failure. Requires root + GKI.
3. **Inactive slot** — same as direct install, but targeting the other A/B slot. Requires root, GKI and A/B.

## Kernel ↔ userspace protocol {#protocol}

### Control fd and ioctl {#control-fd}

The manager obtains a control file descriptor from the kernel:

- `prctl(201)` → `[fmac_shm]` (anonymous shared memory, used for path/argument exchange);
- `prctl(203)` → `[fmac_ctl]` (the ioctl control fd);
- `prctl(204)` → `[fmac_ctl]`, available to any root process (used by `ncore`).

The volume-key safe-mode state is exposed as a plain file node, `/proc/nksu/safemode`, which `ncore` reads at each stage.

The single ioctl is `IOC_CMD = _IOWR('F', 0, struct fmac_ioc)`:

```c
struct fmac_ioc { unsigned int flag; unsigned int size; unsigned char data[]; };
```

Payload offsets and sizes are pinned with `static_assert` and defined once in the shared [`uapi/ioctl.h`](../uapi/ioctl.h) header, which the kernel module, the manager's JNI layer and `ncore` all include. See the [full flag table](kernel.md#control-fd--ioctl-abi).

### SELinux rule batches {#selinux-batches}

`ncore` parses `sepolicy.rule` files (KernelSU syntax, with `{ }` cross-product expansion), encodes them into the wire format defined in the shared [`uapi/selinux.h`](../uapi/selinux.h), and sends them with `IOC_SET_SEPOLICY`. A batch is a sequence of `<cmd><subcmd>` records, each followed by up to five `<length><bytes><NUL>` arguments where a zero length means "wildcard". Batches are limited to 8 MiB.

### Boot-image patching {#boot-image-patching}

Boot-image patching is entirely userspace. `ncore` implements its own boot-image reader (no `magiskboot`): it detects `boot`/`vendor_boot` header versions v0–v4, decompresses and recompresses the ramdisk in its original format (gzip/bzip2/lzo/xz/lz4/lzma), and installs `nksu.ko` into a v3/v4 `vendor_boot` ramdisk at `lib/modules/nksu.ko`, registering `modules.load` and `modules.dep`.

## Version coupling {#version-coupling}

A single root `version` file (`1.0.0`) is the source of truth:

- The LKM embeds it as `NKSU_VERSION` and reports it over `IOC_GET_VERSION`.
- The manager embeds it as `BuildConfig.NKSU_VERSION` and compares it against the running LKM on the home screen. A mismatch is surfaced as **NEED_UPDATE**, and the manager refuses to install its bundled `ncore` to avoid a userspace/kernel skew.

The kernel git commit (`NKSU_GIT_COMMIT`) and the app's `versionCode`/`versionName` are derived from git at build time.

# ncore — Userspace Runtime

**English** | [简体中文](ncore.zh.md)

`ncore` is NekoSU's userspace module runtime. It is a KernelSU/`ksud`-compatible
reimplementation in C++, built with Bazel + the Android NDK and shipped inside the manager APK
as `libncore.so`. Despite the `.so` extension it is a **static executable** that the kernel
starts and that the manager invokes via a root shell — it is never `dlopen`ed.

- Source: `userspace/ncore/`
- Built artifact: `bazel-bin/ncore`
- Installed to: `/data/adb/nksu/ncore` (SELinux label `u:object_r:nksu_file:s0`)
- High-level role: see [Architecture & Design](architecture.md#3-userspace-runtime-ncore)
- Build instructions: see [Build & Development Guide](building.md#4-building-ncore)

---

## 1. Directory map

```
userspace/ncore/
├── .bazelrc / .bazelversion    build config, pinned Bazel 9.3.0
├── MODULE.bazel               Bzlmod (no WORKSPACE)
├── BUILD.bazel                //:argparse //:boot //:module //:cli //:su //:ncore
├── android_ndk.bzl            @androidndk module extension
├── include/
│   ├── cli.hpp                cli::arguments, cli::parse_cli
│   ├── argparse/argparse.hpp  vendored p-ranav/argparse (header-only)
│   ├── boot/boot.hpp          boot::unpack / replace / patch
│   ├── module/module.hpp      modules::* public API
│   └── su/su.hpp              su::is_su_invocation / su::run
├── src/
│   ├── main.cc                entry: su dispatch or CLI
│   ├── cli/cli.cc             argparse wiring + subcommands
│   ├── su/su.cc               su implementation
│   ├── module/
│   │   ├── module.cc          lifecycle, install, list
│   │   ├── module_internal.hpp internal defs/utils API
│   │   ├── sepolicy.cc        sepolicy.rule parser + kernel transport
│   │   ├── utils.cc           path constants + fs/prop helpers
│   │   └── zip.cc/.hpp        minimal read-only ZIP reader
│   └── boot/
│       ├── prase.cc           boot type probe (sic: "prase")
│       ├── unpack.cc          unpack + format detection/compression
│       ├── replace.cc         collect/rebuild sections + replace
│       ├── patch.cc           vendor_boot v3/v4 nksu.ko injection
│       ├── cpio.cc/.hpp       newc cpio parse/pack/put
│       ├── bootimg.h          AOSP boot/vendor header structs
│       ├── boot_internal.hpp  MMapFile, section, codec declarations
│       └── gzip.cc bzip2.cc lz4.cc lzma.cc lzop.cc
└── third_party/               bzip2, lz4, lzo, xz, zlib (submodules)
```

Only two `WORKSPACE`-era facts matter: there is no `WORKSPACE` (Bzlmod only) and the compression
libraries come from git submodules under `third_party/`.

---

## 2. CLI reference

`src/main.cc` dispatches:

1. If the program is invoked as `su` (basename `"su"`) → `su::run`.
2. Else if `argv[1] == "su"` → `su::run(argc-1, argv+1)`.
3. Else → `cli::parse_cli` (program name `ncore`, version `ncore 1.0.0`).

| Command | Description |
| --- | --- |
| `post-fs-data` | Run the `post-fs-data` boot stage (`modules::on_post_fs_data`) |
| `services` | Run the `services` boot stage |
| `boot-completed` | Run the `boot-completed` boot stage |
| `install` | Install the runtime into `/data/adb/nksu/` |
| `module list [--json]` | List modules (text or JSON) |
| `module enable <id>` / `disable <id>` | Toggle a module |
| `module uninstall <id>` / `undo-uninstall <id>` | Mark/unmark for removal |
| `module action <id>` | Run `<id>/action.sh`, propagating its exit code |
| `module install <zip>` | Install a Magisk/KernelSU module ZIP |
| `initrc refresh` | Regenerate the preinit `modules.rc` |
| `-u, --unpack <path>` | Unpack a boot image |
| `-r, --replace <img> <kernel\|dtb>` | Replace a boot image section |
| `-p, --patch <vendor_boot.img> <ko>` | Patch a v3/v4 vendor_boot with `nksu.ko` |

With no arguments it prints help and returns 0.

Concrete invocations used elsewhere:

```sh
# kernel-injected init.rc
/data/adb/nksu/ncore post-fs-data
/data/adb/nksu/ncore services
/data/adb/nksu/ncore boot-completed

# manager (root shell)
/data/adb/nksu/ncore module list --json
/data/adb/nksu/ncore module install /sdcard/mod.zip

# manager (no root; writes <image>.new)
/data/adb/nksu/ncore -p vendor_boot.img nksu.ko
```

---

## 3. `su`

`su` is reached because the kernel redirects `execve`/`execveat` of `/system/bin/su` (and
`/system/bin/sh`) to `/data/adb/nksu/ncore` while keeping `argv[0]` so `ncore` can detect the
invocation. By the time userspace runs, the kernel has already escalated the process according
to the caller's UID profile — so `ncore`'s `su` only parses arguments and execs a shell.

Supported options:

| Option | Meaning |
| --- | --- |
| `-c, --command` | Join all remaining argv with spaces and run as a command |
| `-s, --shell` | Shell to execute |
| `-u, --user` | Target user (name or numeric uid) |
| `-g, --group` | Target group |
| `-G, --supp-group` | Supplementary group |
| `-p, --preserve-environment` | Keep the existing environment |
| `-l, --login` | Login shell |
| `-M, --mount-master` | `setns` to `/proc/1/ns/mnt` |
| `-Z, --context` | Write `/proc/self/attr/current` |
| `-v, --version` / `-h, --help` | Info |

Identity handling resolves names via `getpwuid`/`getpwnam`/`getgrnam`; unknown numeric uids keep
`gid == uid` (Android `u0_aNNN`). `can_switch_user()` guards the `setgroups`/`setgid`/`setuid`
path by checking for root or `CAP_SETUID|CAP_SETGID` (avoiding a seccomp `SIGSYS`). Unless `-p`
is given, `HOME`/`USER`/`LOGNAME`/`SHELL` are set, PATH gets `/data/adb/nksu` prepended, and
`umask(022)` is applied. The default shell is `/system/bin/sh`.

---

## 4. Module runtime

### Layout and compatibility

Modules live in `/data/adb/modules/<id>` and follow the KernelSU/`ksud` convention:

- `module.prop` (Java properties), `system/` overlay, `webroot/`, `action.sh`,
  `uninstall.sh`, `system.prop`, `sepolicy.rule`, `initrc/`, `skip_mount`;
- control files `disable`, `remove`, `update`, and an `update/` staging dir.

Module IDs must be ≥ 2 chars, start with a letter, and contain only `[A-Za-z0-9._-]`.

The runtime directory is `/data/adb/nksu/` (overridable via `NCORE_ADB_DIR`,
`NCORE_WORKING_DIR`, `NCORE_BOOT_PATH`, `NCORE_PREINIT_DIR_WATCHDOG`,
`NCORE_PREINIT_DIR_DEFAULT`). `/data/adb/ksu` is symlinked to `/data/adb/nksu` unless a real
KernelSU install exists, so tools expecting `/data/adb/ksu/bin/*` keep working.

### Environment exported to scripts

`build_env()` exports to every module script:

```
ASH_STANDALONE=1
KSU=true
KSU_KERNEL_VER_CODE=30000
KSU_VER_CODE=30000
KSU_VER=ncore
KSU_UAPI_VER=5
KSU_RUNTIME_MODE=lkm
KSU_MODULE=<id>
```

`PATH` prepends `/data/adb/nksu/bin/`. Shell resolution order: bundled
`/data/adb/nksu/bin/busybox`, then `which(busybox)`, then `/system/bin/sh`.

### Boot stages

Each stage first runs the global `/data/adb/<stage>.d` scripts, then the metamodule stage
script, then every active module's `<stage>.sh`:

- `on_post_fs_data` — has a 35 s deadline (`BOOT_STAGE_TIMEOUT_SEC`). It handles the common
  `post-fs-data.d` scripts, safe mode (disabling all modules), swaps `modules_update/<id>` into
  `modules/<id>` preserving `disable`/`remove`, prunes removed modules (running
  `uninstall.sh`), regenerates the preinit `modules.rc`, loads `sepolicy.rule` files, runs stage
  scripts, applies `system.prop` via `setprop`, and mounts the metamodule.
- `on_services` / `on_boot_completed` — `NoWait`. Boot-completed also notifies the kernel
  (`NKSU_PRCTL_BOOT_COMPLETED`) to stop the safe-mode watcher.

Safe mode is active if `persist.sys.safemode`/`ro.sys.safemode` is set or
`/data/adb/nksu/safe_module` exists (created by the kernel volume-up watcher).

### Metamodules

`metamodule=1` or `=true` in `module.prop` designates a module that takes over mounting. It is
symlinked at `/data/adb/metamodule` and may provide `metamount.sh`, `metainstall.sh`, and
`metauninstall.sh`. Installs are blocked when a metamodule with `metainstall.sh` has pending
changes (`metamodule_check_install_safety`).

### File formats and the installer prelude

- Java-properties parsing drives `module.prop` and `system.prop`.
- `regenerate_preinit_rc` collects `initrc.d/*.rc` (execute bit = enabled) and each module's
  `initrc/*.rc` into `/metadata/watchdog/nksu/` or `/metadata/nksu/`.
- `sepolicy.rule` files use KernelSU syntax and are parsed by `sepolicy.cc`
  (see [§5](#5-kernel-interfaces)).
- Module ZIPs are read by a native `ZipArchive` (stored/deflate/xz, ZIP64, symlinks,
  path-traversal-safe, `META-INF/` skipped); `SKIPUNZIP=1` in `customize.sh` is honored.
- The install shell prelude provides Magisk-style helpers (`ui_print`, `abort`, `grep_prop`,
  `set_perm[_recursive]`, `mktouch`, `mark_remove`, `mark_replace`, `api_level_arch_detect`,
  `handle_partition`, `install_module`) plus `BOOTMODE`, `NVBASE`, `MODULOOT`, `MODPATH`,
  `ZIPFILE`, `MAGISK_VER=25.2`, `ARCH`/`ABI`/`ABI32`/`IS64BIT`.

### `module list --json`

Returns an array of objects:

```json
{
  "id": "...", "name": "...", "version": "...", "versionCode": 0,
  "updateJson": "...", "author": "...", "description": "...",
  "enabled": true, "metamodule": false, "update": false, "remove": false,
  "skipMount": false, "hasSystem": false,
  "hasActionScript": false, "hasWebUi": true
}
```

---

## 5. Kernel interfaces

### Control fd

`ncore` obtains the control fd with `prctl(204)` (`NKSU_PRCTL_GET_DRIVER_FD`) and locates it by
scanning `/proc/self/fd` for the `[fmac_ctl]` link (`find_driver_fd`). The ioctl is
`IOC_CMD = _IOWR('F', 0, struct fmac_ioc)` with the flag set defined in the kernel's
`manager/ioctl.h`. Only `IOC_SET_SEPOLICY` (12) is used by `sepolicy.cc`; other flags are used
by the manager's JNI layer.

### sepolicy.rule transport

`sepolicy.cc` parses KernelSU-style rules into `Atomic` records and sends them in batches:

```
<u32 cmd><u32 subcmd> (<u32 len><bytes>'\0'){0,5}
```

A `len == 0` argument is a wildcard. Supported commands include `allow`, `deny`,
`auditallow`, `dontaudit`, `allowxperm`/`auditallowxperm`/`dontauditxperm` (ioctl only),
`permissive`/`enforce`, `type`, `typeattribute`/`attradd`, `attribute`,
`type_transition`/`name_transition`, `type_change`/`type_member`, `genfscon`, with `{ }`
cross-product expansion. If no control fd is available, sepolicy rules are skipped with a log.

### Boot-completed notification

`notify_boot_completed()` calls `prctl(205)` so the kernel can stop its volume-key safe-mode
watcher.

---

## 6. Boot image handling

`ncore` implements its own boot-image reader — it does **not** use `magiskboot`:

- `probe_type` identifies `ANDROID!`/`VNDRBOOT` images and header versions v0–v4.
- `unpack` dumps sections (`kernel`, `ramdisk`, `second`, `recovery_dtbo`, `dtb`,
  `boot_signature`, `vendor_ramdisk`, `vendor_dtb`, `vendor_ramdisk_table`, `bootconfig`) and
  detects gzip/gzip2/bzip2/lzo/xz/lz4 (block/legacy/lg/frame)/lzma.
- `replace` rebuilds the image with page-aligned relayout and recompresses to the original
  encoding.
- `patch` installs `nksu.ko` at `lib/modules/nksu.ko` in a v3/v4 vendor_boot ramdisk and
  registers `lib/modules/modules.load` (`nksu.ko`) and `lib/modules/modules.dep`
  (`/lib/modules/nksu.ko:`), preserving per-sub-ramdisk compression and the v4 table. The
  result is written as `<image>.new`.

---

## 7. Extending `ncore`

- Subcommands live in `cli/cli.cc`; keep `argparse` registration and help text consistent.
- Module behaviour is in `src/module/`; public API in `include/module/module.hpp`, internals in
  `src/module/module_internal.hpp`.
- New kernel ABI usage must match `kernel/manager/ioctl.h` and the manager's
  `cpp/libjni/ioctl.h`.
- Build with `bazel build //:ncore --config=android`; CI stages the result as `libncore.so`.
- When you change userspace behaviour that pairs with a kernel change, bump the root `version`
  file.

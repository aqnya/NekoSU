[English](ncore.md) | **简体中文**

# ncore

`ncore` 是 NekoSU 的用户态模块运行时。它是用 C++ 编写的 KernelSU/`ksud` 兼容实现，使用 Bazel + Android NDK 构建，并作为 `libncore.so` 打包在管理器 APK 中。尽管带有 `.so` 扩展名，它其实是一个**静态可执行文件**，由内核启动、由管理器通过 root shell 调用——从不 `dlopen`。

- 源码：`userspace/ncore/`
- 构建产物：`bazel-bin/ncore`
- 安装到：`/data/adb/nksu/ncore`（SELinux 标签 `u:object_r:nksu_file:s0`）
- 整体角色：见 [架构 § ncore](architecture.zh.md#ncore)
- 构建说明：见 [如何构建 § 构建 ncore](building.zh.md#build-ncore)

## 目录结构 {#directory-layout}

```txt
userspace/ncore/
├── .bazelrc / .bazelversion    构建配置，固定 Bazel 9.3.0
├── MODULE.bazel               Bzlmod（无 WORKSPACE）
├── BUILD.bazel                //:argparse //:boot //:module //:cli //:su //:ncore
├── android_ndk.bzl            @androidndk 模块扩展
├── include/
│   ├── cli.hpp                cli::arguments、cli::parse_cli
│   ├── argparse/argparse.hpp  内置 p-ranav/argparse（仅头文件）
│   ├── boot/boot.hpp          boot::unpack / replace / patch
│   ├── module/module.hpp      modules::* 公共 API
│   └── su/su.hpp              su::is_su_invocation / su::run
├── src/
│   ├── main.cc                入口：su 分发或 CLI
│   ├── cli/cli.cc             argparse 接线 + 子命令
│   ├── su/su.cc               su 实现
│   ├── module/
│   │   ├── module.cc          生命周期、安装、列表
│   │   ├── module_internal.hpp 内部 defs/utils API
│   │   ├── sepolicy.cc        sepolicy.rule 解析 + 内核传输
│   │   ├── utils.cc           路径常量 + 文件系统/属性辅助
│   │   └── zip.cc/.hpp        极简只读 ZIP 读取器
│   └── boot/
│       ├── prase.cc           boot 类型探测（原文件名如此）
│       ├── unpack.cc          解包 + 格式检测/压缩
│       ├── replace.cc         收集/重建分区 + 替换
│       ├── patch.cc           vendor_boot v3/v4 注入 nksu.ko
│       ├── cpio.cc/.hpp       newc cpio 解析/打包/写入
│       ├── bootimg.h          AOSP boot/vendor 头部结构
│       ├── boot_internal.hpp  MMapFile、section、编解码器声明
│       └── gzip.cc bzip2.cc lz4.cc lzma.cc lzop.cc
└── third_party/               bzip2、lz4、lzo、xz、zlib（子模块）
```

::: info
没有 `WORKSPACE`（仅 Bzlmod），压缩库来自 `third_party/` 下的 git 子模块。
:::

## CLI 参考 {#cli}

`src/main.cc` 的分发：

1. 若程序以 `su` 身份调用（basename 为 `"su"`）→ `su::run`。
2. 否则若 `argv[1] == "su"` → `su::run(argc-1, argv+1)`。
3. 否则 → `cli::parse_cli`（程序名 `ncore`，版本 `ncore 1.0.0`）。

| 命令 | 说明 |
| --- | --- |
| `post-fs-data` | 运行 `post-fs-data` 启动阶段（`modules::on_post_fs_data`） |
| `services` | 运行 `services` 启动阶段 |
| `boot-completed` | 运行 `boot-completed` 启动阶段 |
| `install` | 把运行时安装到 `/data/adb/nksu/` |
| `module list [--json]` | 列出模块（文本或 JSON） |
| `module enable <id>` / `disable <id>` | 切换模块 |
| `module uninstall <id>` / `undo-uninstall <id>` | 标记/取消移除 |
| `module action <id>` | 运行 `<id>/action.sh`，并传递其退出码 |
| `module install <zip>` | 安装 Magisk/KernelSU 模块 ZIP |
| `initrc refresh` | 重新生成预初始化 `modules.rc` |
| `-u, --unpack <path>` | 解包 boot 镜像 |
| `-r, --replace <img> <kernel\|dtb>` | 替换 boot 镜像分区 |
| `-p, --patch <vendor_boot.img> <ko>` | 用 `nksu.ko` 修补 v3/v4 vendor_boot |

无参数时打印帮助并返回 0。其它地方实际使用的命令：

```sh
# 内核注入的 init.rc
/data/adb/nksu/ncore post-fs-data
/data/adb/nksu/ncore services
/data/adb/nksu/ncore boot-completed

# 管理器（root shell）
/data/adb/nksu/ncore module list --json
/data/adb/nksu/ncore module install /sdcard/mod.zip

# 管理器（无需 root；写出 <image>.new）
/data/adb/nksu/ncore -p vendor_boot.img nksu.ko
```

## su {#su}

`su` 能够被触发，是因为内核把 `/system/bin/su`（以及 `/system/bin/sh`）的 `execve`/`execveat` 重定向到 `/data/adb/nksu/ncore`，同时保留 `argv[0]`，使 `ncore` 能识别该调用。到用户态运行时，内核已按调用者的 UID 档案完成提权——因此 `ncore` 的 `su` 只解析参数并 exec 一个 shell。

支持的选项：

| 选项 | 含义 |
| --- | --- |
| `-c, --command` | 把其余 argv 用空格连接后作为命令运行 |
| `-s, --shell` | 要执行的 shell |
| `-u, --user` | 目标用户（名称或数字 uid） |
| `-g, --group` | 目标组 |
| `-G, --supp-group` | 附加组 |
| `-p, --preserve-environment` | 保留现有环境 |
| `-l, --login` | 登录 shell |
| `-M, --mount-master` | `setns` 到 `/proc/1/ns/mnt` |
| `-Z, --context` | 写入 `/proc/self/attr/current` |
| `-v, --version` / `-h, --help` | 信息 |

身份处理通过 `getpwuid`/`getpwnam`/`getgrnam` 解析名称；未知数字 uid 保持 `gid == uid`（Android `u0_aNNN`）。`can_switch_user()` 通过检查 root 或 `CAP_SETUID|CAP_SETGID` 来守护 `setgroups`/`setgid`/`setuid` 路径（避免 seccomp `SIGSYS`）。除非指定 `-p`，否则会设置 `HOME`/`USER`/`LOGNAME`/`SHELL`，把 `/data/adb/nksu` 前置到 `PATH`，并执行 `umask(022)`。默认 shell 为 `/system/bin/sh`。

## 模块运行时 {#module-runtime}

### 布局与兼容性 {#module-layout}

模块位于 `/data/adb/modules/<id>`，遵循 KernelSU/`ksud` 约定：

- `module.prop`（Java properties）、`system/` 覆盖、`webroot/`、`action.sh`、`uninstall.sh`、`system.prop`、`sepolicy.rule`、`initrc/`、`skip_mount`；
- 控制文件 `disable`、`remove`、`update`，以及 `update/` 暂存目录。

模块 ID 要求：长度 ≥ 2，首字符为字母，仅含 `[A-Za-z0-9._-]`。

运行时目录为 `/data/adb/nksu/`（可通过 `NCORE_ADB_DIR`、`NCORE_WORKING_DIR`、`NCORE_BOOT_PATH`、`NCORE_PREINIT_DIR_WATCHDOG`、`NCORE_PREINIT_DIR_DEFAULT` 覆盖）。`/data/adb/ksu` 被软链接到 `/data/adb/nksu`（除非已存在真正的 KernelSU 安装），使依赖 `/data/adb/ksu/bin/*` 的工具继续工作。

### 向脚本导出的环境变量 {#environment}

`build_env()` 向每个模块脚本导出：

```txt
ASH_STANDALONE=1
KSU=true
KSU_KERNEL_VER_CODE=30000
KSU_VER_CODE=30000
KSU_VER=ncore
KSU_UAPI_VER=5
KSU_RUNTIME_MODE=lkm
KSU_MODULE=<id>
```

`PATH` 前置 `/data/adb/nksu/bin/`。shell 解析顺序：内置的 `/data/adb/nksu/bin/busybox`，其次 `which(busybox)`，最后 `/system/bin/sh`。

### 启动阶段 {#boot-stages}

每个阶段先运行全局 `/data/adb/<stage>.d` 脚本，再运行 metamodule 阶段脚本，最后运行每个活动模块的 `<stage>.sh`：

- `on_post_fs_data` —— 有 35 秒期限（`BOOT_STAGE_TIMEOUT_SEC`）。它先读取 `/proc/nksu/safemode`，若内核音量键观察器锁存了安全模式，则创建 `/data/adb/nksu/safe_module`。然后处理公共 `post-fs-data.d` 脚本、安全模式（禁用所有模块）、把 `modules_update/<id>` 换入 `modules/<id>`（保留 `disable`/`remove`）、清理被移除的模块（运行 `uninstall.sh`）、重新生成预初始化 `modules.rc`、加载 `sepolicy.rule`、运行阶段脚本、通过 `setprop` 应用 `system.prop`，并挂载 metamodule。
- `on_services` / `on_boot_completed` —— `NoWait`。两者在运行各自阶段前也会读取 `/proc/nksu/safemode`（并在需要时创建标记）。

若设置了 `persist.sys.safemode`/`ro.sys.safemode`、存在 `/data/adb/nksu/safe_module`（由 ncore 在内核音量键观察器报告安全模式时创建），或内核 `/proc/nksu/safemode` 读出 `1`，则安全模式生效。

### Metamodule {#metamodules}

`module.prop` 中 `metamodule=1` 或 `=true` 表示该模块接管挂载。它被软链接到 `/data/adb/metamodule`，并可提供 `metamount.sh`、`metainstall.sh`、`metauninstall.sh`。当带有 `metainstall.sh` 的 metamodule 存在未完成变更时会阻止安装（`metamodule_check_install_safety`）。

### 文件格式与安装器前导 {#file-formats}

- Java-properties 解析驱动 `module.prop` 与 `system.prop`。
- `regenerate_preinit_rc` 收集 `initrc.d/*.rc`（可执行位 = 启用）与各模块的 `initrc/*.rc`，生成到 `/metadata/watchdog/nksu/` 或 `/metadata/nksu/`。
- `sepolicy.rule` 使用 KernelSU 语法，由 `sepolicy.cc` 解析（见[内核接口](#kernel-interfaces)）。
- 模块 ZIP 由原生 `ZipArchive` 读取（stored/deflate/xz、ZIP64、符号链接、防路径穿越、跳过 `META-INF/`）；`customize.sh` 中的 `SKIPUNZIP=1` 会被遵守。
- 安装 shell 前导提供 Magisk 风格辅助函数（`ui_print`、`abort`、`grep_prop`、`set_perm[_recursive]`、`mktouch`、`mark_remove`、`mark_replace`、`api_level_arch_detect`、`handle_partition`、`install_module`），以及 `BOOTMODE`、`NVBASE`、`MODULOOT`、`MODPATH`、`ZIPFILE`、`MAGISK_VER=25.2`、`ARCH`/`ABI`/`ABI32`/`IS64BIT`。

### module list --json {#module-list-json}

返回对象数组：

```json
{
  "id": "...", "name": "...", "version": "...", "versionCode": 0,
  "updateJson": "...", "author": "...", "description": "...",
  "enabled": true, "metamodule": false, "update": false, "remove": false,
  "skipMount": false, "hasSystem": false,
  "hasActionScript": false, "hasWebUi": true
}
```

## 内核接口 {#kernel-interfaces}

### 控制 fd {#control-fd}

`ncore` 通过 `prctl(204)`（`NKSU_PRCTL_GET_DRIVER_FD`）获取控制 fd，并通过扫描 `/proc/self/fd` 查找 `[fmac_ctl]` 链接来定位它（`find_driver_fd`）。ioctl 为 `IOC_CMD = _IOWR('F', 0, struct fmac_ioc)`，标志集定义于共享的 [`uapi/ioctl.h`](../uapi/ioctl.h)。`sepolicy.cc` 只使用 `IOC_SET_SEPOLICY`（12）；其它标志由管理器的 JNI 层使用。

### sepolicy.rule 传输 {#sepolicy-transport}

`sepolicy.cc` 把 KernelSU 风格规则解析为 `Atomic` 记录并分批发送：

```txt
<u32 cmd><u32 subcmd> (<u32 len><bytes>'\0'){0,5}
```

`len == 0` 的参数表示通配。支持的命令包括 `allow`、`deny`、`auditallow`、`dontaudit`、`allowxperm`/`auditallowxperm`/`dontauditxperm`（仅 ioctl）、`permissive`/`enforce`、`type`、`typeattribute`/`attradd`、`attribute`、`type_transition`/`name_transition`、`type_change`/`type_member`、`genfscon`，支持 `{ }` 笛卡尔展开。若没有可用的控制 fd，sepolicy 规则会被跳过并记录日志。

### 安全模式查询 {#safe-mode-query}

`sync_safe_mode_from_kernel()` 读取 `/proc/nksu/safemode`；当读出 `1` 时，ncore 创建 `/data/adb/nksu/safe_module`，使本次启动的其余部分（以及后续启动）跳过模块加载。它在每个阶段开始时调用。

## boot 镜像处理 {#boot-image}

`ncore` 自带 boot 镜像读写实现——**不**使用 `magiskboot`：

- `probe_type` 识别 `ANDROID!`/`VNDRBOOT` 镜像及头部版本 v0–v4。
- `unpack` 导出各分区（`kernel`、`ramdisk`、`second`、`recovery_dtbo`、`dtb`、`boot_signature`、`vendor_ramdisk`、`vendor_dtb`、`vendor_ramdisk_table`、`bootconfig`），并检测 gzip/gzip2/bzip2/lzo/xz/lz4（block/legacy/lg/frame）/lzma。
- `replace` 以页对齐重新布局并重建镜像，重新压缩为原始编码。
- `patch` 把 `nksu.ko` 安装到 v3/v4 vendor_boot ramdisk 的 `lib/modules/nksu.ko`，并注册 `lib/modules/modules.load`（`nksu.ko`）与 `lib/modules/modules.dep`（`/lib/modules/nksu.ko:`），保留各子 ramdisk 的压缩与 v4 表。结果写为 `<image>.new`。

## 扩展 ncore {#extending}

- 子命令位于 `cli/cli.cc`；保持 `argparse` 注册与帮助文本一致。
- 模块行为位于 `src/module/`；公共 API 在 `include/module/module.hpp`，内部在 `src/module/module_internal.hpp`。
- 新增内核 ABI 使用必须与共享的 `uapi/ioctl.h` / `uapi/selinux.h` 保持一致。
- 用 `bazel build //:ncore --config=android` 构建；CI 会把结果作为 `libncore.so` 暂存。
- 当你修改的用户态行为与内核改动配套时，递增根目录 `version` 文件。

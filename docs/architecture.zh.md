# 架构与设计

[English](architecture.md) | **简体中文**

NekoSU 是 Android 平台上的内核级 Root 方案。它由三个协同工作的部分组成，每个部分位于各自的顶层目录：

| 层 | 源码 | 产物 | 运行身份 |
| --- | --- | --- | --- |
| 内核模块（LKM） | `kernel/` | `nksu.ko` | 内核态 |
| 用户态运行时 | `userspace/ncore/` | `bazel-bin/ncore`，打包为 `libncore.so` | Root（init 域 `u:r:nksu:s0`） |
| 管理器 App | `manager/` | `manager-*.apk` | Android 应用进程 |

内核模块提供 `su`、Root 权限管理、模块挂载 hook 与 SELinux 策略操作。用户可见的一切
（界面、模块安装、boot 镜像修补）要么在管理器 App 中，要么在内核启动的 `ncore` 可执行文件中。

---

## 1. 分层视图

```
┌──────────────────────────────────────────────────────────────────────┐
│ Manager app  (me.nekosu.aqnya)                                       │
│   Compose UI · 模块管理 · 安装器 · WebUI 宿主                        │
│     │ JNI (libjni.so)                        │ root shell             │
│     ▼                                        ▼                        │
│   prctl/ioctl ────────────────────────►  exec ncore <子命令>         │
└─────┬───────────────────────────────────────────┬────────────────────┘
      │ 控制 fd [fmac_ctl]、共享内存 [fmac_shm]    │
      ▼                                            ▼
┌──────────────────────────────────────────────────────────────────────┐
│ 内核模块  nksu.ko                                                    │
│   boot/      一阶段引导 + init.rc 注入 + 安全模式                    │
│   hook/      系统调用表 hook · LSM hook · 内核文本补丁               │
│   manager/   应用信任 · 控制 fd（ioctl）· 特性注册表                 │
│   privilege/ uid 档案 · 凭证提升 · 命名空间                          │
│   selinux/   策略复制/恢复 · 规则引擎 · SELinux 隐藏                 │
│   spawn/     内核 → 用户态启动器                                     │
│   symbol/    未导出符号自解析                                        │
│   fd/        匿名共享内存 + eventfd 通知                             │
└───────────────────────────────┬──────────────────────────────────────┘
                                 │ execve 重定向、exec init.rc 行
                                 ▼
┌──────────────────────────────────────────────────────────────────────┐
│ ncore  (/data/adb/nksu/ncore)                                        │
│   su · 模块生命周期 · sepolicy.rule 传输 · boot 镜像                 │
│   兼容 KernelSU：/data/adb/modules、KSU 环境变量、window.ksu         │
└──────────────────────────────────────────────────────────────────────┘
```

### 信任与身份

- 管理器 App 通过签名认证：内核将 `me.nekosu.aqnya` 的签名证书哈希与内置常量比对
  （`manager/manager.c`）。
- LKM 会把用户态运行时 `ncore` 从 App 的 native library 目录复制安装到 `/data/adb/nksu/`。
  `ncore` 获得 SELinux 标签 `u:object_r:nksu_file:s0`。
- Root 授权是按 UID 的档案，保存在 `/data/adb/nksu/allow.profile`，由 `privilege/` 应用。

---

## 2. 内核模块

`nksu.ko` 是由 `kernel/` 构建的单一 LKM（完整的目录参考见[内核模块开发者文档](kernel.zh.md)）。
其组成部分如下：

| 目录 | 职责 |
| --- | --- |
| `boot/` | 运行时引导：跟随 `init` 的各阶段，注入启动 `ncore` 的 `init.rc` 行，音量键安全模式。（它**不**修补 boot 镜像——那是管理器 App 的工作。） |
| `hook/` | 通过系统调用表或 tracepoint 拦截系统调用（`prctl`、`execve`、`execveat`、`setresuid`、路径查询），修补内核文本，并在运行时 hook LSM 槽位。 |
| `manager/` | 管理器 App 信任、控制文件描述符及其 `ioctl` 协议、内核特性注册表。 |
| `privilege/` | 按 UID 的 Root 档案、凭证提升、命名空间切换。 |
| `selinux/` | 复制/恢复活动 SELinux 策略，应用来自 `ncore` 的规则批次，对应用隐藏 SELinux 状态。 |
| `spawn/` | 不借助 `call_usermodehelper` 从内核启动用户态程序。 |
| `symbol/` | 运行时解析未导出的 GKI 符号（kallsyms 扫描 + 类型化间接调用）。 |
| `fd/` | 内核与管理器之间的匿名共享内存（`[fmac_shm]`）与基于 eventfd 的通知。 |

### 启动流程

当 `nksu.ko` 在 `vendor_boot` 一阶段（当前进程为 `init`，pid 1）被加载时，模块并**不会**
一次性初始化全部内容。`core.c` 决定路径：

1. `symbol_compat_init()` 解析全部未导出符号。
2. 若由 `init` 加载 → `hook_init()` 安装临时系统调用观察器并跟随 `init`：
   - 遇到 `selinux_setup` → 准备 SELinux；
   - 遇到 `second_stage` → 初始化 SELinux 组件；
   - 一旦出现 zygote（`-Xzygote`）→ 安装系统调用/tracepoint hook，初始化特性组件，
     然后拆除临时观察器。
3. 若为后期加载（例如被已 Root 的 shell `insmod`）→ 立即初始化全部组件。

`boot/init_rc.c` hook 了 `read`/`fstat`，使 `init` 看到一份被追加内容的 `init.rc`：

```
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore post-fs-data
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore services
exec u:r:nksu:s0 root -- /data/adb/nksu/ncore boot-completed
```

这就是 `ncore` 在每个启动阶段被拉起的方式，而无需修改磁盘上真实的 `init.rc`。

### `su` 重定向

`hook/` 层会重写 `execve`/`execveat` 中的可执行路径：`/system/bin/su`（以及
`/system/bin/sh`）被重定向到 `/data/adb/nksu/ncore`。在用户态 `su` 运行之前，内核已按调用者
的 UID 档案完成了凭证提升。因此 `ncore` 的 `su` 实现只解析参数并 exec 一个 shell——它自己
不管理凭证。

---

## 3. 用户态运行时：`ncore`

`ncore` 是用 C++ 编写的 KernelSU/`ksud` 兼容实现，使用 Bazel 与 Android NDK 构建。它打包在
APK 中并被**执行**（而非链接）。它提供：

- `su` —— 通过内核路径重定向到达的 shell 入口。
- 模块生命周期 —— `post-fs-data`、`services`、`boot-completed` 阶段脚本，安装、启用/禁用、
  卸载、动作。
- `sepolicy.rule` 的解析与向内核的传输。
- boot 镜像处理 —— 解包、替换，并把 `nksu.ko` 打入 `vendor_boot`。

兼容性契约（使 KernelSU 模块与 WebUI 无需改动即可工作）：

- 模块位于 `/data/adb/modules/<id>`，包含 `module.prop`、`webroot/`、`action.sh`、`system/`、
  `uninstall.sh`、`sepolicy.rule`，以及 `disable`/`remove`/`update` 标志文件。
- 向模块脚本导出的环境变量：`KSU=true`、`KSU_VER=ncore`、`KSU_VER_CODE=30000`、
  `KSU_KERNEL_VER_CODE=30000`、`KSU_UAPI_VER=5`、`KSU_RUNTIME_MODE=lkm`、`KSU_MODULE=<id>`。
- `/data/adb/ksu` 被软链接到 `/data/adb/nksu`（除非已存在真正的 KernelSU 安装），
  因此寻找 `/data/adb/ksu/bin/*` 的工具仍可工作。

启动阶段由上文注入的 `init.rc` 行驱动。每个阶段依次运行全局 `/data/adb/<stage>.d` 脚本、
metamodule 的阶段脚本，然后运行每个活动模块的 `<stage>.sh`。

---

## 4. 管理器 App

管理器是单模块 Gradle 工程（`manager/app`），使用 Jetpack Compose + Material 3，
`minSdk 31`，仅 arm64-v8a。它有两条进入系统的通道：

1. **JNI / ioctl**（`core/Ncore.kt` ↔ `cpp/libjni/jni.cc`）：使用 `prctl` 操作码与
   `[fmac_ctl]` 控制 fd 与内核模块通信。用于 Root 档案、能力集、SELinux 规则、特性开关、
   版本查询。
2. **调用 `ncore` 的 Root shell**（`data/shell/RootShell.kt`）：以 Root 运行打包的
   `/data/adb/nksu/ncore`，用于模块操作与 boot 镜像修补。

### Root 管理

`ui/screens/AppList.kt` / `AppDetailScreen.kt` 通过 `Ncore.listProfiles()` 读取内核档案，
并通过 `Ncore.setProfile(...)` / `Ncore.deluid(...)` 写入变更。一个档案包含：是否允许、
要模拟的 root UID、一组 Linux 能力、SELinux 域（默认 `u:r:nksu:s0`）以及命名空间模式
（`INHERITED=1`、`INDIVIDUAL=2`、`GLOBAL=3`）。

### 模块管理

`data/module/ModuleRepository.kt` 用 `ncore module list --json` 枚举模块，通过 HTTP 从每个
模块的 `updateJson` 检查更新，用系统 `DownloadManager` 下载 ZIP，并以
`ncore module install <zip>` 安装。

### WebUI

`ui/webui/` 承载兼容 KernelSU 的 WebUI。模块的 `webroot/` 通过单个 `WebView` 在虚拟域名
`mui.kernelsu.org` 上提供，文件以 Root 身份读取。桥接对象注册为全局 `ksu`，暴露
`window.ksu`：`exec`、`spawn`、`toast`、`moduleInfo`、`listPackages`、`getPackagesInfo`、
`fullScreen`、`exit`。管理器还像 KernelSU 一样提供 `internal/insets.css` 与
`internal/colors.css`（把 Material 3 配色导出为 CSS 变量）。

### 安装

`ui/screens/install/` + `data/installer/VendorBootInstaller.kt` 实现了三种与 KernelSU 对齐的
方式：

- **文件安装** —— 用 `ncore -p` 修补用户选择的 `vendor_boot` 镜像，并把结果导出到
  `Download/`。无需 Root。
- **直接安装** —— 用 Root helper dump 当前 `vendor_boot` 分区，修补后写回；失败时回滚。
  需要 Root + GKI。
- **非活动 slot** —— 与直接安装相同，但目标是另一个 A/B slot。需要 Root、GKI 且为 A/B。

---

## 5. 内核 ↔ 用户态协议

### 控制 fd 与 ioctl

管理器从内核获取控制文件描述符：

- `prctl(201)` → `[fmac_shm]`（匿名共享内存，用于路径/参数交换）；
- `prctl(203)` → `[fmac_ctl]`（ioctl 控制 fd）；
- `prctl(204)` → `[fmac_ctl]`，对任意 Root 进程可用（供 `ncore` 使用）；
- `prctl(205)` → 启动完成通知（停止内核安全模式观察器）。

唯一的 ioctl 是 `IOC_CMD = _IOWR('F', 0, struct fmac_ioc)`：

```c
struct fmac_ioc { unsigned int flag; unsigned int size; unsigned char data[]; };
```

载荷偏移与大小由 `static_assert` 固定，并在 `manager/app/src/main/cpp/libjni/ioctl.h` 中逐字节
镜像。标志包括 `IOC_GET_SHM`、`IOC_BIND_EVT`、`IOC_ADD_UID`/`IOC_DEL_UID`/`IOC_HAS_UID`、
`IOC_SET_CAP`/`IOC_GET_CAP`/`IOC_DEL_CAP`、`IOC_SET_PROFILE`、`IOC_GET_PROFILES`、
`IOC_GET_VERSION`、`IOC_SET_SEPOLICY`，以及特性的 list/get/set 命令。完整表格见
[内核模块开发者文档](kernel.zh.md#5-控制-fd-与-ioctl-abi)。

### SELinux 规则批次

`ncore` 解析 `sepolicy.rule` 文件（KernelSU 语法，支持 `{ }` 笛卡尔展开），编码为
`kernel/selinux/nksu_uapi_selinux.h` 定义的线上格式，再用 `IOC_SET_SEPOLICY` 发送。一个批次是
一串 `<cmd><subcmd>` 记录，每条后跟最多五个 `<长度><字节><NUL>` 参数，长度为零表示“通配”。
批次上限 8 MiB。

### Boot 镜像修补

Boot 镜像修补完全在用户态完成。`ncore` 自带 boot 镜像读写实现（不使用 `magiskboot`）：
检测 `boot`/`vendor_boot` 头部版本 v0–v4，按原始格式（gzip/bzip2/lzo/xz/lz4/lzma）解压并重新
压缩 ramdisk，并把 `nksu.ko` 安装进 v3/v4 `vendor_boot` ramdisk 的 `lib/modules/nksu.ko`，
同时注册 `modules.load` 与 `modules.dep`。

---

## 6. 版本耦合

根目录的单一 `version` 文件（`1.0.0`）是唯一事实来源：

- LKM 将其嵌入为 `NKSU_VERSION`，并通过 `IOC_GET_VERSION` 上报。
- 管理器将其嵌入为 `BuildConfig.NKSU_VERSION`，并在首页与运行中的 LKM 比对。不一致会显示为
  **NEED_UPDATE**，且管理器拒绝安装其自带的 `ncore`，以避免用户态/内核版本错配。

内核 git 提交（`NKSU_GIT_COMMIT`）与 App 的 `versionCode`/`versionName` 在构建时由 git 推导。

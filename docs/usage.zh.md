[English](usage.md) | **简体中文**

# 使用

NekoSU 是 Android 平台上的内核级 root 方案。本页说明如何安装、管理 root 权限与模块，以及如何抢救无法启动的设备。

::: warning 先备份你的设备
安装 NekoSU 会修改设备的启动链。请务必备份原始 `vendor_boot` 镜像，并了解如何用 fastboot 恢复。需要已解锁的 Bootloader。
:::

## 环境要求 {#requirements}

- Android 12 或更高（minSdk 31）。
- 仅支持 **arm64-v8a** 设备。
- Bootloader 已解锁——NekoSU 会被刷入 `vendor_boot`。
- **直接安装**与**非活动 slot 安装**额外需要：已有 root 方案（Magisk 或 KernelSU）且为 GKI 设备。

## 获取应用 {#getting-the-app}

从项目 GitHub 仓库的 **Actions → Android CI → artifacts（`release`）** 页面下载管理器 APK，安装到设备上。

首页会检查环境：

- 应用会显示内核模块是否已安装并运行。
- 若运行中的模块版本与应用期望的版本不一致，卡片会显示 **NEED_UPDATE**，并引导你更新模块。

## 安装方式 {#installation-methods}

打开应用，进入**安装**页面，选择以下之一。

### 文件安装 {#file-install}

无需 root。

1. 获取与设备当前固件/KMI 匹配的 `vendor_boot.img`（可从设备出厂镜像中提取，或从已 root 的方案中 dump）。
2. 在安装页面选择**文件安装**，选中该镜像。
3. NekoSU 用内置的 `nksu.ko` 修补镜像，并把结果保存到 **Download** 目录，文件名为 `nekosu_<id>_vendor_boot.img`。
4. 用 fastboot 刷入修补后的镜像：
   ```sh
   fastboot flash vendor_boot nekosu_<id>_vendor_boot.img
   ```
5. 重启。

应用会自动从内置的多个选项中挑选与设备 KMI 匹配的 `nksu.ko`。你也可以提供自己的 `.ko`。

### 直接安装 {#direct-install}

需要 root + GKI。

1. 选择**直接安装**。
2. NekoSU 会 dump 当前 `vendor_boot` 分区，修补后写回。
3. 重启。

若中途失败，NekoSU 会把分区回滚到原始镜像。不会导出文件。

### 非活动 slot {#inactive-slot}

需要 root + GKI + A/B。

与直接安装相同，但修补的是**另一个** A/B slot。何时重启进入更新后的系统由你决定（通过 fastboot/Bootloader 切换 slot）。适合在不影响当前运行 slot 的情况下更新模块。

::: warning
三种方式都会写入 `vendor_boot`；Bootloader 必须已解锁。
:::

## root 授权管理 {#root-access}

打开 **应用** 标签页，查看所有请求过 root 的应用。对每个应用你可以配置：

- **允许** —— 授予或撤销该 UID 的 root。
- **Root UID** —— 要模拟的用户（默认 `0`）。
- **能力（Capabilities）** —— 授予的 Linux 能力（默认：完整集合）。
- **SELinux 域** —— 提权后进程运行所在的域（默认 `u:r:nksu:s0`）。
- **命名空间** —— `INHERITED`（共享应用的命名空间）、`INDIVIDUAL` 或 `GLOBAL`（init/全局挂载命名空间）。

更改会通过内核档案存储立即生效。当被允许的应用运行 `su` 时，内核会在 shell 启动前按该档案完成提权——默认没有交互式弹窗；应用列表就是唯一依据。

## 模块 {#modules}

打开 **模块** 标签页。

- **安装模块** —— 点击安装按钮，选择模块 ZIP（Magisk 或 KernelSU 格式）。模块会被解压到 `/data/adb/modules/<id>`，并在下次启动时生效。
- **启用 / 禁用** —— 切换模块的 `disable` 标志。
- **移除** —— 标记模块待移除；下次启动时卸载（会运行其 `uninstall.sh`）。
- **更新** —— 若模块声明了 `updateJson`，应用会检查更新；否则用新 ZIP 覆盖安装。
- **动作** —— 运行模块的 `action.sh`（若存在）。

NekoSU 兼容 KernelSU 模块格式，因此现有 KernelSU 模块无需改动即可使用。模块可以挂载 `system/` 覆盖、提供 `initrc/*.rc`、`sepolicy.rule`、`system.prop`，以及用于 `post-fs-data`、`services`、`boot-completed` 阶段的脚本。

### Metamodule {#metamodules}

在 `module.prop` 中标记 `metamodule=1` 的模块会接管模块挂载（例如某种 Zygisk 实现）。它会被软链接到 `/data/adb/metamodule`，并获得 `metamount.sh`、`metainstall.sh`、`metauninstall.sh` 钩子。仅在你的方案确实需要时再安装 metamodule。

### WebUI {#webui}

提供 `webroot/` 目录的模块会有 WebUI。点击模块的 WebUI 按钮会在独立 Activity 中打开。WebUI 可使用兼容 KernelSU 的 `window.ksu` API（`exec`、`spawn`、`toast`、`moduleInfo`、`listPackages`、`getPackagesInfo`、`fullScreen`、`exit`），并通过 `internal/colors.css` 获取管理器的 Material 3 配色。

## 设置 {#settings}

- **语言** —— 跟随系统、英文或简体中文。
- **外观** —— 主题模式（跟随系统/浅色/深色）、主题配色、AMOLED 纯黑。
- **特性** —— 内核特性开关，包括 **SELinux 隐藏**（对应用隐藏 SELinux 状态）。
- **工具** —— 导出日志等实用功能。
- **关于** —— 版本信息与开源许可。

## 从 bootloop 中救砖 {#rescue}

如果某个模块导致设备无法启动，NekoSU 内置了恢复路径：

1. 启动时**连续按三次音量上键**（按下-松开、按下-松开、按下-松开）。
2. NekoSU 写入安全模式标记并重启。
3. 下次启动即进入安全模式，**所有模块被禁用**，设备得以启动。

你也可以通过系统属性 `persist.sys.safemode` 或 `ro.sys.safemode` 触发安全模式。启动后逐个重新启用模块以定位问题模块。

## 更新 NekoSU {#updating}

- 发布新的内核模块后，用[安装方式](#installation-methods)中的任一方式安装。首页卡片会把版本不一致标记为 **NEED_UPDATE**。
- 应用也可能更新其内置的用户态运行时；一旦运行中的内核模块版本匹配，它会自动完成。

## 故障排查 {#troubleshooting}

| 现象 | 可能原因 / 处理 |
| --- | --- |
| 应用显示 **NOT_INSTALLED** | 内核模块未加载。刷入修补后的 `vendor_boot` 并重启。 |
| 应用显示 **NEED_UPDATE** | 运行中的模块版本与应用不一致。重新构建/刷入匹配的 `nksu.ko`。 |
| 安装后设备反复重启 | 进入[安全模式](#rescue)（音量上 ×3）禁用模块，然后移除问题模块。 |
| 模块不显示 | 确认 ZIP 为 Magisk/KernelSU 格式，且安装后已重启设备。 |
| 某应用 `su` 无效 | 检查 **应用** 标签页：该 UID 必须为**允许**。 |
| SELinux 相关模块失败 | 检查 设置 → 特性 中的开关（例如 SELinux 隐藏）。 |
| 直接/非活动安装不可用 | 需要 root 与 GKI 设备；用 `uname -r` 检查设备并确认存在 root 方案。 |
| `nksu.ko` 与设备不匹配 | 将 KMI（`uname -r` 中的 `android<NN>-<major.minor>`）与内置的 `*_nksu.ko` 对应。 |

### 收集日志 {#collecting-logs}

使用 设置 → 工具 → 导出日志。应用会打包 logcat 输出以便诊断。报告问题时，请附上应用版本、运行中的内核模块版本，以及设备的 `uname -r`。

## 卸载 {#uninstalling}

内核模块没有应用内卸载功能。要移除 NekoSU：

1. 用 fastboot 刷回**原始** `vendor_boot` 镜像：
   ```sh
   fastboot flash vendor_boot <原始_vendor_boot.img>
   ```
2. 重启。
3. 可选：卸载管理器 APK 并删除 `/data/adb/nksu/`。

仅卸载应用会保留内核模块；仅移除模块会留下 `/data/adb/nksu/` 下的孤立文件。

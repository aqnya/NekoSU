[English](README.md) | **简体中文**

# NekoSU 文档 {#introduction}

NekoSU 是 Android 平台上的内核级 root 方案，由可加载内核模块（`nksu.ko`）与 Material 3 管理器 App 组成。它的模块系统、WebUI 与 SELinux 规则格式均兼容 KernelSU，因此 KernelSU 模块与 WebUI 无需改动即可使用。

## 文档 {#documentation}

| 页面 | 受众 | 你将了解到 |
| --- | --- | --- |
| [架构](architecture.zh.md) | 贡献者、评审者 | 内核模块、`ncore` 与管理器 App 如何协同，以及它们之间如何通信 |
| [如何构建](building.zh.md) | 开发者、打包者 | 构建 LKM、`ncore` 与管理器 App，以及 CI 如何产出产物 |
| [内核模块开发者指南](kernel.zh.md) | 内核开发者 | `kernel/` 的目录结构、启动流程、hook 层与控制 fd ABI |
| [ncore](ncore.zh.md) | 用户态开发者 | `ncore` 运行时：CLI、`su`、模块系统与 boot 镜像修补 |
| [使用](usage.zh.md) | 终端用户 | 安装 NekoSU、管理 root 与模块，以及抢救无法启动的设备 |

## 快速开始 {#quick-start}

1. 阅读 [环境要求](usage.zh.md#requirements)。
2. 从 **Actions → Android CI → artifacts（`release`）** 下载管理器 APK。
3. 按照 [安装指南](usage.zh.md#installation-methods) 操作。

::: tip
第一次接触内核级 root？先看 [架构](architecture.zh.md) 了解全貌；如果你只是想使用 NekoSU，直接看 [使用](usage.zh.md)。
:::

## 相关文档 {#related-documents}

- [`README.md`](../README.md) / [`README_zh.md`](README_zh.md) —— 项目概览。
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) / [`CONTRIBUTING.zh.md`](CONTRIBUTING.zh.md) —— 如何贡献。

## 术语约定 {#conventions}

- **LKM** —— 可加载内核模块；NekoSU 构建为 `nksu.ko`。
- **ncore** —— 用户态模块运行时。由 `userspace/ncore` 构建，作为 `libncore.so` 打包进 APK，并以**执行**方式运行（而非加载）。
- **KMI** —— 内核模块接口，写作 `android<NN>-<major.minor>`（例如 `android14-6.1`）。
- **管理器** —— Android 应用（`me.nekosu.aqnya`）。

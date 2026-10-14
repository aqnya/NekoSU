# NekoSU 文档

[English](README.md) | **简体中文**

本目录包含 NekoSU 的文档。请从下方选择主题。

| 文档 | 受众 | 内容 |
| --- | --- | --- |
| [架构与设计](architecture.zh.md) | 贡献者、评审者 | 三层结构（内核模块、`ncore`、管理器 App）的职责、数据流，以及内核 ↔ 用户态协议 |
| [构建与开发指南](building.zh.md) | 开发者、打包者 | 前置条件，构建 LKM / `ncore` / 管理器 App，版本流程，CI 工作流及其产物 |
| [内核模块开发者文档](kernel.zh.md) | 内核开发者 | `kernel/` 各目录参考、启动流程、hook 层、ioctl ABI、权限档案、SELinux 引擎、符号解析 |
| [ncore —— 用户态运行时](ncore.zh.md) | `ncore` 开发者 | CLI 参考、`su` 实现、模块生命周期与 KernelSU 兼容性、boot 镜像处理、内核接口 |
| [用户使用手册](usage.zh.md) | 终端用户 | 环境要求、安装方式、模块、WebUI、Root 授权、安全模式、故障排查 |

## 仓库中的相关文档

- [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) — 项目概览与快速开始。
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) / [`CONTRIBUTING.zh.md`](../CONTRIBUTING.zh.md) — 如何贡献。

## 术语约定

- **LKM** — 可加载内核模块；NekoSU 以 `nksu.ko` 形式提供。
- **ncore** — 仓库内置的用户态模块运行时（兼容 KernelSU/`ksud`）。由 `userspace/ncore` 构建，作为 `libncore.so` 打包进 APK，并以**执行**方式运行（而非链接）。
- **KMI** — 内核模块接口，写作 `android<NN>-<major.minor>`（例如 `android14-6.1`）。
- **管理器** — Android 应用（`me.nekosu.aqnya`）。

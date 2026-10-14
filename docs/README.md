**English** | [简体中文](README.zh.md)

# NekoSU Documentation

NekoSU is a kernel-level root solution for Android, shipped as a loadable kernel module (`nksu.ko`) plus a Material 3 manager app. Its module system, WebUI and SELinux rule wire format are all compatible with KernelSU, so KernelSU modules and WebUIs work without modification.

## Documentation

| Page | Audience | What you'll learn |
| --- | --- | --- |
| [Architecture](architecture.md) | Contributors, reviewers | How the kernel module, `ncore` and the manager app fit together, and how they talk to each other |
| [How to build](building.md) | Developers, packagers | Building the LKM, `ncore` and the manager app, and how CI produces the artifacts |
| [Kernel module developer guide](kernel.md) | Kernel developers | The layout of `kernel/`, the boot flow, the hook layer and the control-fd ABI |
| [ncore](ncore.md) | Userspace developers | The `ncore` runtime: CLI, `su`, the module system and boot-image patching |
| [Usage](usage.md) | End users | Installing NekoSU, managing root and modules, and rescuing a failed boot |

## Quick start

1. Read the [requirements](usage.md#requirements).
2. Download the manager APK from **Actions → Android CI → artifacts (`release`)**.
3. Follow the [installation guide](usage.md#installation-methods).

::: tip
New to kernel-based root? Start with [Architecture](architecture.md) for the big picture, then [Usage](usage.md) if you just want to use NekoSU.
:::

## Related documents

- [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) — project overview.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) / [`CONTRIBUTING.zh.md`](../CONTRIBUTING.zh.md) — how to contribute.

## Conventions

- **LKM** — loadable kernel module; NekoSU is built as `nksu.ko`.
- **ncore** — the userspace module runtime. It is built from `userspace/ncore`, shipped in the APK as `libncore.so`, and executed (not loaded).
- **KMI** — Kernel Module Interface, written as `android<NN>-<major.minor>` (e.g. `android14-6.1`).
- **Manager** — the Android app (`me.nekosu.aqnya`).

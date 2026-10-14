# NekoSU Documentation

**English** | [简体中文](README.zh.md)

This directory contains the NekoSU documentation. Pick a topic below.

| Document | Audience | Contents |
| --- | --- | --- |
| [Architecture & Design](architecture.md) | Contributors, reviewers | The three layers (kernel module, `ncore`, manager app), their responsibilities, data flow, and the kernel ↔ userspace protocol |
| [Build & Development Guide](building.md) | Developers, packagers | Prerequisites, building the LKM / `ncore` / manager app, the version flow, the CI workflows and their artifacts |
| [Kernel Module Developer Guide](kernel.md) | Kernel developers | Per-directory reference of `kernel/`, boot flow, hook layer, the ioctl ABI, privilege profiles, SELinux engine, symbol resolution |
| [ncore — Userspace Runtime](ncore.md) | `ncore` developers | CLI reference, the `su` implementation, module lifecycle and KernelSU compatibility, boot-image handling, kernel interfaces |
| [User Manual](usage.md) | End users | Requirements, installation methods, modules, WebUI, root management, safe mode, troubleshooting |

## Related documents in the repository

- [`README.md`](../README.md) / [`README_zh.md`](../README_zh.md) — project overview and quick start.
- [`CONTRIBUTING.md`](../CONTRIBUTING.md) / [`CONTRIBUTING.zh.md`](../CONTRIBUTING.zh.md) — how to contribute.

## Conventions

- **LKM** — loadable kernel module; NekoSU ships as `nksu.ko`.
- **ncore** — the in-repo userspace module runtime (KernelSU/`ksud`-compatible). It is built from `userspace/ncore`, shipped in the APK as `libncore.so`, and executed (not linked).
- **KMI** — Kernel Module Interface, written as `android<NN>-<major.minor>` (e.g. `android14-6.1`).
- **Manager** — the Android app (`me.nekosu.aqnya`).

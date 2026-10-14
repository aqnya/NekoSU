# 为 NekoSU 做贡献

[English](CONTRIBUTING.md) | **简体中文**

感谢你对 NekoSU 的关注。本指南说明如何搭建环境、进行修改并让改动被合并。

---

## 1. 项目结构

| 路径 | 说明 |
| --- | --- |
| `kernel/` | 可加载内核模块 `nksu.ko` |
| `userspace/ncore/` | 用户态运行时，使用 Bazel 构建 |
| `manager/` | Android 管理器应用（Gradle） |
| `docs/` | 文档 —— 见 [`docs/README.zh.md`](docs/README.zh.md) |
| `version` | 模块/应用版本的唯一事实来源 |
| `.github/workflows/` | CI |

在做非平凡改动之前，请先阅读[架构](docs/architecture.zh.md)文档。

---

## 2. 环境搭建

```sh
git clone --recurse-submodules https://github.com/nksu-crew/NekoSU.git
cd NekoSU
```

必须包含子模块：`userspace/ncore/third_party/` 内置了压缩库。如果你已经克隆但未带上子模块：

```sh
git submodule update --init --recursive
```

工具链：Bazel、NDK、JDK 与 Gradle 的准确版本见
[如何构建 § 前置条件](docs/building.zh.md#prerequisites)。

---

## 3. 进行修改

### 内核模块

- 遵循 `.clang-format`（Linux 内核风格，120 列，4 空格缩进）。
- 新增 ABI 应放入共享的 `uapi/` 头文件（`uapi/ioctl.h`、`uapi/selinux.h`），并用
  `static_assert` 固定。见 [内核模块开发者指南](docs/kernel.zh.md)。
- 本地构建：
  ```sh
  make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
  ```

### `ncore`

- C++17。保持 KernelSU 兼容性契约不变（`docs/ncore.zh.md`）。
- 构建：
  ```sh
  cd userspace/ncore
  ANDROID_NDK_HOME=/path/to/ndk ANDROID_NDK_API_LEVEL=28 bazel build //:ncore --config=android
  ```

### 管理器 App

- Kotlin、Jetpack Compose + Material 3。格式由 ktlint 强制。
- App 构建需要预先准备好的 native 产物（`*_nksu.ko` 资源与 `libncore.so`）；见
  [如何构建 § 构建管理器 App](docs/building.zh.md#building-the-manager-app)。

### 文档

- 文档为双语：每个文档都有英文文件与 `.zh.md` 对应文件，并通过语言切换器互相链接。修改其一
  时请同步更新另一个。
- 重命名标题时请同步更新 Markdown 锚点。

---

## 4. 版本递增

根目录的 `version` 文件（当前为 `1.0.0`）会嵌入内核模块与 App 两者。**每当修改内核 ABI 或
用户态/内核协议时都要递增它**，以便管理器能把过期模块标记为 `NEED_UPDATE`。编辑 `version`
也会触发两个 CI 工作流。

---

## 5. CI

| 工作流 | 触发时机 | 产物 |
| --- | --- | --- |
| `build-lkm.yml` | `kernel/**` 或 `version` 变更 | 按 KMI 矩阵扇出 |
| `ddk-lkm.yml` | 由 `build-lkm.yml` 调用 | `<kmi>-lkm` artifacts |
| `android.yml` | 管理器/ncore/version 变更，或 LKM 构建成功后 | `release` APK |

请确保你的改动能在与 CI 相同的工具链版本下构建。目前没有测试套件；`./gradlew assembleRelease`
与 LKM 构建是主要检查。

---

## 6. 提交与 Pull Request

- 保持提交聚焦；对内核 ABI 的改动与共享 `uapi/` 头文件应放在同一个提交里。
- 提交信息中说明**为什么**，而不只是做了什么。
- 向 `main` 发起 Pull Request。PR 必须通过 `Android CI`（它依赖 LKM 构建成功）。
- 若改动影响 ABI 或跨层协议，请在 PR 描述中明确说明，以便评审版本递增。

---

## 7. 依赖更新

`renovate.json` 为依赖更新启用了 automerge。请留意管理器
（`manager/gradle/libs.versions.toml`）与 CI actions 的机器人 PR。

---

## 8. 许可证

提交贡献即表示你同意你的贡献以 [GPL-3.0](LICENSE) 许可发布。

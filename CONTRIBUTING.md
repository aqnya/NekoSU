# Contributing to NekoSU

**English** | [简体中文](docs/CONTRIBUTING.zh.md)

Thanks for your interest in NekoSU. This guide covers how to set up, make changes, and get them
merged.

---

## 1. Project layout

| Path | What it is |
| --- | --- |
| `kernel/` | The loadable kernel module `nksu.ko` |
| `userspace/ncore/` | The userspace runtime, built with Bazel |
| `manager/` | The Android manager app (Gradle) |
| `docs/` | Documentation — see [`docs/README.md`](docs/README.md) |
| `version` | Single source of truth for the module/app version |
| `.github/workflows/` | CI |

Read the [Architecture](docs/architecture.md) document before making non-trivial changes.

---

## 2. Getting set up

```sh
git clone --recurse-submodules https://github.com/nksu-crew/NekoSU.git
cd NekoSU
```

Submodules are required: `userspace/ncore/third_party/` vendors the compression libraries.
If you already cloned without them:

```sh
git submodule update --init --recursive
```

Toolchain: see [How to build § Prerequisites](docs/building.md#prerequisites) for the exact
versions of Bazel, the NDK, JDK and Gradle used.

---

## 3. Making changes

### Kernel module

- Follow `.clang-format` (Linux kernel style, 120 columns, 4-space indent).
- New ABI additions belong in the shared `uapi/` headers (`uapi/ioctl.h`, `uapi/selinux.h`) and
  must be pinned with `static_assert`. See the [Kernel module developer guide](docs/kernel.md).
- Build locally with:
  ```sh
  make -C kernel CONFIG_NKSU=m CONFIG_NKSU_DEBUG=y
  ```

### `ncore`

- C++17. Keep the KernelSU compatibility contract intact (`docs/ncore.md`).
- Build with:
  ```sh
  cd userspace/ncore
  ANDROID_NDK_HOME=/path/to/ndk ANDROID_NDK_API_LEVEL=28 bazel build //:ncore --config=android
  ```

### Manager app

- Kotlin, Jetpack Compose + Material 3. Formatting is enforced by ktlint.
- The app build needs staged native artifacts (the `*_nksu.ko` assets and `libncore.so`); see
  [How to build § Build the manager app](docs/building.md#building-the-manager-app).

### Documentation

- Documentation is bilingual: every document has an English file and a `.zh.md` counterpart,
  cross-linked with a language switcher. Update both when you change one.
- Keep Markdown anchors in sync when renaming headings.

---

## 4. Version bumps

The root `version` file (currently `1.0.0`) is embedded into both the kernel module and the
app. **Bump it whenever you change the kernel ABI or the userspace/kernel protocol**, so the
manager can flag stale modules as `NEED_UPDATE`. Editing `version` also triggers both CI
workflows.

---

## 5. CI

| Workflow | Runs when | Produces |
| --- | --- | --- |
| `build-lkm.yml` | `kernel/**` or `version` changes | fans out over the KMI matrix |
| `ddk-lkm.yml` | called by `build-lkm.yml` | `<kmi>-lkm` artifacts |
| `android.yml` | manager/ncore/version changes, or after a successful LKM build | `release` APK |

Make sure your change builds under the same toolchain versions CI uses. There is currently no
test suite; `./gradlew assembleRelease` and the LKM build are the primary checks.

---

## 6. Commits and pull requests

- Keep commits focused; a change to the kernel ABI and the shared `uapi/` header belongs in one
  commit.
- Describe **why** in the commit message, not just what.
- Open a pull request against `main`. The PR must pass `Android CI` (which depends on a
  successful LKM build).
- If your change affects the ABI or cross-layer protocol, note it explicitly in the PR
  description so the version bump can be reviewed.

---

## 7. Dependency updates

`renovate.json` enables automerge for dependency updates. Review bot PRs for the manager
(`manager/gradle/libs.versions.toml`) and CI actions as they come in.

---

## 8. License

By contributing you agree that your contributions are licensed under [GPL-3.0](LICENSE).

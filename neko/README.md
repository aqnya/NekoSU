# neko

NekoSU's userspace runtime.

- `ncore/` — ported from [KernelSU](https://github.com/tiann/KernelSU) (GPL-3.0-or-later).
  The KernelSU kernel interface is replaced by a NekoSU adapter in `ncore/src/ksucalls.rs`.
- `jni/` — the JNI bridge the Compose manager loads as `libjni.so`. It also
  contains the v3/v4 `vendor_boot` patcher (`jni/src/patch/`), which installs
  the LKM in `lib/modules/nksu.ko`, updates `modules.load` / `modules.dep` and
  preserves the ramdisk compression and v4 vendor ramdisk table. Before patching
  it reads the `.nksu_ksymtab` symbol table embedded in the LKM and verifies the
  required kallsyms entries against the kernel in the supplied boot image.
- `bootparse` (Git dependency) — the boot/vendor_boot image library the patcher
  is built on: AOSP headers via bindgen, block extraction with compression
  detection, a newc `cpio` reader/writer and the vendor_boot repacker.

Both binaries are cross-compiled by Gradle through `cargo-ndk` as part of the
manager build and shipped as `libncore.so` and `libjni.so`.

Most Rust dependencies come from crates.io or are vendored under `third_party/`
(`prop-rs`/`prop-rs-android`). The patcher's `bootparse` dependency is pinned to
a Git revision in `Cargo.lock`.

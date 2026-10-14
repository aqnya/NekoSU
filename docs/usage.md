# User Manual

**English** | [简体中文](usage.zh.md)

NekoSU is a kernel-level root solution for Android. This manual explains how to install it,
manage modules and root access, and troubleshoot common problems.

> ⚠️ **Warning.** Installing NekoSU modifies your device's boot chain. Always keep a copy of
> your original `vendor_boot` image and know how to restore it with fastboot. An unlocked
> bootloader is required.

---

## 1. Requirements

- Android 12 or newer (minSdk 31).
- **arm64-v8a** devices only.
- Unlocked bootloader (NekoSU is flashed into `vendor_boot`).
- For **direct install** and **inactive-slot install**: an existing root solution
  (Magisk or KernelSU) and a GKI device.

---

## 2. Getting the app

Download the manager APK from the **Actions → Android CI → artifacts (`release`)** page of the
project's GitHub repository. Install it on your device.

Requirements are checked on the home screen:

- The app shows whether the kernel module is installed and running.
- If the running module's version does not match the app's expected version, the card shows
  **NEED_UPDATE** and directs you to update the module.

---

## 3. Installation methods

Open the app and go to the **Install** screen. Choose one of:

### File install (no root)

1. Obtain the `vendor_boot.img` matching your device's current firmware/KMI.
   (You can extract it from your device's factory image or dump it from a root solution.)
2. In the Install screen, choose **File** and select the image.
3. NekoSU patches the image with the bundled `nksu.ko` and saves the result to your
   **Download** folder as `nekosu_<id>_vendor_boot.img`.
4. Flash the patched image with fastboot:
   ```sh
   fastboot flash vendor_boot nekosu_<id>_vendor_boot.img
   ```
5. Reboot.

The correct `nksu.ko` for your device's KMI is selected automatically from the ones bundled in
the app. You can also supply your own `.ko`.

### Direct install (root + GKI)

1. Choose **Direct install**.
2. NekoSU dumps the current `vendor_boot` partition, patches it, and writes it back.
3. Reboot.

If anything fails, NekoSU rolls the partition back to the original image. No file is exported.

### Inactive slot (root + GKI + A/B)

Same as direct install, but the **other** A/B slot is patched. Rebooting into the updated
system is under your control (switch slots via fastboot/your bootloader). Useful for updating
the module without touching the currently running slot.

> All three methods write `vendor_boot`; the bootloader must be unlocked.

---

## 4. Root access management

Open the **Apps** tab to see every app that has requested root.

For each app you can configure:

- **Allowed** — grant or revoke root for that UID.
- **Root UID** — which user to impersonate (default `0`).
- **Capabilities** — the Linux capabilities granted (default: full set).
- **SELinux domain** — the domain the escalated process runs in (default `u:r:nksu:s0`).
- **Namespace** — `INHERITED` (share the app's namespaces), `INDIVIDUAL`, or `GLOBAL`
  (the init/global mount namespace).

Changes are applied immediately through the kernel profile store. When an allowed app runs
`su`, the kernel escalates it according to this profile before the shell starts — there is no
interactive prompt by default; the app list is the source of truth.

---

## 5. Modules

Open the **Modules** tab.

- **Install a module** — tap the install button and pick a module ZIP (Magisk- or
  KernelSU-format). The module is extracted to `/data/adb/modules/<id>` and activated on the
  next boot.
- **Enable / disable** — toggles the module's `disable` flag.
- **Remove** — marks the module for removal; it is uninstalled on the next boot (its
  `uninstall.sh` runs).
- **Update** — if the module declares an `updateJson`, the app checks for updates; otherwise
  install a new ZIP over the old one.
- **Action** — runs the module's `action.sh`, if present.

NekoSU is compatible with the KernelSU module format, so existing KernelSU modules work
unchanged. Modules can mount `system/` overlays, ship `initrc/*.rc`, `sepolicy.rule`,
`system.prop`, and scripts for the `post-fs-data`, `services`, and `boot-completed` stages.

### Metamodules

A module marked `metamodule=1` in its `module.prop` takes over module mounting (e.g. a Zygisk
implementation). It is symlinked at `/data/adb/metamodule` and gets `metamount.sh`,
`metainstall.sh`, and `metauninstall.sh` hooks. Install a metamodule only if you know your
setup needs one.

### WebUI

Modules that provide a `webroot/` directory get a WebUI. Tapping the module's WebUI button
opens it in an isolated Activity. The WebUI sees the KernelSU-compatible `window.ksu` API
(`exec`, `spawn`, `toast`, `moduleInfo`, `listPackages`, `getPackagesInfo`, `fullScreen`,
`exit`) and the manager's Material 3 colors via `internal/colors.css`.

---

## 6. Settings

- **Language** — system default, English, or Simplified Chinese.
- **Appearance** — theme mode (system/light/dark), color theme, AMOLED black.
- **Features** — kernel feature toggles, including **SELinux hide** (hide SELinux state from
  apps).
- **Tools** — export logs, and other utilities.
- **About** — version information, open-source licenses.

---

## 7. Safe mode

If a module prevents your device from booting, NekoSU has a built-in recovery path:

1. During boot, **press the Volume Up key three times**.
2. NekoSU writes a safe-mode marker and reboots.
3. On the next boot, safe mode is active and **all modules are disabled**, so the device boots.

You can also trigger safe mode from the system property `persist.sys.safemode` or
`ro.sys.safemode`. Once booted, re-enable modules one at a time to find the culprit.

---

## 8. Updating NekoSU

- When a new kernel module is released, install it with one of the methods in
  [§3](#3-installation-methods). The app's home card flags a version mismatch as
  **NEED_UPDATE**.
- The app may also update its bundled userspace runtime; it does so automatically once the
  running kernel module version matches.

---

## 9. Troubleshooting

| Symptom | Likely cause / fix |
| --- | --- |
| App shows **NOT_INSTALLED** | The kernel module is not loaded. Flash the patched `vendor_boot` and reboot. |
| App shows **NEED_UPDATE** | The running module version differs from the app's. Rebuild/reflash the matching `nksu.ko`. |
| Device bootloops after installing | Enter [safe mode](#7-safe-mode) (Volume Up ×3) to disable modules, then remove the offending module. |
| A module does not appear | Ensure the ZIP is Magisk/KernelSU-format and the device has rebooted after install. |
| `su` does nothing for an app | Check the **Apps** tab: the UID must be **Allowed**. |
| SELinux-related module failures | Check the Settings → Features toggles (e.g. SELinux hide). |
| Direct / inactive install disabled | Requires root and a GKI device; check the device with `uname -r` and confirm a root solution is present. |
| Wrong `nksu.ko` for the device | Match the KMI (`android<NN>-<major.minor>` from `uname -r`) to a bundled `*_nksu.ko`. |

### Collecting logs

Use Settings → Tools → export logs. The app bundles logcat output for diagnosis. When reporting
an issue, include the app version, the running kernel module version, and the device's
`uname -r`.

---

## 10. Uninstalling

There is no in-app uninstaller for the kernel module. To remove NekoSU:

1. Flash your **original** `vendor_boot` image back with fastboot:
   ```sh
   fastboot flash vendor_boot <original_vendor_boot.img>
   ```
2. Reboot.
3. Optionally uninstall the manager APK and delete `/data/adb/nksu/`.

Removing only the app leaves the kernel module active; removing only the module leaves orphaned
files under `/data/adb/nksu/`.

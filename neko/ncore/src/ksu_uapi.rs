// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * NekoSU kernel interface constants.
 *
 * Upstream ksud generates these bindings from KernelSU's uapi/ksu.h.  NekoSU
 * exposes a different (multiplexed ioctl) interface, so this module only keeps
 * the small set of names and layouts that the rest of ncore uses.  The actual
 * ioctl encoding lives in ksucalls.rs.
 *
 * SELinux command/subcommand ids come from the bindgen-generated `uapi`
 * crate (NKSU_* names); the KSU_* aliases below are kept so existing
 * ncore call sites don't have to change.
 */
#![allow(nonstandard_style)]
use uapi::*;

pub const KERNEL_SU_UAPI_VERSION: u32 = 5;

pub const KSU_GET_INFO_FLAG_LKM: u32 = 1 << 0;
pub const KSU_GET_INFO_FLAG_LATE_LOAD: u32 = 1 << 1;
pub const KSU_GET_INFO_FLAG_PR_BUILD: u32 = 1 << 2;
pub const KSU_GET_INFO_FLAG_BUNDLED: u32 = 1 << 3;
pub const KSU_GET_INFO_FLAG_PREINIT: u32 = 1 << 4;

pub const KSU_FEATURE_FLAG_SUPPORTED: u32 = 1 << 0;
pub const KSU_FEATURE_FLAG_FORCED: u32 = 1 << 1;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ksu_get_info_cmd {
    pub version: u32,
    pub flags: u32,
    pub features: u32,
    pub uapi_version: u32,
}

pub const EVENT_POST_FS_DATA: u32 = 1;
pub const EVENT_BOOT_COMPLETED: u32 = 2;
pub const EVENT_MODULE_MOUNTED: u32 = 3;
pub const EVENT_SERVICES: u32 = 4;

// ---- SELinux batch command ids ----
// Values come from uapi/ioctl.h via the `uapi` crate (bindgen).  The KSU_*
// aliases keep ncore call sites stable; the NKSU_* names are the canonical
// ones (same prefix as the kernel header).
pub const KSU_SEPOLICY_CMD_NORMAL_PERM: u32 = NKSU_SEPOLICY_CMD_NORMAL_PERM;
pub const KSU_SEPOLICY_CMD_XPERM: u32 = NKSU_SEPOLICY_CMD_XPERM;
pub const KSU_SEPOLICY_CMD_TYPE_STATE: u32 = NKSU_SEPOLICY_CMD_TYPE_STATE;
pub const KSU_SEPOLICY_CMD_TYPE: u32 = NKSU_SEPOLICY_CMD_TYPE;
pub const KSU_SEPOLICY_CMD_TYPE_ATTR: u32 = NKSU_SEPOLICY_CMD_TYPE_ATTR;
pub const KSU_SEPOLICY_CMD_ATTR: u32 = NKSU_SEPOLICY_CMD_ATTR;
pub const KSU_SEPOLICY_CMD_TYPE_TRANSITION: u32 = NKSU_SEPOLICY_CMD_TYPE_TRANSITION;
pub const KSU_SEPOLICY_CMD_TYPE_CHANGE: u32 = NKSU_SEPOLICY_CMD_TYPE_CHANGE;
pub const KSU_SEPOLICY_CMD_GENFSCON: u32 = NKSU_SEPOLICY_CMD_GENFSCON;

pub const KSU_SEPOLICY_SUBCMD_NORMAL_PERM_ALLOW: u32 = NKSU_SEPOLICY_SUBCMD_NORMAL_PERM_ALLOW;
pub const KSU_SEPOLICY_SUBCMD_NORMAL_PERM_DENY: u32 = NKSU_SEPOLICY_SUBCMD_NORMAL_PERM_DENY;
pub const KSU_SEPOLICY_SUBCMD_NORMAL_PERM_AUDITALLOW: u32 =
    NKSU_SEPOLICY_SUBCMD_NORMAL_PERM_AUDITALLOW;
pub const KSU_SEPOLICY_SUBCMD_NORMAL_PERM_DONTAUDIT: u32 =
    NKSU_SEPOLICY_SUBCMD_NORMAL_PERM_DONTAUDIT;

pub const KSU_SEPOLICY_SUBCMD_XPERM_ALLOW: u32 = NKSU_SEPOLICY_SUBCMD_XPERM_ALLOW;
pub const KSU_SEPOLICY_SUBCMD_XPERM_AUDITALLOW: u32 = NKSU_SEPOLICY_SUBCMD_XPERM_AUDITALLOW;
pub const KSU_SEPOLICY_SUBCMD_XPERM_DONTAUDIT: u32 = NKSU_SEPOLICY_SUBCMD_XPERM_DONTAUDIT;

pub const KSU_SEPOLICY_SUBCMD_TYPE_STATE_PERMISSIVE: u32 =
    NKSU_SEPOLICY_SUBCMD_TYPE_STATE_PERMISSIVE;
pub const KSU_SEPOLICY_SUBCMD_TYPE_STATE_ENFORCE: u32 = NKSU_SEPOLICY_SUBCMD_TYPE_STATE_ENFORCE;

pub const KSU_SEPOLICY_SUBCMD_TYPE_CHANGE_CHANGE: u32 = NKSU_SEPOLICY_SUBCMD_TYPE_CHANGE_CHANGE;
pub const KSU_SEPOLICY_SUBCMD_TYPE_CHANGE_MEMBER: u32 = NKSU_SEPOLICY_SUBCMD_TYPE_CHANGE_MEMBER;

pub const KSU_SEPOLICY_MAX_BATCH_SIZE: u32 = NKSU_SEPOLICY_MAX_BATCH_SIZE;
pub const KSU_SEPOLICY_MAX_ARGS: u32 = NKSU_SEPOLICY_MAX_ARGS;

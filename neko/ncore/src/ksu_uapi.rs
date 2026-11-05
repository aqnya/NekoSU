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

pub const KERNEL_SU_UAPI_VERSION: u32 = 5;

pub const KSU_GET_INFO_FLAG_LKM: u32 = 1 << 0;
pub const KSU_GET_INFO_FLAG_LATE_LOAD: u32 = 1 << 1;
pub const KSU_GET_INFO_FLAG_PREINIT: u32 = 1 << 4;

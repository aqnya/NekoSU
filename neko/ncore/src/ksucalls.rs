// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * NekoSU userspace kernel interface.
 *
 * This is the NekoSU port of upstream ksud's ksucalls.rs.  NekoSU does not
 * expose KernelSU's per-feature ioctls; instead one multiplexed ioctl
 * (IOC_CMD) takes a struct fmac_ioc { flag, size, data[] }.  Functions that
 * have no NekoSU equivalent degrade to successful no-ops so the module runtime
 * keeps working.
 */
use crate::ksu_uapi;
use anyhow::bail;

pub fn get_info() -> kzcall::defs::ksu_get_info_cmd {
    kzcall::get_info()
}

pub fn get_version() -> i32 {
    get_info().version as i32
}

pub fn is_late_load() -> bool {
    get_info().flags & ksu_uapi::KSU_GET_INFO_FLAG_LATE_LOAD != 0
}

pub fn is_lkm() -> bool {
    get_info().flags & ksu_uapi::KSU_GET_INFO_FLAG_LKM != 0
}

pub fn is_preinit_supported() -> bool {
    get_info().flags & ksu_uapi::KSU_GET_INFO_FLAG_PREINIT != 0
}

pub fn runtime_mode() -> &'static str {
    if is_late_load() {
        "late-load"
    } else if is_lkm() {
        "lkm"
    } else {
        "built-in"
    }
}

pub fn ensure_uapi_version_matched() -> anyhow::Result<()> {
    let kernel_uapi = get_info().uapi_version;
    let userspace_uapi = ksu_uapi::KERNEL_SU_UAPI_VERSION;
    if kernel_uapi != userspace_uapi {
        bail!(
            "UAPI version mismatch: kernel={kernel_uapi}, ncore={userspace_uapi}. Please update NekoSU!"
        );
    }
    Ok(())
}

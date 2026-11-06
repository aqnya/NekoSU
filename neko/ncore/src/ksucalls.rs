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
use crate::utils;

pub fn runtime_mode() -> &'static str {
    match utils::find_module("nksu") {
        Ok(Some(_)) => "lkm",
        Ok(None) => "built-in",
        Err(_) => "unknown",
    }
}

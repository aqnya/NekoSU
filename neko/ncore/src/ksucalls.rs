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
use anyhow::{Result, bail};
use std::cell::Cell;
use std::fs;
use std::io;
use std::os::fd::RawFd;
use std::sync::OnceLock;

use crate::ksu_uapi;

const FMAC_CTL_NAME: &str = "fmac_ctl";

// _IOWR('F', 0, struct fmac_ioc) with a two-u32 header.
const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;

const IOC_SET_SEPOLICY: u32 = 12;
const IOC_GET_VERSION: u32 = 14;
const IOC_GET_SAFEMODE: u32 = 18;
const IOC_DISARM_SAFEMODE: u32 = 19;
const IOC_SET_INIT_PGRP: u32 = 20;
const IOC_SET_MNT_LAYOUT: u32 = 23;

std::thread_local! {
    #[allow(clippy::missing_const_for_thread_local)]
    static SVC_IN_FLIGHT: Cell<bool> = const { Cell::new(false) };
    #[allow(clippy::missing_const_for_thread_local)]
    static SIGSYS_OCCURRED: Cell<bool> = const { Cell::new(false) };
}

pub fn setup_sigsys_handler() {
    // NekoSU attaches the control fd from the kernel exec tracepoint; there is
    // no prctl path left to guard.  Keep the symbol for upstream callers.
    let _ = (&SVC_IN_FLIGHT, &SIGSYS_OCCURRED);
}

static DRIVER_FD: OnceLock<RawFd> = OnceLock::new();
static INFO_CACHE: OnceLock<ksu_uapi::ksu_get_info_cmd> = OnceLock::new();

fn scan_driver_fd() -> io::Result<Option<RawFd>> {
    for entry in fs::read_dir("/proc/self/fd")?.flatten() {
        let Ok(fd_num) = entry.file_name().to_string_lossy().parse::<i32>() else {
            continue;
        };
        let link = format!("/proc/self/fd/{fd_num}");
        if let Ok(target) = fs::read_link(link)
            && target.to_string_lossy().contains(FMAC_CTL_NAME)
        {
            return Ok(Some(fd_num));
        }
    }
    Ok(None)
}

fn init_driver_fd() -> Option<RawFd> {
    if let Ok(Some(fd)) = scan_driver_fd() {
        return Some(fd);
    }

    // The kernel attaches the control fd via task_work when this ncore binary
    // (or libncore.so) is exec'd, so it is already in /proc/self/fd here.
    scan_driver_fd().ok().flatten()
}

pub fn claim_inherited_driver_fd() -> io::Result<()> {
    if DRIVER_FD.get().is_none()
        && let Some(fd) = scan_driver_fd()?
    {
        let _ = DRIVER_FD.set(fd);
    }
    Ok(())
}

fn driver_fd() -> Result<RawFd> {
    let fd = *DRIVER_FD.get_or_init(|| init_driver_fd().unwrap_or(-1));
    if fd < 0 {
        bail!("could not retrieve the NekoSU control fd");
    }
    Ok(fd)
}

fn ksuctl(flag: u32, data: &mut [u8]) -> Result<i32> {
    let fd = driver_fd()?;
    let mut buffer = Vec::with_capacity(8 + data.len());
    buffer.extend_from_slice(&flag.to_ne_bytes());
    buffer.extend_from_slice(&(data.len() as u32).to_ne_bytes());
    buffer.extend_from_slice(data);

    let ret = unsafe { libc::ioctl(fd, IOC_CMD, buffer.as_mut_ptr()) };
    if ret < 0 {
        bail!("NekoSU ioctl {flag} failed: {}", io::Error::last_os_error());
    }

    // Copy back the flag payload; callers pass a buffer big enough for it.
    data.copy_from_slice(&buffer[8..]);
    Ok(ret)
}

pub fn get_info() -> ksu_uapi::ksu_get_info_cmd {
    *INFO_CACHE.get_or_init(|| {
        let mut info = ksu_uapi::ksu_get_info_cmd {
            version: 30000,
            flags: ksu_uapi::KSU_GET_INFO_FLAG_LKM,
            features: 0,
            uapi_version: ksu_uapi::KERNEL_SU_UAPI_VERSION,
        };

        // The version ioctl is the authoritative liveness probe.
        let mut version = [0u8; 64];
        if ksuctl(IOC_GET_VERSION, &mut version).is_err() {
            info.flags = 0;
        }
        info
    })
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

pub const fn uapi_version() -> u32 {
    ksu_uapi::KERNEL_SU_UAPI_VERSION
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
    let userspace_uapi = uapi_version();
    if kernel_uapi != userspace_uapi {
        bail!(
            "UAPI version mismatch: kernel={kernel_uapi}, ncore={userspace_uapi}. Please update NekoSU!"
        );
    }
    Ok(())
}

pub fn report_post_fs_data() {}

pub fn report_services() -> Result<bool> {
    Ok(true)
}

pub fn report_boot_complete() {
    // Safe-mode is latched by the kernel until the daemon has read it; the
    // last boot stage releases the watcher.
    let _ = ksuctl(IOC_DISARM_SAFEMODE, &mut []);
}

pub fn check_kernel_safemode() -> bool {
    let mut data = [];
    matches!(ksuctl(IOC_GET_SAFEMODE, &mut data), Ok(1))
}

pub fn set_sepolicy(payload: *const u8, payload_len: u64) -> Result<i32> {
    if payload.is_null() {
        return Ok(0);
    }
    let bytes = unsafe { std::slice::from_raw_parts(payload, payload_len as usize) };
    let mut data = bytes.to_vec();
    ksuctl(IOC_SET_SEPOLICY, &mut data)
}

pub fn set_init_pgrp() -> Result<()> {
    let mut data = [];
    ksuctl(IOC_SET_INIT_PGRP, &mut data)?;
    Ok(())
}

pub fn set_mount_layout(mnt_id_off: u32, mnt_off: u32) -> Result<()> {
    let mut data = Vec::with_capacity(8);
    data.extend_from_slice(&mnt_id_off.to_ne_bytes());
    data.extend_from_slice(&mnt_off.to_ne_bytes());
    ksuctl(IOC_SET_MNT_LAYOUT, &mut data)?;
    Ok(())
}

pub fn set_no_new_privs() -> anyhow::Result<()> {
    let ret = unsafe { libc::prctl(libc::PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) };
    if ret != 0 {
        return Err(io::Error::last_os_error().into());
    }
    Ok(())
}

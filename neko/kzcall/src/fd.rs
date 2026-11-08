use anyhow::{Context, Result, bail};
use std::fs;
use std::os::fd::RawFd;
use std::sync::OnceLock;

use crate::defs::*;

static DRIVER_FD: OnceLock<RawFd> = OnceLock::new();

fn scan_driver_fd() -> Result<Option<RawFd>> {
    for entry in fs::read_dir("/proc/self/fd")
        .context("open /proc/self/fd")?
        .flatten()
    {
        let Ok(fd_num) = entry.file_name().to_string_lossy().parse::<i32>() else {
            continue;
        };
        let link = format!("/proc/self/fd/{fd_num}");
        if let Ok(target) = fs::read_link(link)
            && target.to_string_lossy().ends_with(FMAC_CTL_NAME)
        {
            return Ok(Some(fd_num));
        }
    }
    Ok(None)
}

/// Return the inherited NekoSU control fd.
///
/// A failed scan is not cached, so a caller may retry after the kernel has
/// attached the fd to this process.
pub fn driver_fd() -> Result<RawFd> {
    if let Some(fd) = DRIVER_FD.get() {
        return Ok(*fd);
    }

    let Some(fd) = scan_driver_fd()? else {
        bail!("could not retrieve the NekoSU control fd");
    };

    // Another thread may win the race; either cached fd is valid.
    let _ = DRIVER_FD.set(fd);
    Ok(*DRIVER_FD.get().unwrap_or(&fd))
}

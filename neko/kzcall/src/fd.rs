use anyhow::{Result, bail};
use std::fs;
use std::io;
use std::os::fd::RawFd;
use std::sync::OnceLock;
static DRIVER_FD: OnceLock<RawFd> = OnceLock::new();
use crate::defs::*;

fn init_driver_fd() -> Option<RawFd> {
    if let Ok(Some(fd)) = scan_driver_fd() {
        return Some(fd);
    } else {
        return None;
    }
}

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

pub fn driver_fd() -> Result<RawFd> {
    let fd = *DRIVER_FD.get_or_init(|| init_driver_fd().unwrap_or(-1));
    if fd < 0 {
        bail!("could not retrieve the NekoSU control fd");
    }
    Ok(fd)
}

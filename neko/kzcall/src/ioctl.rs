use crate::defs::*;
use crate::fd;
use anyhow::{Result, bail};
use std::io;

pub fn call(flag: u32, data: &mut [u8]) -> Result<i32> {
    let fd = fd::driver_fd()?;
    let mut buffer = Vec::with_capacity(8 + data.len());
    buffer.extend_from_slice(&flag.to_ne_bytes());
    buffer.extend_from_slice(&(data.len() as u32).to_ne_bytes());
    buffer.extend_from_slice(data);

    let ret = unsafe { libc::ioctl(fd, IOC_CMD, buffer.as_mut_ptr()) };
    if ret < 0 {
        bail!("NekoSU ioctl {flag} failed: {}", io::Error::last_os_error());
    }

    data.copy_from_slice(&buffer[8..]);
    Ok(ret)
}

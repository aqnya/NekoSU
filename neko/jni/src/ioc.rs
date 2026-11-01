// SPDX-License-Identifier: GPL-3.0-or-later
use std::fs;
use std::os::fd::RawFd;
use std::sync::atomic::{AtomicI32, Ordering};

use uapi::*;

const FMAC_MAX_DATA: usize = 64 * 1024;
const FMAC_STACK_DATA: usize = 264;

const HDR_SIZE: usize = std::mem::size_of::<fmac_ioc>();

const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;

const _: () = {
    let size = core::mem::size_of::<fmac_ioc>() as u32;
    let expected = IOC_INOUT | ((size & 0x1fff) << 16) | ((IOC_MAGIC as u32) << 8) | IOC_CMD_NR;
    assert!(IOC_CMD == expected as libc::c_int);
};

const PR_GET_SHM_FD: libc::c_int = 201;
const PR_GET_CTL_FD: libc::c_int = 203;

static DRIVER_FD: AtomicI32 = AtomicI32::new(-1);
static CTL_FD: AtomicI32 = AtomicI32::new(-1);

fn scan_fd(target: &str) -> RawFd {
    let Ok(entries) = fs::read_dir("/proc/self/fd") else {
        return -1;
    };
    for entry in entries.flatten() {
        let Ok(fd) = entry.file_name().to_string_lossy().parse::<i32>() else {
            continue;
        };
        let link = format!("/proc/self/fd/{fd}");
        if let Ok(path) = fs::read_link(link)
            && path.to_string_lossy().contains(target)
        {
            return fd;
        }
    }
    -1
}

pub fn get_ctl_fd() -> Option<CtlFd> {
    let cached = CTL_FD.load(Ordering::Relaxed);
    if cached >= 0 {
        return Some(CtlFd(cached));
    }
    unsafe {
        libc::prctl(PR_GET_CTL_FD, 0, 0, 0, 0);
    }
    let fd = scan_fd("[fmac_ctl]");
    if fd >= 0 {
        CTL_FD.store(fd, Ordering::Relaxed);
        Some(CtlFd(fd))
    } else {
        log::error!("control fd not found after prctl({PR_GET_CTL_FD})");
        None
    }
}

#[derive(Clone, Copy)]
pub struct CtlFd(RawFd);

impl CtlFd {
    pub fn raw(self) -> RawFd {
        self.0
    }

    fn call(self, flag: fmac_flag, data: &mut [u8]) -> Result<(), ()> {
        if data.len() > FMAC_MAX_DATA {
            return Err(());
        }
        let total = HDR_SIZE + data.len();

        let mut stack = [0u8; HDR_SIZE + FMAC_STACK_DATA];
        let mut heap = Vec::new();
        let buf: &mut [u8] = if total <= stack.len() {
            &mut stack[..total]
        } else {
            heap.resize(total, 0);
            &mut heap
        };

        {
            let hdr = unsafe { &mut *(buf.as_mut_ptr() as *mut fmac_ioc) };
            hdr.flag = flag as __u32;
            hdr.size = data.len() as __u32;
        }
        buf[HDR_SIZE..].copy_from_slice(data);

        let ret = unsafe { libc::ioctl(self.0, IOC_CMD, buf.as_mut_ptr()) };
        if ret < 0 {
            log::error!(
                "ioctl flag {} failed: {}",
                flag,
                std::io::Error::last_os_error()
            );
            return Err(());
        }
        if !data.is_empty() {
            data.copy_from_slice(&buf[HDR_SIZE..]);
        }
        Ok(())
    }

    pub fn set_profile(
        self,
        uid: i32,
        root_uid: i32,
        caps: u64,
        domain: &str,
        ns: i32,
    ) -> Result<(), ()> {
        let mut data = [0u8; FMAC_DATA_PROFILE as usize];
        put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
        put_u64(&mut data, FMAC_OFF_CAPS as usize, caps);
        put_cstr(
            &mut data[FMAC_OFF_DOMAIN as usize..FMAC_OFF_NS as usize],
            domain,
        );
        put_u32(&mut data, FMAC_OFF_NS as usize, ns as u32);
        put_u32(&mut data, FMAC_OFF_ROOT_UID as usize, root_uid as u32);
        self.call(fmac_flag_IOC_SET_PROFILE, &mut data)
    }

    pub fn add_uid(self, uid: i32) -> Result<(), ()> {
        if uid < 0 {
            return Err(());
        }
        let mut data = (uid as u32).to_ne_bytes();
        self.call(fmac_flag_IOC_ADD_UID, &mut data)
    }

    pub fn del_uid(self, uid: i32) -> Result<(), ()> {
        if uid < 0 {
            return Err(());
        }
        let mut data = (uid as u32).to_ne_bytes();
        self.call(fmac_flag_IOC_DEL_UID, &mut data)
    }

    pub fn has_uid(self, uid: i32) -> Option<bool> {
        if uid < 0 {
            return None;
        }
        let mut data = (uid as u32).to_ne_bytes();
        self.call(fmac_flag_IOC_HAS_UID, &mut data).ok()?;
        Some(u32::from_ne_bytes(data) != 0)
    }

    pub fn set_cap(self, uid: i32, caps: u64) -> Result<(), ()> {
        if uid < 0 {
            return Err(());
        }
        let mut data = [0u8; FMAC_DATA_CAP as usize];
        put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
        put_u64(&mut data, FMAC_OFF_CAPS as usize, caps);
        self.call(fmac_flag_IOC_SET_CAP, &mut data)
    }

    pub fn get_cap(self, uid: i32) -> Option<u64> {
        if uid < 0 {
            return None;
        }
        let mut data = [0u8; FMAC_DATA_CAP as usize];
        put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
        self.call(fmac_flag_IOC_GET_CAP, &mut data).ok()?;
        Some(get_u64(&data, FMAC_OFF_CAPS as usize))
    }

    pub fn del_cap(self, uid: i32) -> Result<(), ()> {
        if uid < 0 {
            return Err(());
        }
        let mut data = [0u8; FMAC_DATA_CAP as usize];
        put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
        self.call(fmac_flag_IOC_DEL_CAP, &mut data)
    }

    pub fn add_selinux_rule(
        self,
        src: &str,
        tgt: &str,
        cls: &str,
        perm: &str,
        effect: i32,
        invert: bool,
    ) -> Result<(), ()> {
        let mut data = [0u8; FMAC_DATA_SELRULE as usize];
        put_cstr(&mut data[FMAC_OFF_UID as usize..FMAC_OFF_TGT as usize], src);
        put_cstr(&mut data[FMAC_OFF_TGT as usize..FMAC_OFF_CLS as usize], tgt);
        put_cstr(
            &mut data[FMAC_OFF_CLS as usize..FMAC_OFF_PERM as usize],
            cls,
        );
        put_cstr(
            &mut data[FMAC_OFF_PERM as usize..FMAC_OFF_EFFECT as usize],
            perm,
        );
        put_u32(&mut data, FMAC_OFF_EFFECT as usize, effect as u32);
        put_u32(&mut data, FMAC_OFF_INVERT as usize, invert as u32);
        self.call(fmac_flag_IOC_SEL_ADD_RULE, &mut data)
    }

    pub fn list_profiles(self) -> Option<String> {
        let mut data = vec![0u8; 64 * 1024];
        self.call(fmac_flag_IOC_GET_PROFILES, &mut data).ok()?;
        Some(cstr_lossy(&data))
    }

    pub fn module_version(self) -> Option<String> {
        let mut data = [0u8; 64];
        self.call(fmac_flag_IOC_GET_VERSION, &mut data).ok()?;
        Some(cstr_lossy(&data))
    }

    pub fn feature_list(self) -> Option<String> {
        let mut data = [0u8; 1024];
        self.call(fmac_flag_IOC_FEATURE_LIST, &mut data).ok()?;
        Some(cstr_lossy(&data))
    }

    pub fn feature_get(self, id: i32) -> Option<u64> {
        let mut data = [0u8; FMAC_DATA_FEATURE as usize];
        put_u32(&mut data, 0, id as u32);
        self.call(fmac_flag_IOC_FEATURE_GET, &mut data).ok()?;
        Some(get_u64(&data, FMAC_OFF_FEATURE_VALUE as usize))
    }

    pub fn feature_set(self, id: i32, value: u64) -> Result<(), ()> {
        let mut data = [0u8; FMAC_DATA_FEATURE as usize];
        put_u32(&mut data, 0, id as u32);
        put_u64(&mut data, FMAC_OFF_FEATURE_VALUE as usize, value);
        self.call(fmac_flag_IOC_FEATURE_SET, &mut data)
    }
}

fn put_u32(buf: &mut [u8], off: usize, v: u32) {
    buf[off..off + 4].copy_from_slice(&v.to_ne_bytes());
}

fn put_u64(buf: &mut [u8], off: usize, v: u64) {
    buf[off..off + 8].copy_from_slice(&v.to_ne_bytes());
}

fn get_u64(buf: &[u8], off: usize) -> u64 {
    u64::from_ne_bytes(buf[off..off + 8].try_into().unwrap())
}

fn put_cstr(field: &mut [u8], value: &str) {
    let len = value.len().min(field.len() - 1);
    field[..len].copy_from_slice(&value.as_bytes()[..len]);
    field[len] = 0;
}

fn cstr_lossy(data: &[u8]) -> String {
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    String::from_utf8_lossy(&data[..end]).into_owned()
}

pub fn init_driver() -> i32 {
    unsafe {
        libc::prctl(PR_GET_SHM_FD, 0, 0, 0, 0);
    }
    let fd = scan_fd("[fmac_shm]");
    if fd >= 0 {
        DRIVER_FD.store(fd, Ordering::Relaxed);
    }
    result_of_driver()
}

pub fn init_ctl() -> i32 {
    get_ctl_fd();
    result_of_driver()
}

fn result_of_driver() -> i32 {
    if DRIVER_FD.load(Ordering::Relaxed) >= 0 {
        0
    } else {
        -1
    }
}

pub fn ctl(value: i32) -> i32 {
    log::info!("ctl({value})");
    match value {
        1 => init_driver(),
        3 => init_ctl(),
        _ => -1,
    }
}

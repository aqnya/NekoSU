#![allow(dead_code)]

pub mod defs;
mod fd;
mod ioctl;
use crate::defs::*;
use anyhow::Result;
use std::sync::OnceLock;
use uapi::*;

static INFO_CACHE: OnceLock<ksu_get_info_cmd> = OnceLock::new();

pub fn set_manager_uid(uid: u32) -> Result<()> {
    let mut buf = uid.to_ne_bytes();
    ioctl::call(fmac_flag_IOC_SET_MANAGER_UID, &mut buf)?;
    Ok(())
}

pub fn set_sepolicy(payload: *const u8, payload_len: u64) -> Result<i32> {
    if payload.is_null() {
        return Ok(0);
    }
    let bytes = unsafe { std::slice::from_raw_parts(payload, payload_len as usize) };
    let mut data = bytes.to_vec();
    ioctl::call(fmac_flag_IOC_SET_SEPOLICY, &mut data)
}

pub fn report_boot_complete() {
    let _ = ioctl::call(fmac_flag_IOC_DISARM_SAFEMODE, &mut []);
}

pub fn set_mount_layout(mnt_id_off: u32, mnt_off: u32) -> Result<()> {
    let mut data = Vec::with_capacity(8);
    data.extend_from_slice(&mnt_id_off.to_ne_bytes());
    data.extend_from_slice(&mnt_off.to_ne_bytes());
    ioctl::call(fmac_flag_IOC_SET_MNT_LAYOUT, &mut data)?;
    Ok(())
}

pub fn set_init_pgrp() -> Result<()> {
    let mut data = [];
    ioctl::call(fmac_flag_IOC_SET_INIT_PGRP, &mut data)?;
    Ok(())
}

pub fn check_kernel_safemode() -> bool {
    let mut data = [];
    matches!(ioctl::call(fmac_flag_IOC_GET_SAFEMODE, &mut data), Ok(1))
}

pub fn get_info() -> ksu_get_info_cmd {
    *INFO_CACHE.get_or_init(|| {
        let mut info = ksu_get_info_cmd {
            version: 30000,
            flags: KSU_GET_INFO_FLAG_LKM,
            features: 0,
            uapi_version: KERNEL_SU_UAPI_VERSION,
        };

        let mut version = [0u8; 64];
        if ioctl::call(fmac_flag_IOC_GET_VERSION, &mut version).is_err() {
            info.flags = 0;
        }
        info
    })
}

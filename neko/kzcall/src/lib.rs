#![allow(dead_code)]

pub mod defs;
mod fd;
pub mod ioctl;
mod utils;
use crate::defs::*;
use anyhow::Result;
use std::sync::OnceLock;
use uapi::*;
use utils::*;

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

pub fn set_profile(uid: i32, root_uid: i32, caps: u64, domain: &str, ns: i32) -> Result<()> {
    let mut data = [0u8; FMAC_DATA_PROFILE as usize];
    put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
    put_u64(&mut data, FMAC_OFF_CAPS as usize, caps);
    put_cstr(
        &mut data[FMAC_OFF_DOMAIN as usize..FMAC_OFF_NS as usize],
        domain,
    );
    put_u32(&mut data, FMAC_OFF_NS as usize, ns as u32);
    put_u32(&mut data, FMAC_OFF_ROOT_UID as usize, root_uid as u32);
    ioctl::call(fmac_flag_IOC_SET_PROFILE, &mut data)?;
    Ok(())
}

pub fn add_selinux_rule(
    src: &str,
    tgt: &str,
    cls: &str,
    perm: &str,
    effect: i32,
    invert: bool,
) -> Result<()> {
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
    ioctl::call(fmac_flag_IOC_SEL_ADD_RULE, &mut data)?;
    Ok(())
}

pub fn feature_get(id: i32) -> Option<u64> {
    let mut data = [0u8; FMAC_DATA_FEATURE as usize];
    put_u32(&mut data, 0, id as u32);
    ioctl::call(fmac_flag_IOC_FEATURE_GET, &mut data).ok()?;
    Some(get_u64(&data, FMAC_OFF_FEATURE_VALUE as usize))
}

pub fn add_uid(uid: i32) -> Result<()> {
    if uid < 0 {
        anyhow::bail!("uid should not be negative: {uid}");
    }
    let mut data = (uid as u32).to_ne_bytes();
    ioctl::call(fmac_flag_IOC_ADD_UID, &mut data)?;
    Ok(())
}

pub fn del_uid(uid: i32) -> Result<()> {
    if uid < 0 {
        anyhow::bail!("uid should not be negative: {uid}");
    }
    let mut data = (uid as u32).to_ne_bytes();
    ioctl::call(fmac_flag_IOC_DEL_UID, &mut data)?;
    Ok(())
}

pub fn has_uid(uid: i32) -> Option<bool> {
    if uid < 0 {
        return None;
    }
    let mut data = (uid as u32).to_ne_bytes();
    ioctl::call(fmac_flag_IOC_HAS_UID, &mut data).ok()?;
    Some(u32::from_ne_bytes(data) != 0)
}

pub fn set_cap(uid: i32, caps: u64) -> Result<()> {
    if uid < 0 {
        anyhow::bail!("uid should not be negative: {uid}");
    }
    let mut data = [0u8; FMAC_DATA_CAP as usize];
    put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
    put_u64(&mut data, FMAC_OFF_CAPS as usize, caps);
    ioctl::call(fmac_flag_IOC_SET_CAP, &mut data)?;
    Ok(())
}

pub fn get_cap(uid: i32) -> Option<u64> {
    if uid < 0 {
        return None;
    }
    let mut data = [0u8; FMAC_DATA_CAP as usize];
    put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
    ioctl::call(fmac_flag_IOC_GET_CAP, &mut data).ok()?;
    Some(get_u64(&data, FMAC_OFF_CAPS as usize))
}

pub fn del_cap(uid: i32) -> Result<()> {
    if uid < 0 {
        anyhow::bail!("uid should not be negative: {uid}");
    }
    let mut data = [0u8; FMAC_DATA_CAP as usize];
    put_u32(&mut data, FMAC_OFF_UID as usize, uid as u32);
    ioctl::call(fmac_flag_IOC_DEL_CAP, &mut data)?;
    Ok(())
}

pub fn list_profiles() -> Option<String> {
    let mut data = vec![0u8; 64 * 1024];
    ioctl::call(fmac_flag_IOC_GET_PROFILES, &mut data).ok()?;
    Some(cstr_lossy(&data))
}

pub fn module_version() -> Option<String> {
    let mut data = [0u8; 64];
    ioctl::call(fmac_flag_IOC_GET_VERSION, &mut data).ok()?;
    Some(cstr_lossy(&data))
}

pub fn feature_list() -> Option<String> {
    let mut data = [0u8; 1024];
    ioctl::call(fmac_flag_IOC_FEATURE_LIST, &mut data).ok()?;
    Some(cstr_lossy(&data))
}

pub fn feature_set(id: i32, value: u64) -> Result<()> {
    let mut data = [0u8; FMAC_DATA_FEATURE as usize];
    put_u32(&mut data, 0, id as u32);
    put_u64(&mut data, FMAC_OFF_FEATURE_VALUE as usize, value);
    ioctl::call(fmac_flag_IOC_FEATURE_SET, &mut data)?;
    Ok(())
}

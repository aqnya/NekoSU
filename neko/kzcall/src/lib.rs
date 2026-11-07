#![allow(dead_code)]

pub mod defs;
pub mod ioctl;
mod fd;
mod utils;
use anyhow::Result;
use proto::FeatureEntry;
use proto::ProfileEntry;
use uapi::*;
use utils::*;

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

pub fn set_init_pgrp() -> Result<()> {
    let mut data = [];
    ioctl::call(fmac_flag_IOC_SET_INIT_PGRP, &mut data)?;
    Ok(())
}

pub fn check_kernel_safemode() -> bool {
    let mut data = [];
    matches!(ioctl::call(fmac_flag_IOC_GET_SAFEMODE, &mut data), Ok(1))
}

pub fn set_profile(profile: &ProfileEntry) -> Result<()> {
    let mut data = [0u8; FMAC_DATA_PROFILE as usize];
    put_u32(&mut data, FMAC_OFF_UID as usize, profile.uid as u32);
    put_u64(&mut data, FMAC_OFF_CAPS as usize, profile.caps);
    put_cstr(
        &mut data[FMAC_OFF_DOMAIN as usize..FMAC_OFF_NS as usize],
        &profile.domain,
    );
    put_u32(&mut data, FMAC_OFF_NS as usize, profile.ns as u32);
    put_u32(
        &mut data,
        FMAC_OFF_ROOT_UID as usize,
        profile.root_uid as u32,
    );
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

pub fn feature_set(entry: &FeatureEntry) -> Result<()> {
    let mut data = [0u8; FMAC_DATA_FEATURE as usize];
    put_u32(&mut data, 0, entry.id as u32);
    put_u64(&mut data, FMAC_OFF_FEATURE_VALUE as usize, entry.value);
    ioctl::call(fmac_flag_IOC_FEATURE_SET, &mut data)?;
    Ok(())
}

pub fn feature_get(id: i32) -> Option<u64> {
    let mut data = [0u8; FMAC_DATA_FEATURE as usize];
    put_u32(&mut data, 0, id as u32);
    ioctl::call(fmac_flag_IOC_FEATURE_GET, &mut data).ok()?;
    Some(get_u64(&data, FMAC_OFF_FEATURE_VALUE as usize))
}

pub const FMAC_CTL_NAME: &str = "fmac_ctl";

pub const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;

pub const KERNEL_SU_UAPI_VERSION: u32 = 5;
pub const KSU_GET_INFO_FLAG_LKM: u32 = 1 << 0;
pub const KSU_GET_INFO_FLAG_LATE_LOAD: u32 = 1 << 1;
pub const KSU_GET_INFO_FLAG_PR_BUILD: u32 = 1 << 2;
pub const KSU_GET_INFO_FLAG_BUNDLED: u32 = 1 << 3;
pub const KSU_GET_INFO_FLAG_PREINIT: u32 = 1 << 4;

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct ksu_get_info_cmd {
    pub version: u32,
    pub flags: u32,
    pub features: u32,
    pub uapi_version: u32,
}

pub const FMAC_CTL_NAME: &str = "[fmac_ctl]";

/// `_IOWR('F', 0, struct fmac_ioc)`, whose header is two `u32`s.
pub const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;

pub const KERNEL_SU_UAPI_VERSION: u32 = 5;

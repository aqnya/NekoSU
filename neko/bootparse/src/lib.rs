// SPDX-License-Identifier: GPL-3.0-or-later
//! Parser and patcher for Android boot / vendor_boot images.
//!
//! The image headers are generated from the AOSP `bootimg.h` with bindgen, so
//! the on-disk layout is not re-derived by hand.  On top of that this crate
//! provides the pieces `bootpatcher` needs: block extraction with compression
//! detection, a newc `cpio` reader/writer and a vendor_boot repacker that
//! preserves the vendor ramdisk table and AVB structures.

pub mod boot;
pub mod compress;
pub mod cpio;
pub mod parse;
pub mod patcher;
pub mod vendor_boot;

mod bindgen;
mod utils;

pub use boot::BootVer;
pub use compress::CompressFormat;
pub use cpio::{Cpio, CpioEntry};
pub use parse::{Block, BootImage, BootType, get_imgtype};
pub use patcher::VendorBootPatcher;
pub use vendor_boot::{VendorBootVer, VendorRamdiskEntry};

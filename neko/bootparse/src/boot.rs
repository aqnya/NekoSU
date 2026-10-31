// SPDX-License-Identifier: GPL-3.0-or-later
//! Boot image headers (`boot_img_hdr` v0..v4) and their block layout.

use crate::bindgen::bindings as boot_head;
use crate::utils::{align_to, read_struct, read_u32_le};

/// One of the five on-disk boot image header versions.
///
/// The bindings are generated from the AOSP header, so a v1 header embeds its
/// v0 base (`_base`), a v2 embeds v1, and so on.
#[derive(Debug, Clone, Copy)]
pub enum BootVer {
    V0(boot_head::boot_img_hdr_v0),
    V1(boot_head::boot_img_hdr_v1),
    V2(boot_head::boot_img_hdr_v2),
    V3(boot_head::boot_img_hdr_v3),
    V4(boot_head::boot_img_hdr_v4),
}

/// The blocks of a boot image, already sliced out of the image data.
#[derive(Debug, Clone, Copy)]
pub struct BootBlocks<'a> {
    pub kernel: &'a [u8],
    pub ramdisk: &'a [u8],
    pub second: Option<&'a [u8]>,
    pub recovery_dtbo: Option<&'a [u8]>,
    pub dtb: Option<&'a [u8]>,
    pub signature: Option<&'a [u8]>,
}

impl BootVer {
    /// Parse the header that starts at `data[0]`.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if !data.starts_with(&boot_head::BOOT_MAGIC[..8]) {
            return None;
        }
        // header_version sits at the same offset in every version.
        Some(match read_u32_le(data, 40)? {
            0 => BootVer::V0(read_struct(data)?),
            1 => BootVer::V1(read_struct(data)?),
            2 => BootVer::V2(read_struct(data)?),
            3 => BootVer::V3(read_struct(data)?),
            4 => BootVer::V4(read_struct(data)?),
            _ => return None,
        })
    }

    /// The header version number as stored on disk.
    pub fn version(&self) -> u32 {
        match self {
            BootVer::V0(header) => header.header_version,
            BootVer::V1(header) => header._base.header_version,
            BootVer::V2(header) => header._base._base.header_version,
            BootVer::V3(header) => header.header_version,
            BootVer::V4(header) => header._base.header_version,
        }
    }

    /// The image's page size (v3/v4 fix it at 4096, older headers store it).
    pub fn page_size(&self) -> usize {
        match self {
            BootVer::V0(header) => header.page_size as usize,
            BootVer::V1(header) => header._base.page_size as usize,
            BootVer::V2(header) => header._base._base.page_size as usize,
            BootVer::V3(_) | BootVer::V4(_) => 4096,
        }
    }

    /// The size of the header struct itself.
    pub fn header_size(&self) -> usize {
        match self {
            BootVer::V0(_) => size_of::<boot_head::boot_img_hdr_v0>(),
            BootVer::V1(_) => size_of::<boot_head::boot_img_hdr_v1>(),
            BootVer::V2(_) => size_of::<boot_head::boot_img_hdr_v2>(),
            BootVer::V3(_) => size_of::<boot_head::boot_img_hdr_v3>(),
            BootVer::V4(_) => size_of::<boot_head::boot_img_hdr_v4>(),
        }
    }

    /// The page-aligned space the header occupies in the image.
    pub fn header_space(&self) -> usize {
        let page = self.page_size();
        align_to(self.header_size().max(page), page)
    }

    pub fn kernel_size(&self) -> usize {
        match self {
            BootVer::V0(header) => header.kernel_size as usize,
            BootVer::V1(header) => header._base.kernel_size as usize,
            BootVer::V2(header) => header._base._base.kernel_size as usize,
            BootVer::V3(header) => header.kernel_size as usize,
            BootVer::V4(header) => header._base.kernel_size as usize,
        }
    }

    pub fn ramdisk_size(&self) -> usize {
        match self {
            BootVer::V0(header) => header.ramdisk_size as usize,
            BootVer::V1(header) => header._base.ramdisk_size as usize,
            BootVer::V2(header) => header._base._base.ramdisk_size as usize,
            BootVer::V3(header) => header.ramdisk_size as usize,
            BootVer::V4(header) => header._base.ramdisk_size as usize,
        }
    }

    pub fn second_size(&self) -> usize {
        match self {
            BootVer::V0(header) => header.second_size as usize,
            BootVer::V1(header) => header._base.second_size as usize,
            BootVer::V2(header) => header._base._base.second_size as usize,
            BootVer::V3(_) | BootVer::V4(_) => 0,
        }
    }

    pub fn recovery_dtbo_size(&self) -> usize {
        match self {
            BootVer::V0(_) | BootVer::V3(_) | BootVer::V4(_) => 0,
            BootVer::V1(header) => header.recovery_dtbo_size as usize,
            BootVer::V2(header) => header._base.recovery_dtbo_size as usize,
        }
    }

    pub fn dtb_size(&self) -> usize {
        match self {
            BootVer::V0(_) | BootVer::V1(_) | BootVer::V3(_) | BootVer::V4(_) => 0,
            BootVer::V2(header) => header.dtb_size as usize,
        }
    }

    pub fn signature_size(&self) -> usize {
        match self {
            BootVer::V4(header) => header.signature_size as usize,
            _ => 0,
        }
    }

    /// Slice the image's blocks out of `data`, page-aligning between them.
    pub fn blocks<'a>(&self, data: &'a [u8]) -> Option<BootBlocks<'a>> {
        let page = self.page_size();
        let mut offset = self.header_space();

        let kernel = take(data, &mut offset, self.kernel_size(), page)?;
        let ramdisk = take(data, &mut offset, self.ramdisk_size(), page)?;
        let second = take_optional(data, &mut offset, self.second_size(), page)?;
        let recovery_dtbo = take_optional(data, &mut offset, self.recovery_dtbo_size(), page)?;
        let dtb = take_optional(data, &mut offset, self.dtb_size(), page)?;
        let signature = take_optional(data, &mut offset, self.signature_size(), page)?;

        Some(BootBlocks {
            kernel,
            ramdisk,
            second,
            recovery_dtbo,
            dtb,
            signature,
        })
    }
}

/// Slice `size` bytes at `offset` and advance to the next page boundary.
pub(crate) fn take<'a>(
    data: &'a [u8],
    offset: &mut usize,
    size: usize,
    page: usize,
) -> Option<&'a [u8]> {
    let start = *offset;
    let end = start.checked_add(size)?;
    let block = data.get(start..end)?;
    *offset = align_to(end, page);
    Some(block)
}

/// Like [`take`], but an empty block is reported as absent.
pub(crate) fn take_optional<'a>(
    data: &'a [u8],
    offset: &mut usize,
    size: usize,
    page: usize,
) -> Option<Option<&'a [u8]>> {
    if size == 0 {
        return Some(None);
    }
    take(data, offset, size, page).map(Some)
}

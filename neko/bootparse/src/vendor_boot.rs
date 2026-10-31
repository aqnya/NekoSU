// SPDX-License-Identifier: GPL-3.0-or-later
//! Vendor boot image headers (`vendor_boot_img_hdr` v3/v4) and the v4 vendor
//! ramdisk table.

use crate::bindgen::bindings as boot_head;
use crate::boot::{take, take_optional};
use crate::compress::{CompressFormat, decompress, parse_compress_format};
use crate::utils::{align_to, read_struct_at, read_u32_le, trim_nul};

use anyhow::Result;

#[derive(Debug, Clone, Copy)]
pub enum VendorBootVer {
    V3(boot_head::vendor_boot_img_hdr_v3),
    V4(boot_head::vendor_boot_img_hdr_v4),
}

/// The blocks of a vendor boot image.
#[derive(Debug, Clone, Copy)]
pub struct VendorBootBlocks<'a> {
    /// The whole vendor ramdisk section (one or more concatenated ramdisks).
    pub ramdisk: &'a [u8],
    pub dtb: &'a [u8],
    /// The v4 vendor ramdisk table; `None` for v3 images.
    pub table: Option<&'a [u8]>,
    /// The v4 bootconfig section.
    pub bootconfig: Option<&'a [u8]>,
}

/// One ramdisk inside the vendor ramdisk section.
#[derive(Debug, Clone)]
pub struct VendorRamdiskEntry<'a> {
    pub index: usize,
    pub ramdisk_type: u32,
    pub name: String,
    pub data: &'a [u8],
}

impl VendorRamdiskEntry<'_> {
    /// The compression `mkbootimg` applied to this ramdisk.
    pub fn format(&self) -> CompressFormat {
        parse_compress_format(self.data)
    }

    /// The uncompressed payload; raw ramdisks are returned unchanged.
    pub fn decompress(&self) -> Result<Vec<u8>> {
        decompress(self.data)
    }
}

impl VendorBootVer {
    /// Parse the header that starts at `data[0]`.
    pub fn parse(data: &[u8]) -> Option<Self> {
        if !data.starts_with(&boot_head::VENDOR_BOOT_MAGIC[..8]) {
            return None;
        }
        Some(match read_u32_le(data, 8)? {
            3 => VendorBootVer::V3(read_struct_at(data, 0)?),
            4 => VendorBootVer::V4(read_struct_at(data, 0)?),
            _ => return None,
        })
    }

    pub fn version(&self) -> u32 {
        match self {
            VendorBootVer::V3(header) => header.header_version,
            VendorBootVer::V4(header) => header._base.header_version,
        }
    }

    pub fn page_size(&self) -> usize {
        match self {
            VendorBootVer::V3(header) => header.page_size as usize,
            VendorBootVer::V4(header) => header._base.page_size as usize,
        }
    }

    pub fn header_size(&self) -> usize {
        let declared = match self {
            VendorBootVer::V3(header) => header.header_size as usize,
            VendorBootVer::V4(header) => header._base.header_size as usize,
        };
        let actual = match self {
            VendorBootVer::V3(_) => size_of::<boot_head::vendor_boot_img_hdr_v3>(),
            VendorBootVer::V4(_) => size_of::<boot_head::vendor_boot_img_hdr_v4>(),
        };
        declared.max(actual)
    }

    /// The page-aligned space the header occupies in the image.
    pub fn header_space(&self) -> usize {
        align_to(self.header_size(), self.page_size())
    }

    pub fn ramdisk_size(&self) -> usize {
        match self {
            VendorBootVer::V3(header) => header.vendor_ramdisk_size as usize,
            VendorBootVer::V4(header) => header._base.vendor_ramdisk_size as usize,
        }
    }

    pub fn dtb_size(&self) -> usize {
        match self {
            VendorBootVer::V3(header) => header.dtb_size as usize,
            VendorBootVer::V4(header) => header._base.dtb_size as usize,
        }
    }

    pub fn table_size(&self) -> usize {
        match self {
            VendorBootVer::V3(_) => 0,
            VendorBootVer::V4(header) => header.vendor_ramdisk_table_size as usize,
        }
    }

    pub fn table_entry_num(&self) -> usize {
        match self {
            VendorBootVer::V3(_) => 0,
            VendorBootVer::V4(header) => header.vendor_ramdisk_table_entry_num as usize,
        }
    }

    pub fn table_entry_size(&self) -> usize {
        match self {
            VendorBootVer::V3(_) => 0,
            VendorBootVer::V4(header) => header.vendor_ramdisk_table_entry_size as usize,
        }
    }

    pub fn bootconfig_size(&self) -> usize {
        match self {
            VendorBootVer::V3(_) => 0,
            VendorBootVer::V4(header) => header.bootconfig_size as usize,
        }
    }

    /// Slice the image's blocks out of `data`, page-aligning between them.
    pub fn blocks<'a>(&self, data: &'a [u8]) -> Option<VendorBootBlocks<'a>> {
        let page = self.page_size();
        let mut offset = self.header_space();

        let ramdisk = take(data, &mut offset, self.ramdisk_size(), page)?;
        let dtb = take(data, &mut offset, self.dtb_size(), page)?;

        let (table, bootconfig) = match self {
            VendorBootVer::V3(_) => (None, None),
            VendorBootVer::V4(_) => {
                let table = take(data, &mut offset, self.table_size(), page)?;
                let bootconfig = take_optional(data, &mut offset, self.bootconfig_size(), page)?;
                (Some(table), bootconfig)
            }
        };

        Some(VendorBootBlocks {
            ramdisk,
            dtb,
            table,
            bootconfig,
        })
    }

    /// Split the vendor ramdisk section into its entries.
    ///
    /// v3 images carry a single ramdisk; v4 images carry a table describing
    /// one or more ramdisks inside the section.
    pub fn ramdisks<'a>(&self, data: &'a [u8]) -> Option<Vec<VendorRamdiskEntry<'a>>> {
        let blocks = self.blocks(data)?;

        let VendorBootVer::V4(_) = self else {
            return Some(vec![VendorRamdiskEntry {
                index: 0,
                ramdisk_type: 0,
                name: String::new(),
                data: blocks.ramdisk,
            }]);
        };

        let table = blocks.table?;
        let entry_size = self.table_entry_size();
        if entry_size != size_of::<boot_head::vendor_ramdisk_table_entry_v4>() {
            return None;
        }

        let count = self.table_entry_num();
        let mut entries = Vec::with_capacity(count);
        for index in 0..count {
            let entry: boot_head::vendor_ramdisk_table_entry_v4 =
                read_struct_at(table, index * entry_size)?;
            let start = entry.ramdisk_offset as usize;
            let end = start.checked_add(entry.ramdisk_size as usize)?;
            entries.push(VendorRamdiskEntry {
                index,
                ramdisk_type: entry.ramdisk_type,
                name: String::from_utf8_lossy(trim_nul(&entry.ramdisk_name)).into_owned(),
                data: blocks.ramdisk.get(start..end)?,
            });
        }

        Some(entries)
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
//! Whole-image parsing: recognises boot and vendor_boot images, slices their
//! blocks and exposes them as [`Block`]s that know their own compression.

use anyhow::{Context, Result, bail};

use crate::bindgen::bindings as boot_head;
use crate::boot::{BootBlocks, BootVer};
use crate::compress::{CompressFormat, decompress, parse_compress_format};
use crate::vendor_boot::{VendorBootBlocks, VendorBootVer, VendorRamdiskEntry};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BootType {
    Boot,
    VendorBoot,
    Unknown,
}

/// Classify an image by its magic.  `None` means the input is too short to
/// carry a magic at all.
pub fn get_imgtype(data: &[u8]) -> Option<BootType> {
    let magic = data.get(..8)?;
    if magic == &boot_head::BOOT_MAGIC[..8] {
        Some(BootType::Boot)
    } else if magic == &boot_head::VENDOR_BOOT_MAGIC[..8] {
        Some(BootType::VendorBoot)
    } else {
        Some(BootType::Unknown)
    }
}

/// Parse a boot image header.
pub fn boot(data: &[u8]) -> Option<BootVer> {
    BootVer::parse(data)
}

/// Parse a vendor boot image header.
pub fn vendor_boot(data: &[u8]) -> Option<VendorBootVer> {
    VendorBootVer::parse(data)
}

/// A byte range of an image plus the compression `mkbootimg` applied to it.
#[derive(Debug, Clone, Copy)]
pub struct Block<'a> {
    data: &'a [u8],
}

impl<'a> Block<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data }
    }

    /// The block exactly as stored in the image.
    pub fn raw(&self) -> &'a [u8] {
        self.data
    }

    pub fn len(&self) -> usize {
        self.data.len()
    }

    pub fn is_empty(&self) -> bool {
        self.data.is_empty()
    }

    pub fn format(&self) -> CompressFormat {
        parse_compress_format(self.data)
    }

    pub fn is_compressed(&self) -> bool {
        !matches!(
            self.format(),
            CompressFormat::UNKNOWN | CompressFormat::CPIO
        )
    }

    /// The uncompressed payload; raw blocks are returned unchanged.
    pub fn decompress(&self) -> Result<Vec<u8>> {
        decompress(self.data)
    }
}

/// A parsed boot image.
pub struct Boot<'a> {
    pub header: BootVer,
    pub blocks: BootBlocks<'a>,
}

/// A parsed vendor boot image.
pub struct VendorBoot<'a> {
    pub header: VendorBootVer,
    pub blocks: VendorBootBlocks<'a>,
    pub ramdisks: Vec<VendorRamdiskEntry<'a>>,
}

/// A boot or vendor_boot image.
pub struct BootImage<'a> {
    data: &'a [u8],
    boot: Option<Boot<'a>>,
    vendor: Option<VendorBoot<'a>>,
}

impl<'a> BootImage<'a> {
    pub fn parse(data: &'a [u8]) -> Result<Self> {
        match get_imgtype(data).context("image is too short to hold a boot magic")? {
            BootType::Boot => {
                let header =
                    BootVer::parse(data).context("unsupported or truncated boot header")?;
                let blocks = header.blocks(data).context("truncated boot image blocks")?;
                Ok(Self {
                    data,
                    boot: Some(Boot { header, blocks }),
                    vendor: None,
                })
            }
            BootType::VendorBoot => {
                let header = VendorBootVer::parse(data)
                    .context("unsupported or truncated vendor boot header")?;
                let blocks = header
                    .blocks(data)
                    .context("truncated vendor boot image blocks")?;
                let ramdisks = header
                    .ramdisks(data)
                    .context("invalid vendor ramdisk table")?;
                Ok(Self {
                    data,
                    boot: None,
                    vendor: Some(VendorBoot {
                        header,
                        blocks,
                        ramdisks,
                    }),
                })
            }
            BootType::Unknown => bail!("not a boot or vendor_boot image"),
        }
    }

    /// The image exactly as parsed.
    pub fn data(&self) -> &'a [u8] {
        self.data
    }

    pub fn kind(&self) -> BootType {
        if self.boot.is_some() {
            BootType::Boot
        } else {
            BootType::VendorBoot
        }
    }

    pub fn boot(&self) -> Option<&Boot<'a>> {
        self.boot.as_ref()
    }

    pub fn vendor_boot(&self) -> Option<&VendorBoot<'a>> {
        self.vendor.as_ref()
    }

    /// The header version of a boot image, `None` for vendor_boot images.
    pub fn boot_version(&self) -> Option<u32> {
        self.boot.as_ref().map(|boot| boot.header.version())
    }

    /// The header version of a vendor_boot image, `None` for boot images.
    pub fn vendor_boot_version(&self) -> Option<u32> {
        self.vendor.as_ref().map(|boot| boot.header.version())
    }

    /// The kernel block (boot images only).
    pub fn kernel(&self) -> Option<Block<'a>> {
        self.boot
            .as_ref()
            .map(|boot| Block::new(boot.blocks.kernel))
    }

    /// The vendor ramdisk entries (vendor_boot images only).
    ///
    /// v3 images always yield exactly one entry; v4 images yield one entry per
    /// vendor ramdisk table row.
    pub fn ramdisks(&self) -> Option<&[VendorRamdiskEntry<'a>]> {
        self.vendor.as_ref().map(|boot| boot.ramdisks.as_slice())
    }

    /// The single boot ramdisk (boot images only).
    pub fn ramdisk(&self) -> Option<Block<'a>> {
        self.boot
            .as_ref()
            .map(|boot| Block::new(boot.blocks.ramdisk))
    }

    /// The device tree blob, if the image carries one.
    pub fn dtb(&self) -> Option<Block<'a>> {
        match (&self.boot, &self.vendor) {
            (Some(boot), _) => boot.blocks.dtb.map(Block::new),
            (_, Some(vendor)) => Some(Block::new(vendor.blocks.dtb)),
            _ => None,
        }
    }

    /// The v4 bootconfig section, if present.
    pub fn bootconfig(&self) -> Option<Block<'a>> {
        self.vendor
            .as_ref()
            .and_then(|vendor| vendor.blocks.bootconfig)
            .map(Block::new)
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
//! Repacking of vendor_boot images with a replaced vendor ramdisk.
//!
//! The vendor ramdisk section is rebuilt from its entries, the v4 table is
//! rewritten with the new offsets/sizes and the AVB tail, vbmeta blob and
//! footer are preserved so the flashed image stays structurally valid.  The
//! AVB signature itself cannot survive patching; NekoSU flashes the result
//! without re-signing.

use std::collections::HashMap;

use anyhow::{Context, Result, ensure};
use zerocopy::IntoBytes;

use crate::bindgen::bindings as boot_head;
use crate::compress::{compress, parse_compress_format};
use crate::parse::BootImage;
use crate::utils::{align_to, pad_to, read_struct_at};
use crate::vendor_boot::VendorBootVer;

const AVB_FOOTER_MAGIC: &[u8] = b"AVBf";
const AVB_MAGIC: &[u8] = b"AVB0";
const AVB_FOOTER_SIZE: usize = 64;
const FOOTER_ORIGINAL_IMAGE_SIZE: usize = 12;
const FOOTER_VBMETA_OFFSET: usize = 20;
const FOOTER_VBMETA_SIZE: usize = 28;

struct Avb<'a> {
    tail: Option<&'a [u8]>,
    vbmeta: &'a [u8],
    footer: &'a [u8],
}

/// Rebuilds a vendor_boot image with zero or more ramdisks replaced.
pub struct VendorBootPatcher<'a> {
    image: &'a BootImage<'a>,
    replacements: HashMap<usize, Vec<u8>>,
}

impl<'a> VendorBootPatcher<'a> {
    pub fn new(image: &'a BootImage<'a>) -> Result<Self> {
        ensure!(
            image.vendor_boot().is_some(),
            "only vendor_boot images can be patched"
        );
        Ok(Self {
            image,
            replacements: HashMap::new(),
        })
    }

    /// Replace the vendor ramdisk at `index` with the *uncompressed* payload
    /// `data`; the entry's original compression is reapplied on repack.
    pub fn replace_vendor_ramdisk(&mut self, index: usize, data: Vec<u8>) -> &mut Self {
        self.replacements.insert(index, data);
        self
    }

    /// Repack the image and return the new bytes.
    pub fn patch(&self) -> Result<Vec<u8>> {
        let vendor = self
            .image
            .vendor_boot()
            .context("not a vendor_boot image")?;
        let header = vendor.header;
        let page = header.page_size();
        let data = self.image.data();
        let header_space = header.header_space();

        for &index in self.replacements.keys() {
            ensure!(
                index < vendor.ramdisks.len(),
                "invalid vendor ramdisk index {index}"
            );
        }

        let mut out = Vec::with_capacity(data.len());
        out.extend_from_slice(
            data.get(..header_space)
                .context("truncated vendor_boot header")?,
        );

        // The vendor ramdisk section: keep untouched entries byte-for-byte and
        // recompress the replaced ones with their original format.
        let mut new_entries: Vec<(u32, u32)> = Vec::with_capacity(vendor.ramdisks.len());
        for (index, entry) in vendor.ramdisks.iter().enumerate() {
            let payload = match self.replacements.get(&index) {
                Some(replacement) => compress(replacement, parse_compress_format(entry.data))?,
                None => entry.data.to_vec(),
            };
            new_entries.push(((out.len() - header_space) as u32, payload.len() as u32));
            out.extend_from_slice(&payload);
        }
        let ramdisk_size = (out.len() - header_space) as u32;
        pad_to(&mut out, page);

        out.extend_from_slice(vendor.blocks.dtb);
        pad_to(&mut out, page);

        let mut table_size = 0u32;
        if let Some(table) = vendor.blocks.table {
            let entry_size = header.table_entry_size();
            let table_start = out.len();
            for (index, (offset, size)) in new_entries.iter().enumerate() {
                let mut entry: boot_head::vendor_ramdisk_table_entry_v4 =
                    read_struct_at(table, index * entry_size)
                        .context("truncated vendor ramdisk table")?;
                entry.ramdisk_offset = *offset;
                entry.ramdisk_size = *size;
                out.extend_from_slice(entry.as_bytes());
            }
            table_size = (out.len() - table_start) as u32;
            ensure!(
                table_size == header.table_size() as u32,
                "vendor ramdisk table size changed while repacking"
            );
            pad_to(&mut out, page);
        }

        if let Some(bootconfig) = vendor.blocks.bootconfig {
            out.extend_from_slice(bootconfig);
            pad_to(&mut out, page);
        }

        // AVB: keep the vbmeta blob and footer, only moving them if the
        // payload grew or shrank.
        let avb = parse_avb(data, vendor_blocks_end(&header));
        let mut zero_end = data.len();
        let mut new_footer = None;

        if let Some(avb) = avb {
            let tail_len = avb.tail.map_or(0, <[u8]>::len);
            let needed = align_to(out.len() + align_to(tail_len, page), 4096)
                + avb.vbmeta.len()
                + AVB_FOOTER_SIZE;
            ensure!(
                needed <= data.len(),
                "no space left for AVB structures (need {needed} bytes, image is {})",
                data.len()
            );

            if let Some(tail) = avb.tail {
                out.extend_from_slice(tail);
                pad_to(&mut out, page);
            }

            let original_image_size = out.len() as u64;
            pad_to(&mut out, 4096);
            let vbmeta_offset = out.len() as u64;
            out.extend_from_slice(avb.vbmeta);

            zero_end = data.len() - AVB_FOOTER_SIZE;

            let mut footer = avb.footer.to_vec();
            footer[FOOTER_ORIGINAL_IMAGE_SIZE..FOOTER_ORIGINAL_IMAGE_SIZE + 8]
                .copy_from_slice(&original_image_size.to_be_bytes());
            footer[FOOTER_VBMETA_OFFSET..FOOTER_VBMETA_OFFSET + 8]
                .copy_from_slice(&vbmeta_offset.to_be_bytes());
            new_footer = Some(footer);
        }

        if out.len() < zero_end {
            out.resize(zero_end, 0);
        }
        if let Some(footer) = new_footer {
            ensure!(
                out.len() == zero_end,
                "AVB footer would overlap the patched image"
            );
            out.extend_from_slice(&footer);
        }

        // Patch the header's block sizes in place.
        let ramdisk_size_offset =
            core::mem::offset_of!(boot_head::vendor_boot_img_hdr_v3, vendor_ramdisk_size);
        out[ramdisk_size_offset..ramdisk_size_offset + 4]
            .copy_from_slice(&ramdisk_size.to_le_bytes());

        if matches!(header, VendorBootVer::V4(_)) {
            let table_size_offset =
                core::mem::offset_of!(boot_head::vendor_boot_img_hdr_v4, vendor_ramdisk_table_size);
            out[table_size_offset..table_size_offset + 4]
                .copy_from_slice(&table_size.to_le_bytes());
        }

        Ok(out)
    }
}

/// The offset just past the last block of the original image, page-aligned the
/// same way [`VendorBootVer::blocks`] slices them.
fn vendor_blocks_end(header: &VendorBootVer) -> usize {
    let page = header.page_size();
    let mut end = header.header_space();
    end = align_to(end + header.ramdisk_size(), page);
    end = align_to(end + header.dtb_size(), page);
    end = align_to(end + header.table_size(), page);
    align_to(end + header.bootconfig_size(), page)
}

fn parse_avb(data: &[u8], blocks_end: usize) -> Option<Avb<'_>> {
    if data.len() < AVB_FOOTER_SIZE {
        return None;
    }

    let footer = &data[data.len() - AVB_FOOTER_SIZE..];
    if !footer.starts_with(AVB_FOOTER_MAGIC) {
        return None;
    }

    let original_image_size = be_u64(footer, FOOTER_ORIGINAL_IMAGE_SIZE)? as usize;
    let vbmeta_offset = be_u64(footer, FOOTER_VBMETA_OFFSET)? as usize;
    let vbmeta_size = be_u64(footer, FOOTER_VBMETA_SIZE)? as usize;

    let vbmeta = data.get(vbmeta_offset..vbmeta_offset.checked_add(vbmeta_size)?)?;
    if !vbmeta.starts_with(AVB_MAGIC) {
        // Some dumps carry a footer whose vbmeta offset is bogus; leave the
        // AVB metadata alone instead of failing the patch.
        return None;
    }

    let tail = if original_image_size > blocks_end {
        data.get(blocks_end..original_image_size)
    } else {
        None
    };

    Some(Avb {
        tail,
        vbmeta,
        footer,
    })
}

fn be_u64(data: &[u8], offset: usize) -> Option<u64> {
    Some(u64::from_be_bytes(
        data.get(offset..offset.checked_add(8)?)?.try_into().ok()?,
    ))
}

// SPDX-License-Identifier: GPL-3.0-or-later
//! Compression formats used by Android boot images.
//!
//! Detection mirrors Magisk's `magiskboot`; every format that can appear in a
//! boot or vendor_boot image can be decompressed, and every format we detect
//! can also be produced again so a replaced ramdisk keeps its original
//! compression.  LZOP is handled as a complete `.lzo` container (header,
//! block checksums and end marker), not as a bare LZO1X stream.

use anyhow::{Context, Result};
use flate2::read::MultiGzDecoder;
use flate2::write::GzEncoder;
use std::io::{Cursor, Read, Write};

const GZIP1_MAGIC: &[u8] = b"\x1f\x8b";
const GZIP2_MAGIC: &[u8] = b"\x1f\x9e";
const LZOP_MAGIC: &[u8] = b"\x89LZO";
const XZ_MAGIC: &[u8] = b"\xfd7zXZ";
const BZIP_MAGIC: &[u8] = b"BZh";
const LZ4_LEG_MAGIC: &[u8] = b"\x02\x21\x4c\x18";
const LZ41_MAGIC: &[u8] = b"\x03\x21\x4c\x18";
const LZ42_MAGIC: &[u8] = b"\x04\x22\x4d\x18";
const CPIO_MAGIC: &[u8] = b"070701";
const CPIO_CRC_MAGIC: &[u8] = b"070702";

/// Largest uncompressed size of a single legacy LZ4 block.
const LZ4_LEGACY_BLOCK_SIZE: usize = 8 * 1024 * 1024;

#[derive(Debug, PartialEq, Eq, Copy, Clone)]
pub enum CompressFormat {
    UNKNOWN,
    GZIP,
    LZOP,
    XZ,
    LZMA,
    BZIP2,
    LZ4,
    #[allow(non_camel_case_types)]
    LZ4_LEGACY,
    CPIO,
}

/// LZMA has no real magic; `magiskboot` guesses it from the LZMA properties
/// byte and the dictionary size, which must be a power of two.
fn guess_lzma(data: &[u8]) -> bool {
    if data.len() <= 13 || data[0] != 0x5d {
        return false;
    }
    let dict_size = u32::from_le_bytes(data[1..5].try_into().unwrap());
    if dict_size == 0 || !dict_size.is_power_of_two() {
        return false;
    }
    &data[5..13] == b"\xff\xff\xff\xff\xff\xff\xff\xff"
}

/// Detect the compression format of a boot image block.
pub fn parse_compress_format(data: &[u8]) -> CompressFormat {
    if data.starts_with(GZIP1_MAGIC) || data.starts_with(GZIP2_MAGIC) {
        CompressFormat::GZIP
    } else if data.starts_with(LZOP_MAGIC) {
        CompressFormat::LZOP
    } else if data.starts_with(XZ_MAGIC) {
        CompressFormat::XZ
    } else if data.starts_with(BZIP_MAGIC) {
        CompressFormat::BZIP2
    } else if data.starts_with(LZ41_MAGIC) || data.starts_with(LZ42_MAGIC) {
        CompressFormat::LZ4
    } else if data.starts_with(LZ4_LEG_MAGIC) {
        CompressFormat::LZ4_LEGACY
    } else if guess_lzma(data) {
        CompressFormat::LZMA
    } else if data.starts_with(CPIO_MAGIC) || data.starts_with(CPIO_CRC_MAGIC) {
        CompressFormat::CPIO
    } else {
        CompressFormat::UNKNOWN
    }
}

/// Compress `data` with `format`, keeping the original container format.
pub fn compress(data: &[u8], format: CompressFormat) -> Result<Vec<u8>> {
    match format {
        CompressFormat::GZIP => {
            let mut encoder = GzEncoder::new(Vec::new(), flate2::Compression::default());
            encoder.write_all(data)?;
            Ok(encoder.finish()?)
        }
        CompressFormat::XZ => {
            use lzma_rust2::{XzOptions, XzWriter};
            let mut encoder = XzWriter::new(Vec::new(), XzOptions::with_preset(6))?;
            encoder.write_all(data)?;
            Ok(encoder.finish()?)
        }
        CompressFormat::LZMA => {
            use lzma_rust2::{LzmaOptions, LzmaWriter};
            // Unknown unpacked size, exactly like mkbootimg/magiskboot, so the
            // 13-byte .lzma header ends with the 0xff marker guess_lzma checks.
            let mut encoder =
                LzmaWriter::new_use_header(Vec::new(), &LzmaOptions::with_preset(6), None)?;
            encoder.write_all(data)?;
            Ok(encoder.finish()?)
        }
        CompressFormat::BZIP2 => {
            let mut encoder =
                bzip2::write::BzEncoder::new(Vec::new(), bzip2::Compression::default());
            encoder.write_all(data)?;
            Ok(encoder.finish()?)
        }
        CompressFormat::LZ4 => {
            let mut encoder = lz4::EncoderBuilder::new().build(Vec::new())?;
            encoder.write_all(data)?;
            let (out, result) = encoder.finish();
            result?;
            Ok(out)
        }
        CompressFormat::LZ4_LEGACY => {
            // Legacy frame: a magic followed by size-prefixed compressed
            // blocks (the prefix is the *compressed* size).
            let mut out = Vec::new();
            out.extend_from_slice(LZ4_LEG_MAGIC);
            for chunk in data.chunks(LZ4_LEGACY_BLOCK_SIZE) {
                let compressed =
                    lz4::block::compress(chunk, Some(lz4::block::CompressionMode::DEFAULT), false)?;
                out.extend_from_slice(&(compressed.len() as u32).to_le_bytes());
                out.extend_from_slice(&compressed);
            }
            Ok(out)
        }
        CompressFormat::CPIO | CompressFormat::UNKNOWN => Ok(data.to_vec()),
        #[cfg(feature = "experimental")]
        CompressFormat::LZOP => compress_lzop(data),
        #[cfg(not(feature = "experimental"))]
        CompressFormat::LZOP => Err(anyhow::anyhow!(
            "LZOP compression requires the `experimental` feature"
        )),
    }
}

/// Decompress `data`, auto-detecting the format.  Raw (uncompressed) blocks are
/// returned as-is so callers can treat this as "give me the payload".
pub fn decompress(data: &[u8]) -> Result<Vec<u8>> {
    match parse_compress_format(data) {
        CompressFormat::UNKNOWN | CompressFormat::CPIO => Ok(data.to_vec()),
        CompressFormat::GZIP => {
            let mut decoder = MultiGzDecoder::new(data);
            let mut out = Vec::new();
            decoder.read_to_end(&mut out)?;
            Ok(out)
        }
        #[cfg(feature = "experimental")]
        CompressFormat::LZOP => decompress_lzop(data),
        #[cfg(not(feature = "experimental"))]
        CompressFormat::LZOP => Err(anyhow::anyhow!(
            "LZOP decompression requires the `experimental` feature"
        )),
        CompressFormat::XZ => {
            use lzma_rust2::XzReader;
            let mut decoder = XzReader::new(data, true);
            let mut out = Vec::new();
            decoder.read_to_end(&mut out)?;
            Ok(out)
        }
        CompressFormat::BZIP2 => {
            let mut decoder = bzip2::read::MultiBzDecoder::new(data);
            let mut out = Vec::new();
            decoder.read_to_end(&mut out)?;
            Ok(out)
        }
        CompressFormat::LZ4 => {
            let mut decoder = lz4::Decoder::new(Cursor::new(data))?;
            let mut out = Vec::new();
            decoder.read_to_end(&mut out)?;
            Ok(out)
        }
        CompressFormat::LZ4_LEGACY => decompress_lz4_legacy(data),
        CompressFormat::LZMA => {
            use lzma_rust2::LzmaReader;
            let mut decoder = LzmaReader::new_mem_limit(data, 65536, None)?;
            let mut out = Vec::new();
            decoder.read_to_end(&mut out)?;
            Ok(out)
        }
    }
}

fn decompress_lz4_legacy(data: &[u8]) -> Result<Vec<u8>> {
    let body = data
        .strip_prefix(LZ4_LEG_MAGIC)
        .context("lz4 legacy stream is missing its magic")?;
    let mut out = Vec::new();
    let mut block = vec![0u8; LZ4_LEGACY_BLOCK_SIZE];
    let mut pos = 0;

    while pos + 4 <= body.len() {
        let block_size = u32::from_le_bytes(body[pos..pos + 4].try_into().unwrap()) as usize;
        pos += 4;

        // The "LG" variant appends the total uncompressed size after the last
        // block; anything that does not fit is treated as that trailer.
        let Some(compressed) = body.get(pos..pos + block_size) else {
            break;
        };
        pos += block_size;

        let len = lz4::block::decompress_to_buffer(
            compressed,
            Some(LZ4_LEGACY_BLOCK_SIZE as i32),
            &mut block,
        )?;
        out.extend_from_slice(&block[..len]);
    }

    Ok(out)
}

/// Compress `data` into a `.lzo` stream.
/// Note: this uses LZO1X-999 (method 3, level 9), not LZO1X-1.
#[cfg(feature = "experimental")]
fn compress_lzop(data: &[u8]) -> Result<Vec<u8>> {
    crate::lzo::compress_lzop(data, "").map_err(|error| anyhow::anyhow!("lzop: {error}"))
}

/// Read a complete `.lzo` file, checking its header and block checksums.
#[cfg(feature = "experimental")]
fn decompress_lzop(data: &[u8]) -> Result<Vec<u8>> {
    crate::lzo::decompress_lzop(data).map_err(|error| anyhow::anyhow!("lzop: {error}"))
}

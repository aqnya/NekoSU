use lzokay_native;
use std::io::{Cursor, Read, Write};

const LZOP_MAGIC: [u8; 9] = [0x89, 0x4c, 0x5a, 0x4f, 0x00, 0x0d, 0x0a, 0x1a, 0x0a];

const F_ADLER32_D: u32 = 0x0000_0001;
const F_ADLER32_C: u32 = 0x0000_0002;
const F_STDIN: u32 = 0x0000_0004;
const F_STDOUT: u32 = 0x0000_0008;
const F_H_EXTRA_FIELD: u32 = 0x0000_0040;
const F_CRC32_D: u32 = 0x0000_0100;
const F_CRC32_C: u32 = 0x0000_0200;
const F_H_FILTER: u32 = 0x0000_0800;
const F_H_CRC32: u32 = 0x0000_1000;
const F_MASK: u32 = 0x0000_3fff;

const F_OS_UNIX: u32 = 0x0300_0000;
const F_OS_MASK: u32 = 0xff00_0000;
const F_CS_MASK: u32 = 0x00f0_0000;

const LZOP_VERSION: u16 = 0x1040;
const LZOP_LIB_VERSION: u16 = 0x20a0;
const LZOP_VERSION_NEEDED: u16 = 0x0940;

const BLOCK_SIZE: usize = 256 * 1024;
const MAX_BLOCK_SIZE: u32 = 64 * 1024 * 1024;

const M_LZO1X_999: u8 = 3;

pub struct LzopHeader {
    pub version: u16,
    pub lib_version: u16,
    pub version_needed: u16,
    pub method: u8,
    pub level: u8,
    pub flags: u32,
    pub filter: u32,
    pub mode: u32,
    pub mtime_low: u32,
    pub mtime_high: u32,
    pub name: String,
}

// ---- Checksum helpers ----

fn adler32_update(state: u32, data: &[u8]) -> u32 {
    let mut a = state & 0xffff;
    let mut b = state >> 16;
    for &byte in data {
        a = (a + byte as u32) % 65521;
        b = (b + a) % 65521;
    }
    (b << 16) | a
}

fn crc32_update(state: u32, data: &[u8]) -> u32 {
    let mut crc = state;
    for &byte in data {
        crc ^= byte as u32;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xedb8_8320;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

fn adler32(data: &[u8]) -> u32 {
    adler32_update(1, data)
}

fn crc32(data: &[u8]) -> u32 {
    !crc32_update(0xffff_ffff, data)
}

// ---- Big-endian read/write helpers ----

fn read_u8<R: Read>(r: &mut R) -> Result<u8, String> {
    let mut b = [0u8; 1];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read byte: {e}"))?;
    Ok(b[0])
}

fn read_u16_be<R: Read>(r: &mut R) -> Result<u16, String> {
    let mut b = [0u8; 2];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read u16: {e}"))?;
    Ok(u16::from_be_bytes(b))
}

fn read_u32_be<R: Read>(r: &mut R) -> Result<u32, String> {
    let mut b = [0u8; 4];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read u32: {e}"))?;
    Ok(u32::from_be_bytes(b))
}

fn write_u16_be<W: Write>(w: &mut W, v: u16) -> Result<(), String> {
    w.write_all(&v.to_be_bytes())
        .map_err(|e| format!("lzop: failed to write u16: {e}"))
}

fn write_u32_be<W: Write>(w: &mut W, v: u32) -> Result<(), String> {
    w.write_all(&v.to_be_bytes())
        .map_err(|e| format!("lzop: failed to write u32: {e}"))
}

// ---- Header parsing with checksum tracking ----

fn read_header_field_u8<R: Read>(r: &mut R, tracked: &mut Vec<u8>) -> Result<u8, String> {
    let mut b = [0u8; 1];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read header: {e}"))?;
    tracked.push(b[0]);
    Ok(b[0])
}

fn read_header_field_u16<R: Read>(r: &mut R, tracked: &mut Vec<u8>) -> Result<u16, String> {
    let mut b = [0u8; 2];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read header: {e}"))?;
    tracked.extend_from_slice(&b);
    Ok(u16::from_be_bytes(b))
}

fn read_header_field_u32<R: Read>(r: &mut R, tracked: &mut Vec<u8>) -> Result<u32, String> {
    let mut b = [0u8; 4];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read header: {e}"))?;
    tracked.extend_from_slice(&b);
    Ok(u32::from_be_bytes(b))
}

fn read_header_field_bytes<R: Read>(
    r: &mut R,
    tracked: &mut Vec<u8>,
    n: usize,
) -> Result<Vec<u8>, String> {
    let mut b = vec![0u8; n];
    r.read_exact(&mut b)
        .map_err(|e| format!("lzop: failed to read header: {e}"))?;
    tracked.extend_from_slice(&b);
    Ok(b)
}

fn read_header<R: Read>(r: &mut R) -> Result<LzopHeader, String> {
    let mut tracked: Vec<u8> = Vec::with_capacity(256);

    let version = read_header_field_u16(r, &mut tracked)?;
    if version < 0x0900 {
        return Err(format!(
            "lzop: unsupported file format version 0x{version:04x}"
        ));
    }

    let lib_version = read_header_field_u16(r, &mut tracked)?;

    let version_needed = if version >= 0x0940 {
        let v = read_header_field_u16(r, &mut tracked)?;
        if v > LZOP_VERSION {
            return Err(format!(
                "lzop: file requires lzop version 0x{v:04x} or newer, this implementation supports up to 0x{LZOP_VERSION:04x}"
            ));
        }
        if v < 0x0900 {
            return Err(format!("lzop: invalid version_needed_to_extract 0x{v:04x}"));
        }
        v
    } else {
        0x0900
    };

    let method = read_header_field_u8(r, &mut tracked)?;
    if method == 0 {
        return Err("lzop: invalid compression method 0".into());
    }

    let level = if version >= 0x0940 {
        read_header_field_u8(r, &mut tracked)?
    } else {
        0
    };

    let flags = read_header_field_u32(r, &mut tracked)?;

    let filter = if flags & F_H_FILTER != 0 {
        read_header_field_u32(r, &mut tracked)?
    } else {
        0
    };

    let mode = read_header_field_u32(r, &mut tracked)?;
    let mode = if flags & F_STDIN != 0 { 0 } else { mode };

    let mtime_low = read_header_field_u32(r, &mut tracked)?;
    let mtime_high = if version >= 0x0940 {
        read_header_field_u32(r, &mut tracked)?
    } else {
        0
    };

    let name_len = read_header_field_u8(r, &mut tracked)? as usize;
    let name_bytes = if name_len > 0 {
        read_header_field_bytes(r, &mut tracked, name_len)?
    } else {
        Vec::new()
    };
    let name = String::from_utf8_lossy(&name_bytes).into_owned();

    // Verify the header checksum before continuing.
    let expected_header_cs = if flags & F_H_CRC32 != 0 {
        crc32(&tracked)
    } else {
        adler32(&tracked)
    };
    let stored_header_cs = read_u32_be(r)?;
    if stored_header_cs != expected_header_cs {
        return Err(format!(
            "lzop: header checksum mismatch: expected 0x{expected_header_cs:08x}, got 0x{stored_header_cs:08x}"
        ));
    }

    // Reject unknown/reserved flag bits.
    if flags & !(F_MASK | F_OS_MASK | F_CS_MASK) != 0 {
        return Err(format!(
            "lzop: unknown header flags 0x{flags:08x} (reserved bits set)"
        ));
    }

    // Skip extra field, but verify its checksum.
    if flags & F_H_EXTRA_FIELD != 0 {
        let mut extra_tracked: Vec<u8> = Vec::new();
        let extra_len = read_header_field_u32(r, &mut extra_tracked)?;
        let _ = read_header_field_bytes(r, &mut extra_tracked, extra_len as usize)?;
        let expected_extra_cs = if flags & F_H_CRC32 != 0 {
            crc32(&extra_tracked)
        } else {
            adler32(&extra_tracked)
        };
        let stored_extra_cs = read_u32_be(r)?;
        if stored_extra_cs != expected_extra_cs {
            return Err(format!(
                "lzop: extra field checksum mismatch: expected 0x{expected_extra_cs:08x}, got 0x{stored_extra_cs:08x}"
            ));
        }
    }

    Ok(LzopHeader {
        version,
        lib_version,
        version_needed,
        method,
        level,
        flags,
        filter,
        mode,
        mtime_low,
        mtime_high,
        name,
    })
}

// ---- Public API ----

pub fn decompress_lzop(input: &[u8]) -> Result<Vec<u8>, String> {
    let mut cursor = Cursor::new(input);
    let mut output = Vec::new();

    let mut magic = [0u8; 9];
    cursor
        .read_exact(&mut magic)
        .map_err(|e| format!("lzop: failed to read magic: {e}"))?;
    if magic != LZOP_MAGIC {
        return Err("lzop: invalid magic number (not a lzop file)".into());
    }

    let header = read_header(&mut cursor)?;

    loop {
        let uncompressed_len = read_u32_be(&mut cursor)?;
        if uncompressed_len == 0 {
            break;
        }
        if uncompressed_len > MAX_BLOCK_SIZE {
            return Err(format!(
                "lzop: block size {uncompressed_len} exceeds maximum {MAX_BLOCK_SIZE} (file corrupted)"
            ));
        }

        let stored_len = read_u32_be(&mut cursor)?;
        if stored_len == 0 || stored_len > uncompressed_len {
            return Err(format!(
                "lzop: invalid stored length {stored_len} (uncompressed length {uncompressed_len})"
            ));
        }

        // Uncompressed-data checksums (always present if their flags are set).
        let d_adler32 = if header.flags & F_ADLER32_D != 0 {
            Some(read_u32_be(&mut cursor)?)
        } else {
            None
        };
        let d_crc32 = if header.flags & F_CRC32_D != 0 {
            Some(read_u32_be(&mut cursor)?)
        } else {
            None
        };

        // Compressed-data checksums exist only when the block was actually
        // compressed (stored_len < uncompressed_len). Otherwise they are
        // copies of the uncompressed checksums, as per lzop's p_lzo.c.
        let (c_adler32, c_crc32) = if stored_len < uncompressed_len {
            let a = if header.flags & F_ADLER32_C != 0 {
                Some(read_u32_be(&mut cursor)?)
            } else {
                None
            };
            let c = if header.flags & F_CRC32_C != 0 {
                Some(read_u32_be(&mut cursor)?)
            } else {
                None
            };
            (a, c)
        } else {
            (d_adler32, d_crc32)
        };

        let mut block_data = vec![0u8; stored_len as usize];
        cursor
            .read_exact(&mut block_data)
            .map_err(|e| format!("lzop: failed to read block data: {e}"))?;

        // Verify the compressed-block checksums.
        if let Some(expected) = c_adler32 {
            let actual = adler32(&block_data);
            if actual != expected {
                return Err(format!(
                    "lzop: compressed block Adler-32 mismatch: expected 0x{expected:08x}, got 0x{actual:08x}"
                ));
            }
        }
        if let Some(expected) = c_crc32 {
            let actual = crc32(&block_data);
            if actual != expected {
                return Err(format!(
                    "lzop: compressed block CRC-32 mismatch: expected 0x{expected:08x}, got 0x{actual:08x}"
                ));
            }
        }

        let decompressed = if stored_len < uncompressed_len {
            let d = lzokay_native::decompress_all(&block_data, Some(uncompressed_len as usize))
                .map_err(|e| format!("lzop: LZO decompression failed: {e:?}"))?;
            if d.len() != uncompressed_len as usize {
                return Err(format!(
                    "lzop: decompressed size mismatch: expected {}, got {}",
                    uncompressed_len,
                    d.len()
                ));
            }
            d
        } else {
            block_data
        };

        // Verify the uncompressed-block checksums.
        if let Some(expected) = d_adler32 {
            let actual = adler32(&decompressed);
            if actual != expected {
                return Err(format!(
                    "lzop: uncompressed block Adler-32 mismatch: expected 0x{expected:08x}, got 0x{actual:08x}"
                ));
            }
        }
        if let Some(expected) = d_crc32 {
            let actual = crc32(&decompressed);
            if actual != expected {
                return Err(format!(
                    "lzop: uncompressed block CRC-32 mismatch: expected 0x{expected:08x}, got 0x{actual:08x}"
                ));
            }
        }

        output.extend_from_slice(&decompressed);
    }

    Ok(output)
}

fn split_mtime(t: std::time::SystemTime) -> (u32, u32) {
    let secs = t
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    (
        (secs & 0xffff_ffff) as u32,
        ((secs >> 32) & 0xffff_ffff) as u32,
    )
}

pub fn compress_lzop(input: &[u8], filename: &str) -> Result<Vec<u8>, String> {
    let mut output = Vec::new();

    output
        .write_all(&LZOP_MAGIC)
        .map_err(|e| format!("lzop: failed to write magic: {e}"))?;

    // Header field values.
    let version = LZOP_VERSION;
    let lib_version = LZOP_LIB_VERSION;
    let version_needed = LZOP_VERSION_NEEDED;
    let method: u8 = M_LZO1X_999;
    let level: u8 = 9;
    let flags: u32 = F_ADLER32_D | F_ADLER32_C | F_OS_UNIX;
    let mode: u32 = 0o100_700;
    let (mtime_low, mtime_high) = split_mtime(std::time::SystemTime::now());

    let name_bytes = filename.as_bytes();
    let name_len = name_bytes.len().min(255);

    // Build the header payload (everything covered by the header checksum).
    let mut header_bytes: Vec<u8> = Vec::with_capacity(64 + name_len);
    header_bytes.extend_from_slice(&version.to_be_bytes());
    header_bytes.extend_from_slice(&lib_version.to_be_bytes());
    header_bytes.extend_from_slice(&version_needed.to_be_bytes());
    header_bytes.push(method);
    header_bytes.push(level);
    header_bytes.extend_from_slice(&flags.to_be_bytes());
    header_bytes.extend_from_slice(&mode.to_be_bytes());
    header_bytes.extend_from_slice(&mtime_low.to_be_bytes());
    header_bytes.extend_from_slice(&mtime_high.to_be_bytes());
    header_bytes.push(name_len as u8);
    header_bytes.extend_from_slice(&name_bytes[..name_len]);

    output
        .write_all(&header_bytes)
        .map_err(|e| format!("lzop: failed to write header: {e}"))?;

    // Header checksum: Adler-32 because F_H_CRC32 is not set.
    write_u32_be(&mut output, adler32(&header_bytes))?;

    // Compress data in blocks.
    for chunk in input.chunks(BLOCK_SIZE) {
        let uncompressed_len = chunk.len() as u32;

        let compressed = lzokay_native::compress(chunk)
            .map_err(|e| format!("lzop: LZO compression failed: {e:?}"))?;
        let compressed_len = compressed.len() as u32;

        let (stored, stored_len) = if compressed_len < uncompressed_len {
            (compressed.as_slice(), compressed_len)
        } else {
            (chunk, uncompressed_len)
        };

        write_u32_be(&mut output, uncompressed_len)?;
        write_u32_be(&mut output, stored_len)?;

        // Uncompressed checksums.
        if flags & F_ADLER32_D != 0 {
            write_u32_be(&mut output, adler32(chunk))?;
        }
        if flags & F_CRC32_D != 0 {
            write_u32_be(&mut output, crc32(chunk))?;
        }

        // Compressed checksums, only when the block was actually compressed.
        if stored_len < uncompressed_len {
            if flags & F_ADLER32_C != 0 {
                write_u32_be(&mut output, adler32(stored))?;
            }
            if flags & F_CRC32_C != 0 {
                write_u32_be(&mut output, crc32(stored))?;
            }
        }

        output
            .write_all(stored)
            .map_err(|e| format!("lzop: failed to write block data: {e}"))?;
    }

    // EOF marker.
    write_u32_be(&mut output, 0)?;

    Ok(output)
}

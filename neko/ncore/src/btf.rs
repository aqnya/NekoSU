// SPDX-License-Identifier: GPL-3.0-or-later
//! Minimal BTF reader for the two `struct mount` offsets the kernel needs.
//!
//! Kernel modules cannot include `fs/mount.h` portably, and hard-coded
//! offsets break across GKI versions.  The kernel ships its own BTF at
//! `/sys/kernel/btf/vmlinux`; parse just enough of it to find
//! `struct mount::mnt_id` and `struct mount::mnt`.

use anyhow::{Context, Result, bail};

const BTF_MAGIC: u16 = 0xeb9f;
const BTF_KIND_STRUCT: u32 = 4;

#[derive(Debug, Clone, Copy)]
pub struct MountLayout {
    pub mnt_id_off: u32,
    pub mnt_off: u32,
}

pub fn read_mount_layout() -> Result<MountLayout> {
    let data = std::fs::read("/sys/kernel/btf/vmlinux").context("read /sys/kernel/btf/vmlinux")?;
    parse(&data)
}

fn u16_at(data: &[u8], off: usize) -> Result<u16> {
    let bytes = data
        .get(off..off + 2)
        .with_context(|| format!("BTF read u16 at {off}"))?;
    Ok(u16::from_le_bytes([bytes[0], bytes[1]]))
}

fn u32_at(data: &[u8], off: usize) -> Result<u32> {
    let bytes = data
        .get(off..off + 4)
        .with_context(|| format!("BTF read u32 at {off}"))?;
    Ok(u32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

fn cstr_at(data: &[u8], start: usize, end: usize, off: u32) -> Result<&str> {
    let begin = start
        .checked_add(off as usize)
        .with_context(|| format!("BTF string offset overflow: {off}"))?;
    if begin >= end || end > data.len() {
        bail!("BTF string outside string table: off={off}");
    }
    let tail = &data[begin..end];
    let len = tail
        .iter()
        .position(|&b| b == 0)
        .context("unterminated BTF string")?;
    std::str::from_utf8(&tail[..len]).context("BTF string is not UTF-8")
}

fn member_extra(kind: u32, vlen: usize) -> Result<usize> {
    let fixed = match kind {
        0 | 2 | 7 | 8 | 9 | 10 | 11 | 12 | 16 | 18 => 0,
        1 => 4,
        3 => 12,
        14 => 4,
        17 => 4,
        4 | 5 | 15 => 12usize
            .checked_mul(vlen)
            .context("BTF struct member size overflow")?,
        6 | 13 => 8usize
            .checked_mul(vlen)
            .context("BTF enum/proto size overflow")?,
        19 => 12usize
            .checked_mul(vlen)
            .context("BTF enum64 size overflow")?,
        other => bail!("unsupported BTF kind {other}"),
    };
    Ok(fixed)
}

fn parse(data: &[u8]) -> Result<MountLayout> {
    if data.len() < 24 {
        bail!("BTF image is too short");
    }
    if u16_at(data, 0)? != BTF_MAGIC {
        bail!("not a BTF image");
    }
    let hdr_len = u32_at(data, 4)? as usize;
    let type_off = u32_at(data, 8)? as usize;
    let type_len = u32_at(data, 12)? as usize;
    let str_off = u32_at(data, 16)? as usize;
    let str_len = u32_at(data, 20)? as usize;

    let type_start = hdr_len
        .checked_add(type_off)
        .context("BTF type section overflow")?;
    let type_end = type_start
        .checked_add(type_len)
        .context("BTF type length overflow")?;
    let str_start = hdr_len
        .checked_add(str_off)
        .context("BTF string section overflow")?;
    let str_end = str_start
        .checked_add(str_len)
        .context("BTF string length overflow")?;
    if type_end > data.len() || str_end > data.len() {
        bail!("BTF sections exceed file size");
    }

    let mut pos = type_start;
    let mut mnt_id_off = None;
    let mut mnt_off = None;

    while pos < type_end {
        let name_off = u32_at(data, pos)?;
        let info = u32_at(data, pos + 4)?;
        let kind = (info >> 24) & 0x1f;
        let vlen = (info & 0xffff) as usize;
        let name = cstr_at(data, str_start, str_end, name_off)?;

        if kind == BTF_KIND_STRUCT && name == "mount" {
            let members = pos.checked_add(12).context("BTF member table overflow")?;
            for index in 0..vlen {
                let at = members + index * 12;
                let m_name_off = u32_at(data, at)?;
                let m_offset = u32_at(data, at + 8)?;
                let m_name = cstr_at(data, str_start, str_end, m_name_off)?;
                // Bitfield members pack the bit offset in the low 24 bits.
                let bit_off = (m_offset & 0x00ff_ffff) as usize;
                if bit_off % 8 != 0 {
                    continue;
                }
                let byte_off = u32::try_from(bit_off / 8)
                    .context("BTF mount member offset does not fit u32")?;
                match m_name {
                    "mnt_id" => mnt_id_off = Some(byte_off),
                    "mnt" => mnt_off = Some(byte_off),
                    _ => {}
                }
            }
            if let (Some(id), Some(mnt)) = (mnt_id_off, mnt_off) {
                return Ok(MountLayout {
                    mnt_id_off: id,
                    mnt_off: mnt,
                });
            }
        }

        let extra = member_extra(kind, vlen)?;
        pos = pos
            .checked_add(12)
            .and_then(|v| v.checked_add(extra))
            .context("BTF type table overflow")?;
    }

    bail!("BTF has no struct mount with mnt_id/mnt members (id={mnt_id_off:?}, mnt={mnt_off:?})")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rejects_garbage() {
        assert!(parse(&[0u8; 32]).is_err());
    }
}

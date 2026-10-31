// SPDX-License-Identifier: GPL-3.0-or-later
//! Minimal newc (`070701`) cpio reader/writer.
//!
//! Android ramdisks are newc archives, sometimes several of them concatenated
//! in one block.  Parsing merges every archive it finds and writing always
//! produces a single archive with a trailer.

use std::collections::BTreeMap;

use anyhow::{Context, Result, bail, ensure};

use crate::utils::{align_to, trim_nul};

const MAGIC: &[u8] = b"070701";
const TRAILER: &str = "TRAILER!!!";
const HEADER_LEN: usize = 110;

pub const TYPE_MASK: u32 = 0o170000;
pub const TYPE_FIFO: u32 = 0o010000;
pub const TYPE_CHAR: u32 = 0o020000;
pub const TYPE_DIR: u32 = 0o040000;
pub const TYPE_BLOCK: u32 = 0o060000;
pub const TYPE_REGULAR: u32 = 0o100000;
pub const TYPE_SYMLINK: u32 = 0o120000;

/// A newc archive's entries, keyed by path.
pub struct Cpio {
    entries: BTreeMap<String, CpioEntry>,
}

#[derive(Debug, Clone)]
pub struct CpioEntry {
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
    pub rdev_major: u32,
    pub rdev_minor: u32,
    data: Option<Vec<u8>>,
}

impl Default for Cpio {
    fn default() -> Self {
        Self::new()
    }
}

impl Cpio {
    pub fn new() -> Self {
        Self {
            entries: BTreeMap::new(),
        }
    }

    /// Parse every newc archive contained in `data`.
    pub fn load_from_data(data: &[u8]) -> Result<Self> {
        let mut entries = BTreeMap::new();
        let mut pos = 0;

        loop {
            let Some(header) = data.get(pos..pos + HEADER_LEN) else {
                break;
            };
            if !header.starts_with(MAGIC) {
                bail!("unsupported cpio header at offset {pos}");
            }

            let mode = hex_field(&header[14..22])?;
            let uid = hex_field(&header[22..30])?;
            let gid = hex_field(&header[30..38])?;
            let file_size = hex_field(&header[54..62])? as usize;
            let rdev_major = hex_field(&header[78..86])?;
            let rdev_minor = hex_field(&header[86..94])?;
            let name_len = hex_field(&header[94..102])? as usize;

            let name_start = pos + HEADER_LEN;
            let name_end = name_start
                .checked_add(name_len)
                .context("cpio entry name overflows")?;
            let raw_name = data
                .get(name_start..name_end)
                .context("truncated cpio entry name")?;
            let name = String::from_utf8_lossy(trim_nul(raw_name)).into_owned();

            let data_start = align_to(name_end, 4);
            if name == TRAILER {
                // Concatenated archives: keep going at the next newc magic.
                match find_magic(data, data_start) {
                    Some(next) => {
                        pos = next;
                        continue;
                    }
                    None => break,
                }
            }

            let data_end = data_start
                .checked_add(file_size)
                .context("cpio entry data overflows")?;
            let body = data
                .get(data_start..data_end)
                .context("truncated cpio entry data")?;

            pos = align_to(data_end, 4);

            if name == "." || name == ".." {
                continue;
            }

            entries.insert(
                normalize(&name),
                CpioEntry {
                    mode,
                    uid,
                    gid,
                    rdev_major,
                    rdev_minor,
                    data: (file_size > 0).then(|| body.to_vec()),
                },
            );
        }

        Ok(Self { entries })
    }

    /// Serialise the archive as a single newc stream.
    pub fn dump(&self) -> Vec<u8> {
        let mut out = Vec::new();
        let mut inode = 300_000u32;

        for (name, entry) in &self.entries {
            write_entry(
                &mut out,
                inode,
                name,
                entry.mode,
                entry.uid,
                entry.gid,
                entry.rdev_major,
                entry.rdev_minor,
                entry.data.as_deref().unwrap_or(&[]),
            );
            inode += 1;
        }

        write_entry(&mut out, inode, TRAILER, 0, 0, 0, 0, 0, &[]);
        out
    }

    pub fn entries(&self) -> &BTreeMap<String, CpioEntry> {
        &self.entries
    }

    pub fn get(&self, path: &str) -> Option<&CpioEntry> {
        self.entries.get(&normalize(path))
    }

    pub fn exists(&self, path: &str) -> bool {
        self.get(path).is_some()
    }

    pub fn add(&mut self, path: &str, entry: CpioEntry) -> Result<()> {
        ensure!(
            !path.ends_with('/'),
            "cpio path cannot end with '/': {path}"
        );
        self.entries.insert(normalize(path), entry);
        Ok(())
    }

    pub fn remove(&mut self, path: &str, recursive: bool) -> bool {
        let path = normalize(path);
        let removed = self.entries.remove(&path).is_some();
        if recursive {
            let prefix = format!("{path}/");
            self.entries.retain(|name, _| !name.starts_with(&prefix));
        }
        removed
    }
}

impl CpioEntry {
    pub fn regular(mode: u32, data: Vec<u8>) -> Self {
        Self {
            mode: mode | TYPE_REGULAR,
            uid: 0,
            gid: 0,
            rdev_major: 0,
            rdev_minor: 0,
            data: Some(data),
        }
    }

    pub fn dir(mode: u32) -> Self {
        Self {
            mode: mode | TYPE_DIR,
            uid: 0,
            gid: 0,
            rdev_major: 0,
            rdev_minor: 0,
            data: None,
        }
    }

    pub fn symlink(mode: u32, target: &str) -> Self {
        Self {
            mode: mode | TYPE_SYMLINK,
            uid: 0,
            gid: 0,
            rdev_major: 0,
            rdev_minor: 0,
            data: Some(normalize(target).into_bytes()),
        }
    }

    pub fn data(&self) -> Option<&[u8]> {
        self.data.as_deref()
    }

    pub fn len(&self) -> usize {
        self.data.as_ref().map_or(0, Vec::len)
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    pub fn is_dir(&self) -> bool {
        self.mode & TYPE_MASK == TYPE_DIR
    }
}

fn hex_field(bytes: &[u8]) -> Result<u32> {
    let text = std::str::from_utf8(bytes).context("non-utf8 cpio header field")?;
    u32::from_str_radix(text, 16).with_context(|| format!("invalid cpio hex field {text:?}"))
}

fn find_magic(data: &[u8], from: usize) -> Option<usize> {
    let rest = data.get(from..)?;
    rest.windows(MAGIC.len())
        .position(|window| window == MAGIC)
        .map(|offset| from + offset)
}

/// Strip leading `./` and `/` and collapse repeated separators.
fn normalize(path: &str) -> String {
    path.split('/')
        .filter(|component| !component.is_empty() && *component != ".")
        .collect::<Vec<_>>()
        .join("/")
}

fn write_entry(
    out: &mut Vec<u8>,
    inode: u32,
    name: &str,
    mode: u32,
    uid: u32,
    gid: u32,
    rdev_major: u32,
    rdev_minor: u32,
    data: &[u8],
) {
    let header = format!(
        "070701{inode:08x}{mode:08x}{uid:08x}{gid:08x}{nlink:08x}{mtime:08x}{size:08x}\
         {devmajor:08x}{devminor:08x}{rdev_major:08x}{rdev_minor:08x}{namesize:08x}{check:08x}",
        nlink = 1,
        mtime = 0,
        size = data.len(),
        devmajor = 0,
        devminor = 0,
        namesize = name.len() + 1,
        check = 0,
    );
    debug_assert_eq!(header.len(), HEADER_LEN);

    out.extend_from_slice(header.as_bytes());
    out.extend_from_slice(name.as_bytes());
    out.push(0);
    pad4(out);
    out.extend_from_slice(data);
    pad4(out);
}

fn pad4(out: &mut Vec<u8>) {
    while out.len() % 4 != 0 {
        out.push(0);
    }
}

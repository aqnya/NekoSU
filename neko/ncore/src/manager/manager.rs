// SPDX-License-Identifier: GPL-3.0-or-later
//!
//! Manager scan — user-space port of `manager/manager.c`.
//!

use anyhow::{Context, Result, anyhow, bail};
use sha2::{Digest, Sha256};
use std::fs;
use std::path::Path;

use super::abx::{self, AbxOutcome, Package};

pub const MANAGER_CERT_HASH_LEN: usize = 32;
pub const PACKAGES_XML_PATH: &str = "/data/system/packages.xml";
pub const PACKAGES_LIST_PATH: &str = "/data/system/packages.list";
pub const MAX_PACKAGES_XML_SIZE: u64 = 8 * 1024 * 1024;

pub type CertHash = [u8; MANAGER_CERT_HASH_LEN];

pub fn parse_cert_hash(hex: &str) -> Result<CertHash> {
    if hex.len() != MANAGER_CERT_HASH_LEN * 2 {
        bail!(
            "cert hash must be {} hex characters, got {}",
            MANAGER_CERT_HASH_LEN * 2,
            hex.len()
        );
    }
    let mut out = [0u8; MANAGER_CERT_HASH_LEN];
    let bytes = hex.as_bytes();
    for i in 0..MANAGER_CERT_HASH_LEN {
        let hi =
            hex_nibble(bytes[2 * i]).ok_or_else(|| anyhow!("invalid hex at position {}", 2 * i))?;
        let lo = hex_nibble(bytes[2 * i + 1])
            .ok_or_else(|| anyhow!("invalid hex at position {}", 2 * i + 1))?;
        out[i] = (hi << 4) | lo;
    }
    Ok(out)
}

fn hex_nibble(b: u8) -> Option<u8> {
    match b {
        b'0'..=b'9' => Some(b - b'0'),
        b'a'..=b'f' => Some(b - b'a' + 10),
        b'A'..=b'F' => Some(b - b'A' + 10),
        _ => None,
    }
}

pub fn verify_package_signature(packages_xml: &Path, cert_hash: &CertHash) -> Result<Package> {
    let meta =
        fs::metadata(packages_xml).with_context(|| format!("stat {}", packages_xml.display()))?;
    let fsize = meta.len();
    if fsize < 4 || fsize > MAX_PACKAGES_XML_SIZE {
        bail!("invalid {} size: {}", packages_xml.display(), fsize);
    }

    let buf = fs::read(packages_xml).with_context(|| format!("read {}", packages_xml.display()))?;

    match abx::find_package_by_cert(&buf, |cert| {
        let hash = Sha256::digest(cert);
        hash[..] == cert_hash[..]
    }) {
        AbxOutcome::Found(pkg) => Ok(pkg),
        AbxOutcome::NotFound => {
            bail!("no package signed with the manager certificate")
        }
        AbxOutcome::ParseError => {
            bail!("cannot parse {}", packages_xml.display())
        }
    }
}

pub fn get_uid_from_packages_list(packages_list: &Path, package_name: &str) -> Option<u32> {
    let buf = fs::read_to_string(packages_list).ok()?;
    for line in buf.lines() {
        let mut parts = line.split(' ');
        if parts.next() != Some(package_name) {
            continue;
        }
        if let Some(uid_str) = parts.next() {
            if let Ok(uid) = uid_str.parse::<u32>() {
                return Some(uid);
            }
        }
    }
    None
}

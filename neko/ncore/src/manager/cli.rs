use crate::defs;
use crate::ksucalls::set_manager_uid;
use crate::manager;
use anyhow::{Result, anyhow};
use std::path::PathBuf;

/// `packages.list` always sits next to `packages.xml`; keep them together
/// even when the caller overrides the path for testing.
pub fn resolve_packages_paths(packages_xml: Option<String>) -> (PathBuf, PathBuf) {
    let xml = packages_xml
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(manager::PACKAGES_XML_PATH));
    let list = xml
        .parent()
        .map(|d| d.join("packages.list"))
        .unwrap_or_else(|| PathBuf::from(manager::PACKAGES_LIST_PATH));
    (xml, list)
}

pub fn run_manager_scan(cert_hash: &str, packages_xml: Option<String>) -> Result<()> {
    let hash = manager::parse_cert_hash(cert_hash)?;
    let (xml, list) = resolve_packages_paths(packages_xml);

    let pkg = manager::verify_package_signature(&xml, &hash)?;
    let uid = if pkg.has_uid {
        pkg.uid
    } else {
        manager::get_uid_from_packages_list(&list, &pkg.name)
            .ok_or_else(|| anyhow!("could not find UID for {}", pkg.name))?
    };

    log::info!("Manager package: {}", pkg.name);
    log::info!("  uid:         {}", uid);
    log::info!("  codePath:    {}", pkg.code_path);
    log::info!("  nativeLib:   {}", pkg.native_lib_path);
    log::info!("  cert length: {} bytes", pkg.cert.len());
    set_manager_uid(uid)?;
    Ok(())
}

pub fn resolve_cert_hash(cli: String) -> Result<String> {
    let cli = cli.trim();
    if !cli.is_empty() {
        return Ok(cli.to_string());
    }

    let pinned = defs::MANAGER_CERT_HASH.trim();
    if !pinned.is_empty() {
        return Ok(pinned.to_string());
    }

    Err(anyhow!(
        "manager cert hash is required: pass it as an argument, \
         or set NKSU_MANAGER_CERTHASH at build time"
    ))
}

/// Best-effort manager scan for the boot stages.
///
/// The kernel verifies this executable against the ncore signing key before
/// accepting the UID, so only a genuine ncore can authorize the manager.  A
/// missing manager or an unreadable packages.xml must never abort the boot,
/// hence the errors are only logged.
pub fn run_boot_scan() {
    let hash = match resolve_cert_hash(String::new()) {
        Ok(hash) => hash,
        Err(e) => {
            log::warn!("manager scan skipped: {e:#}");
            return;
        }
    };

    match run_manager_scan(&hash, None) {
        Ok(()) => log::info!("manager authorized through the kernel"),
        Err(e) => log::warn!("manager scan failed: {e:#}"),
    }
}

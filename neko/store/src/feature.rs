use kzcall::*;
use proto::{FeatureEntry, FeatureStore};
use std::ffi::OsString;
use std::fs;
use std::io;
use std::io::Write;
use std::path::Path;

use crate::SAVE_LOCK;
use crate::root::ensure_root;
const FEATURE_PATH: &str = "/data/adb/nksu/allowed.feature";

pub fn save_feature(entry: &FeatureEntry) -> io::Result<()> {
    ensure_root()?;
    let _guard = SAVE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let mut store = read_feature_store()?;

    let existing = store.entries.iter_mut().find(|e| e.id == entry.id);

    let changed = match existing {
        Some(slot) => {
            if *slot == *entry {
                false
            } else {
                *slot = entry.clone();
                true
            }
        }
        None => {
            store.entries.push(entry.clone());
            true
        }
    };

    if !changed {
        log::debug!("feature save: no changes (id={})", entry.id);
        return Ok(());
    }

    store.total = u32::try_from(store.entries.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("too many feature entries: {}", store.entries.len()),
        )
    })?;

    write_feature_store(&store)?;
    log::info!(
        "feature saved: id={} value={}, total={}",
        entry.id,
        entry.value,
        store.entries.len()
    );
    Ok(())
}

pub fn del_feature(id: i32) -> io::Result<()> {
    ensure_root()?;
    let _guard = SAVE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let mut store = read_feature_store()?;

    let before = store.entries.len();
    store.entries.retain(|e| e.id != id);

    if store.entries.len() == before {
        log::debug!("feature del: id={id} not found");
        return Ok(());
    }

    store.total = u32::try_from(store.entries.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("too many feature entries: {}", store.entries.len()),
        )
    })?;

    write_feature_store(&store)?;
    log::info!("feature deleted: id={id}, total={}", store.entries.len());
    Ok(())
}

pub fn load_feature() -> io::Result<i32> {
    let path = Path::new(FEATURE_PATH);
    if !path.exists() {
        return Ok(0);
    }

    let bytes = fs::read(path)?;
    let store = FeatureStore::from_bytes(&bytes)
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

    if store.entries.len() != store.total as usize {
        log::warn!(
            "feature store count mismatch: entries={} total={}",
            store.entries.len(),
            store.total,
        );
    }

    let total = store.entries.len();
    let mut failed = 0usize;

    for entry in &store.entries {
        match feature_set(entry) {
            Ok(()) => {}
            Err(e) => {
                failed += 1;
                log::error!(
                    "feature_set failed: id={} value={}: {e:#}",
                    entry.id,
                    entry.value,
                );
            }
        }
    }

    if failed == 0 {
        log::info!("feature loaded: {total} entries");
        Ok(0)
    } else {
        log::warn!(
            "feature load done: {}/{total} succeeded, {failed} failed",
            total - failed,
        );
        Ok(-1)
    }
}

pub fn read_feature_store() -> io::Result<FeatureStore> {
    let path = Path::new(FEATURE_PATH);
    if !path.exists() {
        return Ok(FeatureStore::default());
    }
    let bytes = fs::read(path)?;
    let store = FeatureStore::from_bytes(&bytes).map_err(|e| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            format!("decode feature store: {e}"),
        )
    })?;
    if store.entries.len() != store.total as usize {
        log::warn!(
            "feature store count mismatch: entries={} total={}",
            store.entries.len(),
            store.total,
        );
    }
    Ok(store)
}

pub fn write_feature_store(store: &FeatureStore) -> io::Result<()> {
    let bytes = store.to_bytes();

    let path = Path::new(FEATURE_PATH);
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }

    let mut tmp_name: OsString = path
        .file_name()
        .map(|n| n.to_os_string())
        .unwrap_or_else(|| OsString::from("feature"));
    tmp_name.push(".tmp");
    let tmp = path.with_file_name(tmp_name);

    let write_result = (|| -> io::Result<()> {
        let mut f = fs::File::create(&tmp)?;
        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            f.set_permissions(fs::Permissions::from_mode(0o600))?;
        }
        f.write_all(&bytes)?;
        f.sync_all()?;
        Ok(())
    })();

    if let Err(e) = write_result {
        let _ = fs::remove_file(&tmp);
        log::error!("save feature: write tmp failed: {e:#}");
        return Err(e);
    }

    if let Err(e) = fs::rename(&tmp, path) {
        let _ = fs::remove_file(&tmp);
        log::error!("save feature: rename failed: {e:#}");
        return Err(e);
    }

    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            if let Ok(dir) = fs::File::open(parent) {
                let _ = dir.sync_all();
            }
        }
    }
    Ok(())
}

use kzcall::*;
use proto::{ProfileEntry, ProfileStore};
use std::ffi::OsString;
use std::fs;
use std::io;
use std::io::Write;
use std::path::Path;

use crate::SAVE_LOCK;
use crate::root::ensure_root;
const PROFILE_PATH: &str = "/data/adb/nksu/allowed.profile";

pub fn save(entry: &ProfileEntry) -> io::Result<()> {
    ensure_root()?;
    let _guard = SAVE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let mut store = read_store()?;

    let existing = store.entries.iter_mut().find(|e| e.uid == entry.uid);

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
        log::debug!("profile save: no changes (uid={})", entry.uid);
        return Ok(());
    }

    store.total = u32::try_from(store.entries.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("too many profile entries: {}", store.entries.len()),
        )
    })?;

    write_store(&store)?;
    log::info!(
        "profile saved: uid={} domain={:?}, total={}",
        entry.uid,
        entry.domain,
        store.entries.len()
    );
    Ok(())
}

pub fn del(uid: u32) -> io::Result<()> {
    ensure_root()?;
    let _guard = SAVE_LOCK.lock().unwrap_or_else(|e| e.into_inner());

    let mut store = read_store()?;

    let before = store.entries.len();
    store.entries.retain(|e| e.uid != uid);

    if store.entries.len() == before {
        log::debug!("profile del: uid={uid} not found");
        return Ok(());
    }

    store.total = u32::try_from(store.entries.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("too many profile entries: {}", store.entries.len()),
        )
    })?;

    write_store(&store)?;
    log::info!("profile deleted: uid={uid}, total={}", store.entries.len());
    Ok(())
}

pub fn load() -> io::Result<i32> {
    let path = Path::new(PROFILE_PATH);
    if !path.exists() {
        return Ok(0);
    }

    let bytes = fs::read(path)?;
    let store = ProfileStore::from_bytes(&bytes)
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;

    if store.entries.len() != store.total as usize {
        log::warn!(
            "profile store count mismatch: entries={} total={}",
            store.entries.len(),
            store.total,
        );
    }

    let total = store.entries.len();
    let mut failed = 0usize;

    for entry in &store.entries {
        match set_profile(entry) {
            Ok(()) => {}
            Err(e) => {
                failed += 1;
                log::error!(
                    "set_profile failed: uid={} domain={:?}: {e:#}",
                    entry.uid,
                    entry.domain,
                );
            }
        }
    }

    if failed == 0 {
        log::info!("profile loaded: {total} entries");
        Ok(0)
    } else {
        log::warn!(
            "profile load done: {}/{total} succeeded, {failed} failed",
            total - failed,
        );
        Ok(-1)
    }
}

fn read_store() -> io::Result<ProfileStore> {
    let path = Path::new(PROFILE_PATH);
    if !path.exists() {
        return Ok(ProfileStore::default());
    }
    let bytes = fs::read(path)?;
    let store = ProfileStore::from_bytes(&bytes).map_err(|e| {
        io::Error::new(
            io::ErrorKind::InvalidData,
            format!("decode profile store: {e}"),
        )
    })?;
    if store.entries.len() != store.total as usize {
        log::warn!(
            "profile store count mismatch: entries={} total={}",
            store.entries.len(),
            store.total,
        );
    }
    Ok(store)
}

fn write_store(store: &ProfileStore) -> io::Result<()> {
    let bytes = store.to_bytes();

    let path = Path::new(PROFILE_PATH);
    if let Some(parent) = path.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }

    let mut tmp_name: OsString = path
        .file_name()
        .map(|n| n.to_os_string())
        .unwrap_or_else(|| OsString::from("profile"));
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
        log::error!("save profile: write tmp failed: {e:#}");
        return Err(e);
    }

    if let Err(e) = fs::rename(&tmp, path) {
        let _ = fs::remove_file(&tmp);
        log::error!("save profile: rename failed: {e:#}");
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

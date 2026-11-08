use kzcall::*;
use proto::ProfileEntry;
use std::io;

use crate::db;

pub fn save(entry: &ProfileEntry) -> io::Result<()> {
    match db::save_profile(entry)? {
        Some(total) => log::info!(
            "profile saved: uid={} domain={:?}, total={total}",
            entry.uid,
            entry.domain,
        ),
        None => log::debug!("profile save: no changes (uid={})", entry.uid),
    }
    Ok(())
}

pub fn del(uid: u32) -> io::Result<()> {
    match db::del_profile(uid)? {
        Some(total) => log::info!("profile deleted: uid={uid}, total={total}"),
        None => log::debug!("profile del: uid={uid} not found"),
    }
    Ok(())
}

pub fn load() -> io::Result<()> {
    let store = db::read()?;
    load_entries(&store.profiles)
}

pub(crate) fn load_entries(entries: &[ProfileEntry]) -> io::Result<()> {
    let total = entries.len();
    let mut failed = 0usize;

    for entry in entries {
        if let Err(e) = set_profile(entry) {
            failed += 1;
            log::error!(
                "set_profile failed: uid={} domain={:?}: {e:#}",
                entry.uid,
                entry.domain,
            );
        }
    }

    db::finish_load("profile", total, failed)
}

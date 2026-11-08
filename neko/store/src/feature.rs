use kzcall::*;
use proto::{FeatureEntry, FeatureStore};
use std::io;

use crate::db;

pub fn save_feature(entry: &FeatureEntry) -> io::Result<()> {
    match db::save_feature(entry)? {
        Some(total) => log::info!(
            "feature saved: id={} value={}, total={total}",
            entry.id,
            entry.value,
        ),
        None => log::debug!("feature save: no changes (id={})", entry.id),
    }
    Ok(())
}

pub fn del_feature(id: i32) -> io::Result<()> {
    match db::del_feature(id)? {
        Some(total) => log::info!("feature deleted: id={id}, total={total}"),
        None => log::debug!("feature del: id={id} not found"),
    }
    Ok(())
}

pub fn load_feature() -> io::Result<()> {
    let store = db::read()?;
    load_entries(&store.features)
}

pub(crate) fn load_entries(entries: &[FeatureEntry]) -> io::Result<()> {
    let total = entries.len();
    let mut failed = 0usize;

    for entry in entries {
        if let Err(e) = feature_set(entry) {
            failed += 1;
            log::error!(
                "feature_set failed: id={} value={}: {e:#}",
                entry.id,
                entry.value
            );
        }
    }

    db::finish_load("feature", total, failed)
}

pub fn read_feature_store() -> io::Result<FeatureStore> {
    let store = db::read()?;
    let total = u32::try_from(store.features.len()).map_err(|_| {
        io::Error::new(
            io::ErrorKind::InvalidInput,
            format!("too many feature entries: {}", store.features.len()),
        )
    })?;
    Ok(FeatureStore {
        entries: store.features,
        total,
    })
}

pub fn write_feature_store(store: &FeatureStore) -> io::Result<()> {
    if store.total as usize != store.entries.len() {
        log::warn!(
            "feature write: count mismatch: entries={} total={}",
            store.entries.len(),
            store.total,
        );
    }
    db::replace_features(store.entries.clone())
}

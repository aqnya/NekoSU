use crate::defs::ANTIBRICK_PATH;
use std::fs;
use std::io::{Read, Write};
use std::path::Path;

pub fn write_count(value: u64, increment: bool) -> u64 {
    let path = Path::new(ANTIBRICK_PATH);

    if let Some(parent) = path.parent() {
        if !parent.exists() {
            let _ = fs::create_dir_all(parent);
        }
    }

    let new_value = if increment {
        read_count().saturating_add(1)
    } else {
        value
    };

    let tmp = path.with_extension("tmp");
    let write_ok = (|| -> std::io::Result<()> {
        let mut f = fs::File::create(&tmp)?;
        write!(f, "{}", new_value)?;
        f.sync_all()?;
        Ok(())
    })()
    .is_ok();

    if write_ok {
        let _ = fs::rename(&tmp, path);
    } else {
        let _ = fs::remove_file(&tmp);
    }

    new_value
}

fn read_count() -> u64 {
    let mut s = String::new();
    match fs::File::open(ANTIBRICK_PATH).and_then(|mut f| f.read_to_string(&mut s)) {
        Ok(_) => {
            let trimmed = s.trim();
            if trimmed.is_empty() {
                0
            } else {
                trimmed.parse::<u64>().unwrap_or(0)
            }
        }
        Err(_) => 0,
    }
}

pub fn check_count(threshold: u64) -> bool {
    read_count() >= threshold
}

pub fn on_boot_completed() {
    let _ = fs::remove_file(ANTIBRICK_PATH);
}
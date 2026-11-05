pub mod feature;
pub mod profile;

mod root;
use std::sync::Mutex;

static SAVE_LOCK: Mutex<()> = Mutex::new(());

pub fn load() -> std::io::Result<i32> {
    profile::load()?;
    feature::load_feature()?;
    Ok(0)
}

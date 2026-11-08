pub mod feature;
pub mod profile;

mod db;
mod root;

pub fn load() -> std::io::Result<()> {
    let store = db::read()?;
    let profile = profile::load_entries(&store.profiles);
    let feature = feature::load_entries(&store.features);

    match (profile, feature) {
        (Ok(()), Ok(())) => Ok(()),
        (Err(e), Ok(())) | (Ok(()), Err(e)) => Err(e),
        (Err(profile), Err(feature)) => {
            log::error!("feature load also failed: {feature}");
            Err(profile)
        }
    }
}

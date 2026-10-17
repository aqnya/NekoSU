// SPDX-License-Identifier: GPL-3.0-or-later
// Host builds only exist so `cargo build` can type-check the shared crates.
pub fn run() -> anyhow::Result<()> {
    println!("ncore can only run on Android");
    Ok(())
}

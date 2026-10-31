// SPDX-License-Identifier: GPL-3.0-or-later
use bindgen::callbacks::{DeriveInfo, ParseCallbacks};
use std::env;
use std::path::PathBuf;

/// Adds the zerocopy traits to the packed AOSP structs so they can be read
/// from and written to byte slices without a manual field-by-field copy.
#[derive(Debug)]
struct ZerocopyDerives;

impl ParseCallbacks for ZerocopyDerives {
    fn add_derives(&self, info: &DeriveInfo<'_>) -> Vec<String> {
        let name = info.name;
        let packed_struct = name.starts_with("boot_img_hdr_")
            || name.starts_with("vendor_boot_img_hdr_")
            || name == "vendor_ramdisk_table_entry_v4";
        if packed_struct {
            vec![
                "zerocopy::FromBytes".into(),
                "zerocopy::IntoBytes".into(),
                "zerocopy::Immutable".into(),
                "zerocopy::KnownLayout".into(),
            ]
        } else {
            vec![]
        }
    }
}

fn main() {
    println!("cargo:rerun-if-changed=bindgen/bootimg.h");
    println!("cargo:rerun-if-changed=bindgen/bootimg.hpp");

    let bindings = bindgen::Builder::default()
        .header("bindgen/bootimg.hpp")
        .parse_callbacks(Box::new(ZerocopyDerives))
        .generate()
        .expect("Unable to generate bindings");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_path.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

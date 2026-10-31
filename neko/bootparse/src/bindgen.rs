// SPDX-License-Identifier: GPL-3.0-or-later

//! The bindgen output for `bindgen/bootimg.hpp`.
//!
//! The bindings are generated into `OUT_DIR` by `build.rs`; this module only
//! hides the AOSP naming (which is deliberately non-Rust) from the lints.

#[allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code
)]
pub mod bindings {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

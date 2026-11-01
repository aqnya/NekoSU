// SPDX-License-Identifier: GPL-3.0-or-later

#[allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code,
    unsafe_op_in_unsafe_fn
)]
mod inner {
    include!(concat!(env!("OUT_DIR"), "/ioctl_bindings.rs"));
}

pub use inner::*;

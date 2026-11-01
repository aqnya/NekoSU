use std::env;
use std::path::PathBuf;

fn main() {
    println!("cargo:rerun-if-changed=../../uapi/ioctl.h");
    println!("cargo:rerun-if-changed=../../uapi/selinux.h");
    println!("cargo:rerun-if-changed=build.rs");

    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
    if target_os != "android" {
        std::fs::write(out_path.join("uapi_bindings.rs"), "// not android\n").unwrap();
        return;
    }

    let bindings = bindgen::Builder::default()
        .header("../../uapi/ioctl.h")
        .header("../../uapi/selinux.h")
        .clang_arg("-I../../uapi")
        .allowlist_type("fmac_.*")
        .allowlist_type("nksu_.*")
        .allowlist_var("FMAC_.*")
        .allowlist_var("IOC_.*")
        .allowlist_var("NKSU_.*")
        .allowlist_function(".*")
        .layout_tests(false)
        .derive_default(true)
        .derive_debug(true)
        .ctypes_prefix("libc")
        .generate()
        .expect("Unable to generate bindings for uapi headers");

    bindings
        .write_to_file(out_path.join("uapi_bindings.rs"))
        .expect("Couldn't write uapi_bindings.rs");
}

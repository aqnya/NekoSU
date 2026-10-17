use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

const BOOTSTRAP_OBJECT: &str = "lkm_image_bootstrap.o";

fn get_git_version() -> Result<(u32, String), std::io::Error> {
    let output = Command::new("git")
        .args(["rev-list", "--count", "HEAD"])
        .output()?;

    let version_code = String::from_utf8(output.stdout).expect("Failed to read git count stdout");
    let version_code: u32 = version_code
        .trim()
        .parse()
        .map_err(|_| std::io::Error::other("Failed to parse git count"))?;
    let version_code = 30000 + version_code;

    let version_name = String::from_utf8(
        Command::new("git")
            .args(["describe", "--tags", "--always"])
            .output()?
            .stdout,
    )
    .map_err(|_| std::io::Error::other("Failed to read git describe stdout"))?;
    let version_name = version_name.trim_start_matches('v').to_string();
    Ok((version_code, version_name))
}

fn main() {
    // NekoSU patches vendor_boot with neko-boot and never uses upstream ksud's
    // embedded LKM bootstrap.  Keep the include_bytes! input present, but do
    // not require a host/NDK assembler for it.
    let bootstrap = PathBuf::from(env::var("OUT_DIR").unwrap()).join(BOOTSTRAP_OBJECT);
    fs::write(&bootstrap, [0u8; 0]).expect("create empty bootstrap object");

    let (code, name) = match get_git_version() {
        Ok((code, name)) => (code, name),
        Err(_) => {
            println!("cargo:warning=Failed to get git version, using 0.0.0");
            (0, "0.0.0".to_string())
        }
    };
    if env::var("KSU_PACKAGE_NAME").is_err() {
        println!("cargo:rustc-env=KSU_PACKAGE_NAME=me.aqnya.nekosu");
    }
    println!("cargo:rustc-env=VERSION_CODE={code}");
    println!("cargo:rustc-env=VERSION_NAME={name}");
}

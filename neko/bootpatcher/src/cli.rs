use anyhow::{Context, Result, bail};
use clap::Parser;

#[derive(Parser, Debug)]
#[command(
    name = "bootpatcher",
    about = "patch an LKM into a v3/v4 vendor_boot image",
    arg_required_else_help = true,
    override_usage = "bootpatcher -p <vendor_boot.img> <nksu.ko> [-b <boot.img>]\n       bootpatcher -p <vendor_boot.img> <nksu.ko> --no-symbol-check"
)]
pub struct Cli {
    /// vendor_boot image and nksu.ko to install
    #[arg(short = 'p', long = "patch", num_args = 2, value_names = ["VENDOR_BOOT", "NKSU_KO"])]
    pub patch: Vec<String>,

    /// boot image containing the target kernel
    #[arg(short = 'b', long = "boot")]
    pub boot: Option<String>,

    /// skip the kernel symbol verification
    #[arg(long = "no-symbol-check")]
    pub no_symbol_check: bool,
}

pub struct Args {
    pub image: String,
    pub module: String,
    pub boot: Option<String>,
    pub verify_symbols: bool,
}

pub fn parse_args() -> Result<Args> {
    let cli = Cli::parse();

    if cli.patch.len() != 2 {
        bail!("-p/--patch was given more than once");
    }
    let mut patch = cli.patch.into_iter();
    let image = patch.next().expect("patch len checked");
    let module = patch.next().expect("patch len checked");

    Ok(Args {
        image,
        module,
        boot: cli.boot,
        verify_symbols: !cli.no_symbol_check,
    })
}

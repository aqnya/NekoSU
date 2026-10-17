// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * NekoSU ncore command line.
 *
 * Upstream ksud carries boot patching, LKM late-loading, sulog, feature and
 * profile commands.  NekoSU does boot patching in bootpatcher, loads its LKM from
 * vendor_boot and drives profiles/features through the manager JNI, so only
 * the module runtime, su/resetprop entry points and daemon install remain.
 */
use anyhow::{Ok, Result};
use clap::Parser;

use android_logger::Config;
use log::{LevelFilter, info};

use crate::{defs, init_event, ksucalls, module, resetprop, su, utils};

#[derive(Parser, Debug)]
#[command(author, version = defs::FULL_VERSION, about, long_about = None)]
struct Args {
    #[command(subcommand)]
    command: Commands,
}

#[derive(clap::Subcommand, Debug)]
enum Commands {
    /// Manage KernelSU modules
    Module {
        #[command(subcommand)]
        command: Module,
    },

    /// SELinux policy patch tool
    Sepolicy {
        #[command(subcommand)]
        command: Sepolicy,
    },

    /// Trigger `post-fs-data` event
    PostFsData,

    /// Trigger `service` event
    Services,

    /// Trigger `boot-complete` event
    BootCompleted,

    /// Install the ncore userspace component
    Install,

    /// Resetprop - Magisk-compatible system property tool
    #[command(disable_help_flag = true)]
    Resetprop {
        #[arg(trailing_var_arg = true, allow_hyphen_values = true, num_args = 0..)]
        args: Vec<String>,
    },
}

#[derive(clap::Subcommand, Debug)]
enum Sepolicy {
    /// Patch the live kernel policy with a rule
    Patch { sepolicy: String },

    /// Apply rules from a file
    Apply { file: String },

    /// Check whether a rule is valid/supported without applying it
    Check { sepolicy: String },
}

#[derive(clap::Subcommand, Debug)]
enum Module {
    /// Install module <ZIP>
    Install { zip: String },

    /// Undo module uninstall mark <id>
    UndoUninstall { id: String },

    /// Uninstall module <id>
    Uninstall { id: String },

    /// Enable module <id>
    Enable { id: String },

    /// Disable module <id>
    Disable { id: String },

    /// Run action for module <id>
    Action { id: String },

    /// List all modules as JSON
    List {
        #[arg(long)]
        json: bool,
    },
}

pub fn run() -> Result<()> {
    android_logger::init_once(
        Config::default()
            .with_max_level(crate::debug_select!(LevelFilter::Trace, LevelFilter::Info))
            .with_tag("NekoSU"),
    );

    ksucalls::setup_sigsys_handler();

    // The kernel redirects an execve() of /system/bin/su to this binary while
    // keeping argv[0] intact.
    let arg0 = std::env::args().next().unwrap_or_default();
    if arg0 == "su" || arg0.ends_with("/su") {
        return su::root_shell();
    }

    if arg0.ends_with("resetprop") {
        let all_args: Vec<String> = std::env::args().collect();
        resetprop::resetprop_main(&all_args);
    }

    let cli = Args::parse();
    log::info!("command: {:?}", cli.command);

    let result = match cli.command {
        Commands::PostFsData => init_event::on_post_fs_data(),
        Commands::Services => {
            if ksucalls::get_version() <= 0 {
                info!("NekoSU kernel driver not available, exiting services");
                std::process::exit(0);
            }
            init_event::on_services();
            Ok(())
        }
        Commands::BootCompleted => {
            init_event::on_boot_completed();
            Ok(())
        }
        Commands::Install => utils::install(None, None),
        Commands::Resetprop { args } => {
            let mut full_args = vec!["resetprop".to_string()];
            full_args.extend(args);
            resetprop::resetprop_main(&full_args)
        }
        Commands::Sepolicy { command } => match command {
            Sepolicy::Patch { sepolicy } => crate::sepolicy::live_patch(&sepolicy),
            Sepolicy::Apply { file } => crate::sepolicy::apply_file(file),
            Sepolicy::Check { sepolicy } => crate::sepolicy::check_rule(&sepolicy),
        },
        Commands::Module { command } => {
            utils::switch_mnt_ns(1)?;
            match command {
                Module::Install { zip } => module::install_module(&zip),
                Module::UndoUninstall { id } => module::undo_uninstall_module(&id),
                Module::Uninstall { id } => module::uninstall_module(&id),
                Module::Enable { id } => module::enable_module(&id),
                Module::Disable { id } => module::disable_module(&id),
                Module::Action { id } => module::run_action(&id),
                Module::List { json: _ } => module::list_modules(),
            }
        }
    };

    if let Err(e) = &result {
        log::error!("Error: {e:?}");
    }
    result
}

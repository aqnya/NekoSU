#![cfg(target_os = "android")]

use std::ffi::CStr;
use std::fs;
use std::os::fd::RawFd;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::ptr;
use std::slice;
use std::sync::Once;
use std::sync::atomic::{AtomicI32, Ordering};

const FMAC_MAX_DATA: usize = 64 * 1024;
const FMAC_STACK_DATA: usize = 264;

const FMAC_DATA_PROFILE: usize = 84;
const FMAC_DATA_FEATURE: usize = 12;
const FMAC_OFF_UID: usize = 0;
const FMAC_OFF_CAPS: usize = 4;
const FMAC_OFF_DOMAIN: usize = 12;
const FMAC_OFF_NS: usize = 76;
const FMAC_OFF_ROOT_UID: usize = 80;
const FMAC_OFF_FEATURE_VALUE: usize = 4;

const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;
const IOC_DEL_UID: u32 = 4;
const IOC_HAS_UID: u32 = 5;
const IOC_SET_PROFILE: u32 = 10;
const IOC_GET_PROFILES: u32 = 13;
const IOC_GET_VERSION: u32 = 14;
const IOC_FEATURE_LIST: u32 = 15;
const IOC_FEATURE_GET: u32 = 16;
const IOC_FEATURE_SET: u32 = 17;

const PR_GET_SHM_FD: libc::c_int = 201;
const PR_GET_CTL_FD: libc::c_int = 203;

static LOGGER: Once = Once::new();
static CTL_FD: AtomicI32 = AtomicI32::new(-1);

fn init_logging() {
    LOGGER.call_once(|| {
        android_logger::init_once(
            android_logger::Config::default()
                .with_max_level(log::LevelFilter::Info)
                .with_tag("ncore"),
        );
    });
}

fn scan_fd(target: &str) -> RawFd {
    let Ok(entries) = fs::read_dir("/proc/self/fd") else {
        return -1;
    };
    for entry in entries.flatten() {
        let Ok(fd) = entry.file_name().to_string_lossy().parse::<i32>() else {
            continue;
        };
        let link = format!("/proc/self/fd/{fd}");
        if let Ok(path) = fs::read_link(link)
            && path.to_string_lossy().contains(target)
        {
            return fd;
        }
    }
    -1
}

fn ensure_ctl_fd() -> Option<RawFd> {
    let cached = CTL_FD.load(Ordering::Relaxed);
    if cached >= 0 {
        return Some(cached);
    }
    let ret = unsafe { libc::prctl(PR_GET_CTL_FD, 0, 0, 0, 0) };
    let error = std::io::Error::last_os_error();
    let fd = scan_fd("[fmac_ctl]");
    if fd >= 0 {
        CTL_FD.store(fd, Ordering::Relaxed);
        Some(fd)
    } else {
        log::error!(
            "control fd not found after prctl({PR_GET_CTL_FD}): ret={ret}, error={error}, uid={}",
            unsafe { libc::getuid() }
        );
        None
    }
}

fn ioc_call(fd: RawFd, flag: u32, data: &mut [u8]) -> i32 {
    if data.len() > FMAC_MAX_DATA {
        return -1;
    }
    let total = 8 + data.len();
    let mut stack = [0u8; 8 + FMAC_STACK_DATA];
    let mut heap = Vec::new();
    let buffer: &mut [u8] = if total <= stack.len() {
        &mut stack[..total]
    } else {
        heap.resize(total, 0);
        &mut heap
    };
    buffer[0..4].copy_from_slice(&flag.to_ne_bytes());
    buffer[4..8].copy_from_slice(&(data.len() as u32).to_ne_bytes());
    buffer[8..].copy_from_slice(data);

    let ret = unsafe { libc::ioctl(fd, IOC_CMD, buffer.as_mut_ptr()) };
    if ret < 0 {
        log::error!(
            "ioctl flag {flag} failed: {}",
            std::io::Error::last_os_error()
        );
    } else if !data.is_empty() {
        data.copy_from_slice(&buffer[8..]);
    }
    ret
}

fn ctl_impl(value: i32) -> i32 {
    log::info!("ctl({value})");
    match value {
        1 => {
            let ret = unsafe { libc::prctl(PR_GET_SHM_FD, 0, 0, 0, 0) };
            let error = std::io::Error::last_os_error();
            let fd = scan_fd("[fmac_shm]");
            if fd < 0 {
                log::error!(
                    "shared-memory fd not found after prctl({PR_GET_SHM_FD}): ret={ret}, error={error}, uid={}",
                    unsafe { libc::getuid() }
                );
                return -1;
            }
            0
        }
        3 => ensure_ctl_fd().map_or(-1, |_| 0),
        _ => -1,
    }
}

fn parse_gki(release: &str) -> Option<(bool, String)> {
    let mut parts = release.split('.');
    let major: i32 = parts.next()?.parse().ok()?;
    let minor_part = parts.next()?;
    let minor: i32 = minor_part
        .split(|c: char| !c.is_ascii_digit())
        .next()?
        .parse()
        .ok()?;
    let mut gki = false;
    if let Some(tag) = release.find("-android") {
        let rest = &release[tag + "-android".len()..];
        let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        if !digits.is_empty() {
            let after = &rest[digits.len()..];
            if let Some(after) = after.strip_prefix('-')
                && after.chars().next().is_some_and(|c| c.is_ascii_digit())
            {
                gki = true;
            }
        }
    }
    Some((gki, format!("{major}.{minor:02}")))
}

fn uname_release() -> Option<String> {
    let mut uts: libc::utsname = unsafe { std::mem::zeroed() };
    if unsafe { libc::uname(&mut uts) } != 0 {
        return None;
    }
    let release = unsafe { CStr::from_ptr(uts.release.as_ptr()) };
    Some(release.to_string_lossy().into_owned())
}

fn module_version() -> Option<String> {
    let fd = ensure_ctl_fd()?;
    let mut data = [0u8; 64];
    if ioc_call(fd, IOC_GET_VERSION, &mut data) < 0 {
        return None;
    }
    let end = data
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(data.len());
    Some(String::from_utf8_lossy(&data[..end]).into_owned())
}

fn copy_string(value: Option<String>, buffer: *mut u8, capacity: usize) -> isize {
    let Some(value) = value else {
        return -1;
    };
    let bytes = value.as_bytes();
    if buffer.is_null() {
        return -1;
    }
    if capacity <= bytes.len() {
        return bytes.len() as isize;
    }
    unsafe {
        ptr::copy_nonoverlapping(bytes.as_ptr(), buffer, bytes.len());
        *buffer.add(bytes.len()) = 0;
    }
    bytes.len() as isize
}

fn set_profile_impl(uid: i32, root_uid: i32, caps: u64, domain: &[u8], namespace: i32) -> i32 {
    if uid < 0 || root_uid < 0 || namespace <= 0 {
        return -1;
    }
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };

    let mut data = [0u8; FMAC_DATA_PROFILE];
    data[FMAC_OFF_UID..FMAC_OFF_UID + 4].copy_from_slice(&(uid as u32).to_ne_bytes());
    data[FMAC_OFF_CAPS..FMAC_OFF_CAPS + 8].copy_from_slice(&caps.to_ne_bytes());
    let len = domain.len().min(FMAC_OFF_NS - FMAC_OFF_DOMAIN - 1);
    data[FMAC_OFF_DOMAIN..FMAC_OFF_DOMAIN + len].copy_from_slice(&domain[..len]);
    data[FMAC_OFF_NS..FMAC_OFF_NS + 4].copy_from_slice(&namespace.to_ne_bytes());
    data[FMAC_OFF_ROOT_UID..FMAC_OFF_ROOT_UID + 4]
        .copy_from_slice(&(root_uid as u32).to_ne_bytes());

    if ioc_call(fd, IOC_SET_PROFILE, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn uid_ioctl(uid: i32, flag: u32) -> i32 {
    if uid < 0 {
        return -1;
    }
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = (uid as u32).to_ne_bytes();
    if ioc_call(fd, flag, &mut data) < 0 {
        -1
    } else {
        0
    }
}

/*
 * Acquire the control fd from a newly created thread.
 *
 * Android forks an app process from zygote while it still carries zygote's
 * uid, so the thread that eventually runs Dart's main() was created before the
 * process owned the manager uid and never received TIF_SYSCALL_TRACEPOINT.
 * prctl() from that thread therefore bypasses the syscall tracepoint and the fd
 * grant never runs.  A thread created now already carries the manager uid, so
 * the kernel marks it and the grant succeeds.  The descriptor is process wide,
 * so every later ctl() on the platform thread finds the cached value.
 */
fn ensure_ctl_fd_from_worker() -> Option<RawFd> {
    std::thread::scope(|scope| scope.spawn(ensure_ctl_fd).join().unwrap_or(None))
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_init() -> i32 {
    init_logging();
    let Some(_) = ensure_ctl_fd_from_worker() else {
        log::error!("can't load control fd!");
        return -1;
    };
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_ctl(value: i32) -> i32 {
    init_logging();
    ctl_impl(value)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_is_gki() -> i32 {
    init_logging();
    uname_release()
        .and_then(|release| parse_gki(&release))
        .map(|(gki, _)| if gki { 1 } else { 0 })
        .unwrap_or(-1)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_kernel_version(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    let version = uname_release()
        .and_then(|release| parse_gki(&release))
        .map(|(_, version)| version);
    copy_string(version, buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_kernel_release(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    copy_string(uname_release(), buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_version(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    copy_string(module_version(), buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_feature_list(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = [0u8; 1024];
    if ioc_call(fd, IOC_FEATURE_LIST, &mut data) < 0 {
        return -1;
    }
    let end = data
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(data.len());
    copy_string(
        Some(String::from_utf8_lossy(&data[..end]).into_owned()),
        buffer,
        capacity,
    )
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_feature_get(id: u32) -> i64 {
    init_logging();
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = [0u8; FMAC_DATA_FEATURE];
    data[..4].copy_from_slice(&id.to_ne_bytes());
    if ioc_call(fd, IOC_FEATURE_GET, &mut data) < 0 {
        return -1;
    }
    u64::from_ne_bytes(
        data[FMAC_OFF_FEATURE_VALUE..FMAC_OFF_FEATURE_VALUE + 8]
            .try_into()
            .expect("feature value has a fixed width"),
    ) as i64
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_feature_set(id: u32, value: u64) -> i32 {
    init_logging();
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = [0u8; FMAC_DATA_FEATURE];
    data[..4].copy_from_slice(&id.to_ne_bytes());
    data[FMAC_OFF_FEATURE_VALUE..FMAC_OFF_FEATURE_VALUE + 8].copy_from_slice(&value.to_ne_bytes());
    if ioc_call(fd, IOC_FEATURE_SET, &mut data) < 0 {
        -1
    } else {
        0
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_set_profile(
    uid: i32,
    root_uid: i32,
    caps: u64,
    domain: *const u8,
    domain_len: usize,
    namespace: i32,
) -> i32 {
    init_logging();
    if domain_len > 4096 || (domain.is_null() && domain_len != 0) {
        return -1;
    }
    let domain = if domain_len == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(domain, domain_len) }
    };
    set_profile_impl(uid, root_uid, caps, domain, namespace)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_del_uid(uid: i32) -> i32 {
    init_logging();
    uid_ioctl(uid, IOC_DEL_UID)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_has_uid(uid: i32) -> i32 {
    init_logging();
    if uid < 0 {
        return -1;
    }
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = (uid as u32).to_ne_bytes();
    if ioc_call(fd, IOC_HAS_UID, &mut data) < 0 {
        return -1;
    }
    if u32::from_ne_bytes(data) != 0 { 1 } else { 0 }
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_list_profiles(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    let Some(fd) = ensure_ctl_fd() else {
        return -1;
    };
    let mut data = vec![0u8; FMAC_MAX_DATA];
    if ioc_call(fd, IOC_GET_PROFILES, &mut data) < 0 {
        return -1;
    }
    let end = data
        .iter()
        .position(|byte| *byte == 0)
        .unwrap_or(data.len());
    copy_string(
        Some(String::from_utf8_lossy(&data[..end]).into_owned()),
        buffer,
        capacity,
    )
}

const MODULE_ARGUMENT_LIMIT: usize = 8192;

fn shell_quote(value: &str) -> String {
    format!("'{}'", value.replace('\'', "'\\''"))
}

fn valid_module_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= 128
        && !id.contains("..")
        && id
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || matches!(byte, b'.' | b'-' | b'_'))
}

fn native_library_dir() -> Option<PathBuf> {
    let maps = fs::read_to_string("/proc/self/maps").ok()?;
    maps.lines().find_map(|line| {
        let path = line.split_whitespace().last()?;
        let path = path.strip_suffix(" (deleted)").unwrap_or(path);
        path.ends_with("/libncore_ffi.so")
            .then(|| Path::new(path).parent().map(Path::to_path_buf))
            .flatten()
    })
}

fn ncore_path() -> Option<PathBuf> {
    if let Some(directory) = native_library_dir() {
        let candidate = directory.join("libncore.so");
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    ["/data/adb/ncore", "/data/adb/nksu/bin/ncore"]
        .into_iter()
        .map(PathBuf::from)
        .find(|path| path.is_file())
}

fn root_command(command: &str) -> (i32, String) {
    let (code, mut output, stderr) = root_command_parts(command);
    if !stderr.is_empty() {
        if !output.is_empty() && !output.ends_with('\n') {
            output.push('\n');
        }
        output.push_str(&stderr);
    }
    (code, output)
}

fn root_command_parts(command: &str) -> (i32, String, String) {
    let su = if Path::new("/system/bin/su").is_file() {
        "/system/bin/su"
    } else {
        "su"
    };
    let command =
        format!("PATH=/sbin:/system/sbin:/system/bin:/system/xbin; export PATH; {command}");
    match Command::new(su).args(["-c", command.as_str()]).output() {
        Ok(result) => (
            result.status.code().unwrap_or(-1),
            String::from_utf8_lossy(&result.stdout).into_owned(),
            String::from_utf8_lossy(&result.stderr).into_owned(),
        ),
        Err(error) => (
            -1,
            String::new(),
            format!("root shell unavailable: {error}"),
        ),
    }
}

fn json_quote(value: &str) -> String {
    let mut output = String::with_capacity(value.len() + 2);
    output.push('"');
    for character in value.chars() {
        match character {
            '"' => output.push_str("\\\""),
            '\\' => output.push_str("\\\\"),
            '\n' => output.push_str("\\n"),
            '\r' => output.push_str("\\r"),
            '\t' => output.push_str("\\t"),
            '\u{08}' => output.push_str("\\b"),
            '\u{0c}' => output.push_str("\\f"),
            character if character < '\u{20}' => {
                output.push_str(&format!("\\u{:04x}", character as u32));
            }
            character => output.push(character),
        }
    }
    output.push('"');
    output
}

fn module_ncore_command(arguments: &str) -> Result<String, String> {
    let ncore = ncore_path().ok_or_else(|| "ncore runtime not found".to_owned())?;
    Ok(format!(
        "{} module {arguments}",
        shell_quote(&ncore.to_string_lossy())
    ))
}

fn module_result(code: i32, output: String) -> String {
    format!("{code}\n{output}")
}

fn copy_module_result(value: String, buffer: *mut u8, capacity: usize) -> isize {
    copy_string(Some(value), buffer, capacity)
}

fn required_argument(pointer: *const u8, length: usize) -> Option<String> {
    if pointer.is_null() || length == 0 || length > MODULE_ARGUMENT_LIMIT {
        return None;
    }
    let bytes = unsafe { slice::from_raw_parts(pointer, length) };
    std::str::from_utf8(bytes).ok().map(str::to_owned)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_list(buffer: *mut u8, capacity: usize) -> isize {
    init_logging();
    let result = match module_ncore_command("list --json") {
        Ok(command) => root_command(&command),
        Err(error) => (-1, error),
    };
    copy_module_result(module_result(result.0, result.1), buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_install(
    zip: *const u8,
    zip_length: usize,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    let Some(zip) = required_argument(zip, zip_length) else {
        return copy_module_result(
            module_result(-1, "Invalid module ZIP path".to_owned()),
            buffer,
            capacity,
        );
    };
    if !Path::new(&zip).is_file() {
        return copy_module_result(
            module_result(-1, format!("Module ZIP not found: {zip}")),
            buffer,
            capacity,
        );
    }
    let result = match module_ncore_command(&format!("install {}", shell_quote(&zip))) {
        Ok(command) => root_command(&command),
        Err(error) => (-1, error),
    };
    copy_module_result(module_result(result.0, result.1), buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_action(
    id: *const u8,
    id_length: usize,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    let Some(id) = required_argument(id, id_length) else {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    };
    if !valid_module_id(&id) {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    }
    let result = match module_ncore_command(&format!("action {}", shell_quote(&id))) {
        Ok(command) => root_command(&command),
        Err(error) => (-1, error),
    };
    copy_module_result(module_result(result.0, result.1), buffer, capacity)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_control(
    id: *const u8,
    id_length: usize,
    operation: i32,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    let Some(id) = required_argument(id, id_length) else {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    };
    if !valid_module_id(&id) {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    }

    let module_path = format!("/data/adb/modules/{id}");
    let command = match operation {
        0 => module_ncore_command(&format!("enable {}", shell_quote(&id))),
        1 => module_ncore_command(&format!("disable {}", shell_quote(&id))),
        2 => Ok(format!("touch {}/remove", shell_quote(&module_path))),
        3 => Ok(format!("rm -f {}/remove", shell_quote(&module_path))),
        4 => Ok(format!("rm -rf {}", shell_quote(&module_path))),
        _ => {
            return copy_module_result(
                module_result(-1, "Invalid module operation".to_owned()),
                buffer,
                capacity,
            );
        }
    };
    let result = match command {
        Ok(command) => root_command(&command),
        Err(error) => (-1, error),
    };
    copy_module_result(module_result(result.0, result.1), buffer, capacity)
}

fn app_cache_dir() -> Option<PathBuf> {
    let command_line = fs::read_to_string("/proc/self/cmdline").ok()?;
    let package = command_line.split('\0').next()?.trim();
    if package.is_empty() {
        return None;
    }
    let current_user = PathBuf::from("/data/user/0").join(package).join("cache");
    if current_user.is_dir() {
        return Some(current_user);
    }
    let legacy = PathBuf::from("/data/data").join(package).join("cache");
    legacy.is_dir().then_some(legacy)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_module_webui(
    id: *const u8,
    id_length: usize,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    let Some(id) = required_argument(id, id_length) else {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    };
    if !valid_module_id(&id) {
        return copy_module_result(
            module_result(-1, "Invalid module id".to_owned()),
            buffer,
            capacity,
        );
    }
    let Some(cache) = app_cache_dir() else {
        return copy_module_result(
            module_result(-1, "App cache directory not found".to_owned()),
            buffer,
            capacity,
        );
    };

    let destination = cache.join("module-webui").join(&id);
    let destination_text = destination.to_string_lossy();
    let source = format!("/data/adb/modules/{id}/webroot");
    let command = format!(
        "rm -rf {destination} && mkdir -p {destination} && cp -a {source}/. {destination}/ && chmod -R a+rX {destination}",
        destination = shell_quote(&destination_text),
        source = shell_quote(&source),
    );
    let (code, output) = root_command(&command);
    if code != 0 {
        return copy_module_result(module_result(code, output), buffer, capacity);
    }

    let entry = ["index.html", "index.htm"]
        .into_iter()
        .map(|name| destination.join(name))
        .find(|path| path.is_file());
    let Some(entry) = entry else {
        return copy_module_result(
            module_result(1, "webroot/index.html was not found".to_owned()),
            buffer,
            capacity,
        );
    };
    copy_module_result(
        module_result(0, entry.to_string_lossy().into_owned()),
        buffer,
        capacity,
    )
}

const WEBUI_COMMAND_LIMIT: usize = 1024 * 1024;

#[unsafe(no_mangle)]
pub extern "C" fn ncore_webui_exec(
    command: *const u8,
    command_length: usize,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    if command.is_null() || command_length == 0 || command_length > WEBUI_COMMAND_LIMIT {
        return copy_module_result(
            "{\"code\":-1,\"stdout\":\"\",\"stderr\":\"Invalid command\"}".to_owned(),
            buffer,
            capacity,
        );
    }
    let bytes = unsafe { slice::from_raw_parts(command, command_length) };
    let Ok(command) = std::str::from_utf8(bytes) else {
        return copy_module_result(
            "{\"code\":-1,\"stdout\":\"\",\"stderr\":\"Command is not valid UTF-8\"}".to_owned(),
            buffer,
            capacity,
        );
    };
    let (code, stdout, stderr) = root_command_parts(command);
    let result = format!(
        "{{\"code\":{code},\"stdout\":{},\"stderr\":{}}}",
        json_quote(&stdout),
        json_quote(&stderr),
    );
    copy_module_result(result, buffer, capacity)
}

const BLOCK_DIRECTORIES: [&str; 3] = [
    "/dev/block/by-name",
    "/dev/block/bootdevice/by-name",
    "/dev/block/platform/soc/1d84000.ufshc/by-name",
];

fn get_prop(name: &str) -> String {
    Command::new("/system/bin/getprop")
        .arg(name)
        .output()
        .map(|output| String::from_utf8_lossy(&output.stdout).trim().to_owned())
        .unwrap_or_default()
}

fn slot_suffix() -> String {
    let suffix = get_prop("ro.boot.slot_suffix");
    let raw = if suffix.is_empty() {
        get_prop("ro.boot.slot")
    } else {
        suffix
    };
    match raw.as_str() {
        "_a" | "_b" => raw,
        "a" => "_a".to_owned(),
        "b" => "_b".to_owned(),
        _ => String::new(),
    }
}

fn executable_file(path: &Path) -> bool {
    use std::os::unix::fs::PermissionsExt;

    let Ok(metadata) = fs::metadata(path) else {
        return false;
    };
    if !metadata.is_file() {
        return false;
    }
    let mode = metadata.permissions().mode();
    if mode & 0o111 == 0 {
        let mut permissions = metadata.permissions();
        permissions.set_mode(mode | 0o755);
        let _ = fs::set_permissions(path, permissions);
    }
    true
}

fn bootpatcher_path() -> Option<PathBuf> {
    let candidate = native_library_dir()?.join("libbootpatcher.so");
    candidate.is_file().then_some(candidate)
}

fn install_error(message: String, buffer: *mut u8, capacity: usize) -> isize {
    copy_module_result(module_result(-1, message), buffer, capacity)
}

fn append_output(output: &mut String, extra: &str) {
    if extra.is_empty() {
        return;
    }
    if !output.is_empty() && !output.ends_with('\n') {
        output.push('\n');
    }
    output.push_str(extra);
}

fn install_fail(mut output: String, message: &str) -> (i32, String) {
    if !output.is_empty() && !output.ends_with('\n') {
        output.push('\n');
    }
    output.push_str("ERROR: ");
    output.push_str(message);
    output.push('\n');
    (-1, output)
}

fn resolve_vendor_boot(slot: &str) -> Option<String> {
    let mut names = Vec::new();
    if !slot.is_empty() {
        names.push(format!("vendor_boot{slot}"));
    }
    names.push("vendor_boot".to_owned());

    let mut candidates = Vec::new();
    for directory in BLOCK_DIRECTORIES {
        for name in &names {
            candidates.push(format!("{directory}/{name}"));
        }
    }
    let list = candidates
        .iter()
        .map(|path| shell_quote(path))
        .collect::<Vec<_>>()
        .join(" ");
    let command =
        format!("for p in {list}; do if [ -e \"$p\" ]; then readlink -f \"$p\"; break; fi; done");
    let (code, output) = root_command(&command);
    let path = output
        .lines()
        .map(str::trim)
        .find(|line| !line.is_empty())?;
    (code == 0 && path.starts_with("/dev/block/")).then(|| path.to_owned())
}

fn run_direct_install(
    partition: &str,
    patcher: &Path,
    ko: &str,
    source: &Path,
    patched: &Path,
) -> (i32, String) {
    let source_text = source.to_string_lossy().into_owned();
    let mut output =
        format!("[nekosu] vendor_boot: {partition}\n[nekosu] reading the active vendor_boot...\n");

    let (code, text) = root_command(&format!(
        "/system/bin/dd if={} of={} bs=4M 2>&1",
        shell_quote(partition),
        shell_quote(&source_text),
    ));
    append_output(&mut output, &text);
    let source_size = fs::metadata(source).map(|meta| meta.len()).unwrap_or(0);
    if code != 0 || source_size == 0 {
        return install_fail(output, "failed to read vendor_boot");
    }

    output.push_str("[nekosu] patching vendor_boot image...\n");
    let patch = match Command::new(patcher).arg("-p").arg(source).arg(ko).output() {
        Ok(result) => result,
        Err(error) => {
            return install_fail(output, &format!("failed to run libbootpatcher: {error}"));
        }
    };
    append_output(&mut output, &String::from_utf8_lossy(&patch.stdout));
    append_output(&mut output, &String::from_utf8_lossy(&patch.stderr));
    let patched_size = fs::metadata(patched).map(|meta| meta.len()).unwrap_or(0);
    if patch.status.code().unwrap_or(-1) != 0 || patched_size == 0 {
        return install_fail(output, "patched image was not produced");
    }

    let (size_code, size_text) = root_command(&format!(
        "/system/bin/blockdev --getsize64 {} 2>/dev/null",
        shell_quote(partition),
    ));
    if size_code == 0
        && let Ok(capacity) = size_text.trim().parse::<u64>()
        && patched_size > capacity
    {
        return install_fail(
            output,
            &format!(
                "patched image ({patched_size} bytes) exceeds partition capacity ({capacity} bytes)"
            ),
        );
    }

    output.push_str("[nekosu] writing the patched image...\n");
    let (code, text) = root_command(&format!(
        "/system/bin/dd if={} of={} bs=4M conv=fsync 2>&1",
        shell_quote(&patched.to_string_lossy()),
        shell_quote(partition),
    ));
    append_output(&mut output, &text);
    if code != 0 {
        output.push_str("[nekosu] write failed, restoring the original image...\n");
        let (restore_code, restore_text) = root_command(&format!(
            "/system/bin/dd if={} of={} bs=4M conv=fsync 2>&1",
            shell_quote(&source_text),
            shell_quote(partition),
        ));
        append_output(&mut output, &restore_text);
        if restore_code != 0 {
            return install_fail(
                output,
                "failed to flash vendor_boot and the original image could not be restored",
            );
        }
        return install_fail(
            output,
            "failed to flash vendor_boot; the original image was restored",
        );
    }

    output.push_str("[nekosu] direct install complete; reboot to take effect\n");
    (0, output)
}

#[unsafe(no_mangle)]
pub extern "C" fn ncore_direct_install(
    ko: *const u8,
    ko_length: usize,
    buffer: *mut u8,
    capacity: usize,
) -> isize {
    init_logging();
    let Some(ko) = required_argument(ko, ko_length) else {
        return install_error("Invalid kernel module path".to_owned(), buffer, capacity);
    };
    if !Path::new(&ko).is_file() {
        return install_error(format!("kernel module not found: {ko}"), buffer, capacity);
    }
    let Some(patcher) = bootpatcher_path() else {
        return install_error("libbootpatcher.so not found".to_owned(), buffer, capacity);
    };
    if !executable_file(&patcher) {
        return install_error(
            "libbootpatcher.so is not executable".to_owned(),
            buffer,
            capacity,
        );
    }
    let Some(partition) = resolve_vendor_boot(&slot_suffix()) else {
        return install_error(
            "vendor_boot partition not found".to_owned(),
            buffer,
            capacity,
        );
    };
    let Some(cache) = app_cache_dir() else {
        return install_error("app cache directory not found".to_owned(), buffer, capacity);
    };

    let work = cache.join("nksu-install");
    if let Err(error) = fs::create_dir_all(&work) {
        return install_error(
            format!("cannot create {}: {error}", work.display()),
            buffer,
            capacity,
        );
    }
    let source = work.join("vendor_boot.img");
    let patched = work.join("vendor_boot.img.new");
    let _ = fs::remove_file(&source);
    let _ = fs::remove_file(&patched);
    // Create the file as the app first so root's dd writes into an app-owned file.
    if let Err(error) = fs::File::create(&source) {
        return install_error(
            format!("cannot create {}: {error}", source.display()),
            buffer,
            capacity,
        );
    }

    let (code, output) = run_direct_install(&partition, &patcher, &ko, &source, &patched);
    let _ = fs::remove_file(&source);
    let _ = fs::remove_file(&patched);
    copy_module_result(module_result(code, output), buffer, capacity)
}

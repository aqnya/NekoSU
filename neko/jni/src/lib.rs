#![cfg(target_os = "android")]
//! Rust replacement for the manager's former C++ JNI layer.

use std::ffi::{CStr, CString};
use std::fs;
use std::os::fd::RawFd;
use std::ptr;
use std::sync::atomic::{AtomicI32, Ordering};

use jni::objects::{JClass, JString};
use jni::sys::{JNI_FALSE, JNI_TRUE, JNI_VERSION_1_6, jboolean, jbyteArray, jint, jlong, jstring};
use jni::{JNIEnv, JavaVM, NativeMethod};

const FMAC_MAX_DATA: usize = 4096;
const FMAC_DATA_CAP: usize = 12;
const FMAC_DATA_SELRULE: usize = 264;
const FMAC_DATA_PROFILE: usize = 84;
const FMAC_DATA_FEATURE: usize = 12;
const FMAC_OFF_UID: usize = 0;
const FMAC_OFF_CAPS: usize = 4;
const FMAC_OFF_DOMAIN: usize = 12;
const FMAC_OFF_NS: usize = 76;
const FMAC_OFF_ROOT_UID: usize = 80;
const FMAC_OFF_TGT: usize = 64;
const FMAC_OFF_CLS: usize = 128;
const FMAC_OFF_PERM: usize = 192;
const FMAC_OFF_EFFECT: usize = 256;
const FMAC_OFF_INVERT: usize = 260;
const FMAC_OFF_FEATURE_VALUE: usize = 4;

const IOC_CMD: libc::c_int = 0xC008_4600u32 as libc::c_int;
const IOC_ADD_UID: u32 = 3;
const IOC_DEL_UID: u32 = 4;
const IOC_HAS_UID: u32 = 5;
const IOC_SET_CAP: u32 = 6;
const IOC_GET_CAP: u32 = 7;
const IOC_DEL_CAP: u32 = 8;
const IOC_SEL_ADD_RULE: u32 = 9;
const IOC_SET_PROFILE: u32 = 10;
const IOC_GET_PROFILES: u32 = 13;
const IOC_GET_VERSION: u32 = 14;
const IOC_FEATURE_LIST: u32 = 15;
const IOC_FEATURE_GET: u32 = 16;
const IOC_FEATURE_SET: u32 = 17;

const PR_GET_SHM_FD: libc::c_int = 201;
const PR_GET_CTL_FD: libc::c_int = 203;

static DRIVER_FD: AtomicI32 = AtomicI32::new(-1);
static CTL_FD: AtomicI32 = AtomicI32::new(-1);

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
    unsafe {
        libc::prctl(PR_GET_CTL_FD, 0, 0, 0, 0);
    }
    let fd = scan_fd("[fmac_ctl]");
    if fd >= 0 {
        CTL_FD.store(fd, Ordering::Relaxed);
        Some(fd)
    } else {
        None
    }
}

fn ioc_call(fd: RawFd, flag: u32, data: &mut [u8]) -> i32 {
    if data.len() > FMAC_MAX_DATA {
        return -1;
    }
    let mut buffer = vec![0u8; 8 + data.len()];
    buffer[0..4].copy_from_slice(&flag.to_ne_bytes());
    buffer[4..8].copy_from_slice(&(data.len() as u32).to_ne_bytes());
    buffer[8..].copy_from_slice(data);

    let ret = unsafe { libc::ioctl(fd, IOC_CMD, buffer.as_mut_ptr()) };
    if ret >= 0 && !data.is_empty() {
        data.copy_from_slice(&buffer[8..]);
    }
    ret
}

fn ctl(env: &mut JNIEnv, _class: JClass, value: jint) -> jint {
    let _ = env;
    match value {
        1 => {
            unsafe {
                libc::prctl(PR_GET_SHM_FD, 0, 0, 0, 0);
            }
            let fd = scan_fd("[fmac_shm]");
            if fd >= 0 {
                DRIVER_FD.store(fd, Ordering::Relaxed);
            }
        }
        3 => {
            ensure_ctl_fd();
        }
        _ => return -1,
    }
    if DRIVER_FD.load(Ordering::Relaxed) >= 0 {
        0
    } else {
        -1
    }
}

fn get_utf(env: &mut JNIEnv, value: &JString) -> Option<String> {
    if value.as_raw().is_null() {
        None
    } else {
        env.get_string(value).ok().map(Into::into)
    }
}

fn put_string(env: &mut JNIEnv, value: &str) -> jstring {
    env.new_string(value)
        .map(|s| s.into_raw())
        .unwrap_or(ptr::null_mut())
}

fn put_bytes(env: &mut JNIEnv, data: &[u8]) -> jbyteArray {
    env.byte_array_from_slice(data)
        .map(|a| a.into_raw())
        .unwrap_or(ptr::null_mut())
}

fn set_profile(
    env: &mut JNIEnv,
    _class: JClass,
    uid: jint,
    root_uid: jint,
    caps: jlong,
    domain: JString,
    ns: jint,
) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    let domain = get_utf(env, &domain).unwrap_or_default();
    let mut data = [0u8; FMAC_DATA_PROFILE];
    data[FMAC_OFF_UID..FMAC_OFF_UID + 4].copy_from_slice(&(uid as u32).to_ne_bytes());
    data[FMAC_OFF_CAPS..FMAC_OFF_CAPS + 8].copy_from_slice(&(caps as u64).to_ne_bytes());
    let bytes = domain.as_bytes();
    let len = bytes.len().min(FMAC_OFF_NS - FMAC_OFF_DOMAIN - 1);
    data[FMAC_OFF_DOMAIN..FMAC_OFF_DOMAIN + len].copy_from_slice(&bytes[..len]);
    data[FMAC_OFF_NS..FMAC_OFF_NS + 4].copy_from_slice(&ns.to_ne_bytes());
    data[FMAC_OFF_ROOT_UID..FMAC_OFF_ROOT_UID + 4]
        .copy_from_slice(&(root_uid as u32).to_ne_bytes());
    if ioc_call(fd, IOC_SET_PROFILE, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn add_uid(_env: &mut JNIEnv, _class: JClass, uid: jint) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = (uid as u32).to_ne_bytes();
    if ioc_call(fd, IOC_ADD_UID, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn del_uid(_env: &mut JNIEnv, _class: JClass, uid: jint) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = (uid as u32).to_ne_bytes();
    if ioc_call(fd, IOC_DEL_UID, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn has_uid(_env: &mut JNIEnv, _class: JClass, uid: jint) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = (uid as u32).to_ne_bytes();
    if ioc_call(fd, IOC_HAS_UID, &mut data) < 0 {
        return -1;
    }
    if u32::from_ne_bytes(data) != 0 { 1 } else { 0 }
}

fn set_cap(_env: &mut JNIEnv, _class: JClass, uid: jint, caps: jlong) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = [0u8; FMAC_DATA_CAP];
    data[FMAC_OFF_UID..FMAC_OFF_UID + 4].copy_from_slice(&(uid as u32).to_ne_bytes());
    data[FMAC_OFF_CAPS..FMAC_OFF_CAPS + 8].copy_from_slice(&(caps as u64).to_ne_bytes());
    if ioc_call(fd, IOC_SET_CAP, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn get_cap(_env: &mut JNIEnv, _class: JClass, uid: jint) -> jlong {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = [0u8; FMAC_DATA_CAP];
    data[FMAC_OFF_UID..FMAC_OFF_UID + 4].copy_from_slice(&(uid as u32).to_ne_bytes());
    if ioc_call(fd, IOC_GET_CAP, &mut data) < 0 {
        return -1;
    }
    u64::from_ne_bytes(data[FMAC_OFF_CAPS..FMAC_OFF_CAPS + 8].try_into().unwrap()) as jlong
}

fn del_cap(_env: &mut JNIEnv, _class: JClass, uid: jint) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    if uid < 0 {
        return -1;
    }
    let mut data = [0u8; FMAC_DATA_CAP];
    data[FMAC_OFF_UID..FMAC_OFF_UID + 4].copy_from_slice(&(uid as u32).to_ne_bytes());
    if ioc_call(fd, IOC_DEL_CAP, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn add_selinux_rule(
    env: &mut JNIEnv,
    _class: JClass,
    src: JString,
    tgt: JString,
    cls: JString,
    perm: JString,
    effect: jint,
    invert: jboolean,
) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    let src = get_utf(env, &src).unwrap_or_default();
    let tgt = get_utf(env, &tgt).unwrap_or_default();
    let cls = get_utf(env, &cls).unwrap_or_default();
    let perm = get_utf(env, &perm).unwrap_or_default();
    let mut data = [0u8; FMAC_DATA_SELRULE];
    let mut put = |offset: usize, cap: usize, value: &str| {
        let bytes = value.as_bytes();
        let len = bytes.len().min(cap - 1);
        data[offset..offset + len].copy_from_slice(&bytes[..len]);
    };
    put(FMAC_OFF_UID, FMAC_OFF_TGT - FMAC_OFF_UID, &src);
    put(FMAC_OFF_TGT, FMAC_OFF_CLS - FMAC_OFF_TGT, &tgt);
    put(FMAC_OFF_CLS, FMAC_OFF_PERM - FMAC_OFF_CLS, &cls);
    put(FMAC_OFF_PERM, FMAC_OFF_EFFECT - FMAC_OFF_PERM, &perm);
    data[FMAC_OFF_EFFECT..FMAC_OFF_EFFECT + 4].copy_from_slice(&effect.to_ne_bytes());
    let inv: i32 = if invert != 0 { 1 } else { 0 };
    data[FMAC_OFF_INVERT..FMAC_OFF_INVERT + 4].copy_from_slice(&inv.to_ne_bytes());
    if ioc_call(fd, IOC_SEL_ADD_RULE, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn add_rule(_env: &mut JNIEnv, _class: JClass, _path: JString, _bits: jlong) -> jint {
    0
}

fn del_rule(_env: &mut JNIEnv, _class: JClass, _path: JString) -> jint {
    0
}

fn list_profiles(env: &mut JNIEnv, _class: JClass) -> jstring {
    let Some(fd) = ensure_ctl_fd() else {
        return ptr::null_mut();
    };
    let mut data = vec![0u8; 64 * 1024];
    if ioc_call(fd, IOC_GET_PROFILES, &mut data) < 0 {
        return ptr::null_mut();
    }
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    put_string(env, &String::from_utf8_lossy(&data[..end]))
}

fn read_file_impl(path: &str) -> Option<Vec<u8>> {
    let path = CString::new(path).ok()?;
    let mut pipe = [0; 2];
    if unsafe { libc::pipe(pipe.as_mut_ptr()) } != 0 {
        return None;
    }
    let pid = unsafe { libc::fork() };
    if pid < 0 {
        unsafe {
            libc::close(pipe[0]);
            libc::close(pipe[1]);
        }
        return None;
    }
    if pid == 0 {
        unsafe {
            libc::close(pipe[0]);
            if libc::setresuid(0, 0, 0) != 0 || libc::getuid() != 0 {
                libc::_exit(1);
            }
            let file = libc::open(path.as_ptr(), libc::O_RDONLY | libc::O_CLOEXEC);
            if file < 0 {
                libc::_exit(2);
            }
            let mut buffer = [0u8; 65536];
            loop {
                let n = libc::read(file, buffer.as_mut_ptr().cast(), buffer.len());
                if n > 0 {
                    let mut off = 0;
                    while off < n {
                        let w = libc::write(
                            pipe[1],
                            buffer.as_ptr().add(off as usize).cast(),
                            (n - off) as usize,
                        );
                        if w > 0 {
                            off += w;
                        } else if w < 0 && *libc::__errno() == libc::EINTR {
                            continue;
                        } else {
                            libc::_exit(3);
                        }
                    }
                } else if n == 0 {
                    break;
                } else if *libc::__errno() != libc::EINTR {
                    libc::_exit(4);
                }
            }
            libc::close(file);
            libc::close(pipe[1]);
            libc::_exit(0);
        }
    }

    unsafe {
        libc::close(pipe[1]);
    }
    let mut data = Vec::new();
    let mut buffer = [0u8; 65536];
    loop {
        let n = unsafe { libc::read(pipe[0], buffer.as_mut_ptr().cast(), buffer.len()) };
        if n > 0 {
            data.extend_from_slice(&buffer[..n as usize]);
        } else if n == 0 {
            break;
        } else if unsafe { *libc::__errno() } != libc::EINTR {
            data.clear();
            break;
        }
    }
    unsafe {
        libc::close(pipe[0]);
    }
    let mut status = 0;
    while unsafe { libc::waitpid(pid, &mut status, 0) } < 0
        && unsafe { *libc::__errno() } == libc::EINTR
    {}
    if libc::WIFEXITED(status) && libc::WEXITSTATUS(status) == 0 {
        Some(data)
    } else {
        None
    }
}

fn read_file(env: &mut JNIEnv, _class: JClass, path: JString) -> jbyteArray {
    let Some(path) = get_utf(env, &path) else {
        return ptr::null_mut();
    };
    match read_file_impl(&path) {
        Some(data) if !data.is_empty() => put_bytes(env, &data),
        _ => ptr::null_mut(),
    }
}

fn read_file_persistent(env: &mut JNIEnv, _class: JClass, path: JString) -> jbyteArray {
    let Some(path) = get_utf(env, &path) else {
        return ptr::null_mut();
    };
    match read_file_impl(&path) {
        Some(data) => put_bytes(env, &data),
        None => ptr::null_mut(),
    }
}

fn write_block(env: &mut JNIEnv, _class: JClass, src: JString, dst: JString) -> jboolean {
    let (Some(src), Some(dst)) = (get_utf(env, &src), get_utf(env, &dst)) else {
        return JNI_FALSE;
    };
    let (Ok(src), Ok(dst)) = (CString::new(src), CString::new(dst)) else {
        return JNI_FALSE;
    };
    let pid = unsafe { libc::fork() };
    if pid < 0 {
        return JNI_FALSE;
    }
    if pid == 0 {
        unsafe {
            if libc::setresuid(0, 0, 0) != 0 || libc::getuid() != 0 {
                libc::_exit(1);
            }
            let input = libc::open(src.as_ptr(), libc::O_RDONLY | libc::O_CLOEXEC);
            if input < 0 {
                libc::_exit(2);
            }
            let output = libc::open(
                dst.as_ptr(),
                libc::O_WRONLY | libc::O_CREAT | libc::O_TRUNC | libc::O_CLOEXEC,
                0o644,
            );
            if output < 0 {
                libc::close(input);
                libc::_exit(3);
            }
            let mut src_stat: libc::stat = std::mem::zeroed();
            let mut dst_stat: libc::stat = std::mem::zeroed();
            if libc::fstat(input, &mut src_stat) != 0 || libc::fstat(output, &mut dst_stat) != 0 {
                libc::close(output);
                libc::close(input);
                libc::_exit(6);
            }
            if dst_stat.st_mode & libc::S_IFMT == libc::S_IFBLK
                && src_stat.st_mode & libc::S_IFMT == libc::S_IFREG
            {
                let capacity = libc::lseek(output, 0, libc::SEEK_END);
                if capacity > 0 && src_stat.st_size > capacity {
                    libc::close(output);
                    libc::close(input);
                    libc::_exit(7);
                }
                if libc::lseek(output, 0, libc::SEEK_SET) < 0 {
                    libc::close(output);
                    libc::close(input);
                    libc::_exit(8);
                }
            }
            let mut buffer = [0u8; 65536];
            loop {
                let n = libc::read(input, buffer.as_mut_ptr().cast(), buffer.len());
                if n > 0 {
                    let mut off = 0;
                    while off < n {
                        let w = libc::write(
                            output,
                            buffer.as_ptr().add(off as usize).cast(),
                            (n - off) as usize,
                        );
                        if w > 0 {
                            off += w;
                        } else if w < 0 && *libc::__errno() == libc::EINTR {
                            continue;
                        } else {
                            libc::close(output);
                            libc::close(input);
                            libc::_exit(4);
                        }
                    }
                } else if n == 0 {
                    break;
                } else if *libc::__errno() != libc::EINTR {
                    libc::close(output);
                    libc::close(input);
                    libc::_exit(5);
                }
            }
            libc::fsync(output);
            libc::close(output);
            libc::close(input);
            libc::_exit(0);
        }
    }
    let mut status = 0;
    while unsafe { libc::waitpid(pid, &mut status, 0) } < 0
        && unsafe { *libc::__errno() } == libc::EINTR
    {}
    if libc::WIFEXITED(status) && libc::WEXITSTATUS(status) == 0 {
        JNI_TRUE
    } else {
        JNI_FALSE
    }
}

fn find_vendor_boot(
    env: &mut JNIEnv,
    _class: JClass,
    slot: JString,
    allow_slotless: jboolean,
) -> jstring {
    let slot = get_utf(env, &slot).unwrap_or_default();
    let mut candidates = Vec::new();
    for base in ["/dev/block/by-name", "/dev/block/bootdevice/by-name"] {
        candidates.push(format!("{base}/vendor_boot{slot}"));
        if allow_slotless != 0 {
            candidates.push(format!("{base}/vendor_boot"));
        }
    }
    let candidates: Vec<CString> = candidates
        .into_iter()
        .filter_map(|s| CString::new(s).ok())
        .collect();
    let mut pipe = [0; 2];
    if unsafe { libc::pipe(pipe.as_mut_ptr()) } != 0 {
        return ptr::null_mut();
    }
    let pid = unsafe { libc::fork() };
    if pid < 0 {
        unsafe {
            libc::close(pipe[0]);
            libc::close(pipe[1]);
        }
        return ptr::null_mut();
    }
    if pid == 0 {
        unsafe {
            libc::close(pipe[0]);
            if libc::setresuid(0, 0, 0) != 0 || libc::getuid() != 0 {
                libc::_exit(1);
            }
            for candidate in &candidates {
                if libc::access(candidate.as_ptr(), libc::F_OK) != 0 {
                    continue;
                }
                let bytes = candidate.as_bytes_with_nul();
                let mut off = 0;
                while off < bytes.len() {
                    let w = libc::write(pipe[1], bytes.as_ptr().add(off).cast(), bytes.len() - off);
                    if w > 0 {
                        off += w as usize;
                    } else if w < 0 && *libc::__errno() == libc::EINTR {
                        continue;
                    } else {
                        libc::_exit(2);
                    }
                }
                libc::close(pipe[1]);
                libc::_exit(0);
            }
            libc::_exit(3);
        }
    }
    unsafe {
        libc::close(pipe[1]);
    }
    let mut data = Vec::new();
    let mut buffer = [0u8; 256];
    loop {
        let n = unsafe { libc::read(pipe[0], buffer.as_mut_ptr().cast(), buffer.len()) };
        if n > 0 {
            data.extend_from_slice(&buffer[..n as usize]);
        } else if n <= 0 {
            break;
        }
    }
    unsafe {
        libc::close(pipe[0]);
    }
    let mut status = 0;
    while unsafe { libc::waitpid(pid, &mut status, 0) } < 0
        && unsafe { *libc::__errno() } == libc::EINTR
    {}
    if data.is_empty() || !libc::WIFEXITED(status) || libc::WEXITSTATUS(status) != 0 {
        return ptr::null_mut();
    }
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    put_string(env, &String::from_utf8_lossy(&data[..end]))
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

fn is_gki(_env: &mut JNIEnv, _class: JClass) -> jboolean {
    uname_release()
        .and_then(|r| parse_gki(&r))
        .map(|(gki, _)| if gki { JNI_TRUE } else { JNI_FALSE })
        .unwrap_or(JNI_FALSE)
}

fn kernel_version(env: &mut JNIEnv, _class: JClass) -> jstring {
    match uname_release().and_then(|r| parse_gki(&r)) {
        Some((_, version)) => put_string(env, &version),
        None => ptr::null_mut(),
    }
}

fn kernel_release(env: &mut JNIEnv, _class: JClass) -> jstring {
    match uname_release() {
        Some(release) => put_string(env, &release),
        None => ptr::null_mut(),
    }
}

fn module_version(env: &mut JNIEnv, _class: JClass) -> jstring {
    let Some(fd) = ensure_ctl_fd() else {
        return ptr::null_mut();
    };
    let mut data = [0u8; 64];
    if ioc_call(fd, IOC_GET_VERSION, &mut data) < 0 {
        return ptr::null_mut();
    }
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    put_string(env, &String::from_utf8_lossy(&data[..end]))
}

fn feature_list(env: &mut JNIEnv, _class: JClass) -> jstring {
    let Some(fd) = ensure_ctl_fd() else {
        return ptr::null_mut();
    };
    let mut data = [0u8; 1024];
    if ioc_call(fd, IOC_FEATURE_LIST, &mut data) < 0 {
        return ptr::null_mut();
    }
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    put_string(env, &String::from_utf8_lossy(&data[..end]))
}

fn feature_get(_env: &mut JNIEnv, _class: JClass, id: jint) -> jlong {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    let mut data = [0u8; FMAC_DATA_FEATURE];
    data[..4].copy_from_slice(&(id as u32).to_ne_bytes());
    if ioc_call(fd, IOC_FEATURE_GET, &mut data) < 0 {
        return -1;
    }
    u64::from_ne_bytes(
        data[FMAC_OFF_FEATURE_VALUE..FMAC_OFF_FEATURE_VALUE + 8]
            .try_into()
            .unwrap(),
    ) as jlong
}

fn feature_set(_env: &mut JNIEnv, _class: JClass, id: jint, value: jlong) -> jint {
    let Some(fd) = ensure_ctl_fd() else { return -1 };
    let mut data = [0u8; FMAC_DATA_FEATURE];
    data[..4].copy_from_slice(&(id as u32).to_ne_bytes());
    data[FMAC_OFF_FEATURE_VALUE..FMAC_OFF_FEATURE_VALUE + 8]
        .copy_from_slice(&(value as u64).to_ne_bytes());
    if ioc_call(fd, IOC_FEATURE_SET, &mut data) < 0 {
        -1
    } else {
        0
    }
}

fn get_slot_suffix(env: &mut JNIEnv, _class: JClass) -> jstring {
    unsafe extern "C" {
        fn __system_property_get(
            name: *const libc::c_char,
            value: *mut libc::c_char,
        ) -> libc::c_int;
    }
    let mut value = [0 as libc::c_char; 92];
    let key = CString::new("ro.boot.slot_suffix").unwrap();
    let len = unsafe { __system_property_get(key.as_ptr(), value.as_mut_ptr()) };
    if len > 0 && value[0] != 0 {
        let text = unsafe { CStr::from_ptr(value.as_ptr()) };
        return put_string(env, &text.to_string_lossy());
    }
    let key = CString::new("ro.boot.slot").unwrap();
    let len = unsafe { __system_property_get(key.as_ptr(), value.as_mut_ptr()) };
    if len > 0 && value[0] != 0 {
        let text = unsafe { CStr::from_ptr(value.as_ptr()) };
        return put_string(env, &format!("_{}", text.to_string_lossy()));
    }
    ptr::null_mut()
}

fn hello_log(_env: &mut JNIEnv, _class: JClass) {
    log::info!("ncore jni (Rust) loaded");
}

#[unsafe(no_mangle)]
pub extern "system" fn JNI_OnLoad(vm: JavaVM, _reserved: *mut libc::c_void) -> jint {
    android_logger::init_once(
        android_logger::Config::default()
            .with_max_level(log::LevelFilter::Info)
            .with_tag("ncore_jni"),
    );
    let mut env = vm.get_env().expect("JNI env");
    let Ok(class) = env.find_class("me/nekosu/aqnya/core/Ncore") else {
        return JNI_VERSION_1_6;
    };
    let methods = [
        NativeMethod {
            name: "ctl".into(),
            sig: "(I)I".into(),
            fn_ptr: ctl as *mut _,
        },
        NativeMethod {
            name: "setProfile".into(),
            sig: "(IIJLjava/lang/String;I)I".into(),
            fn_ptr: set_profile as *mut _,
        },
        NativeMethod {
            name: "adduid".into(),
            sig: "(I)I".into(),
            fn_ptr: add_uid as *mut _,
        },
        NativeMethod {
            name: "deluid".into(),
            sig: "(I)I".into(),
            fn_ptr: del_uid as *mut _,
        },
        NativeMethod {
            name: "hasuid".into(),
            sig: "(I)I".into(),
            fn_ptr: has_uid as *mut _,
        },
        NativeMethod {
            name: "setCap".into(),
            sig: "(IJ)I".into(),
            fn_ptr: set_cap as *mut _,
        },
        NativeMethod {
            name: "getCap".into(),
            sig: "(I)J".into(),
            fn_ptr: get_cap as *mut _,
        },
        NativeMethod {
            name: "delCap".into(),
            sig: "(I)I".into(),
            fn_ptr: del_cap as *mut _,
        },
        NativeMethod {
            name: "addSelinuxRule".into(),
            sig: "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IZ)I"
                .into(),
            fn_ptr: add_selinux_rule as *mut _,
        },
        NativeMethod {
            name: "addRule".into(),
            sig: "(Ljava/lang/String;J)I".into(),
            fn_ptr: add_rule as *mut _,
        },
        NativeMethod {
            name: "delRule".into(),
            sig: "(Ljava/lang/String;)I".into(),
            fn_ptr: del_rule as *mut _,
        },
        NativeMethod {
            name: "listProfiles".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: list_profiles as *mut _,
        },
        NativeMethod {
            name: "readFile".into(),
            sig: "(Ljava/lang/String;)[B".into(),
            fn_ptr: read_file as *mut _,
        },
        NativeMethod {
            name: "readFilePersistent".into(),
            sig: "(Ljava/lang/String;)[B".into(),
            fn_ptr: read_file_persistent as *mut _,
        },
        NativeMethod {
            name: "writeBlock".into(),
            sig: "(Ljava/lang/String;Ljava/lang/String;)Z".into(),
            fn_ptr: write_block as *mut _,
        },
        NativeMethod {
            name: "helloLog".into(),
            sig: "()V".into(),
            fn_ptr: hello_log as *mut _,
        },
        NativeMethod {
            name: "isGki".into(),
            sig: "()Z".into(),
            fn_ptr: is_gki as *mut _,
        },
        NativeMethod {
            name: "kernelVersion".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: kernel_version as *mut _,
        },
        NativeMethod {
            name: "kernelRelease".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: kernel_release as *mut _,
        },
        NativeMethod {
            name: "moduleVersion".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: module_version as *mut _,
        },
        NativeMethod {
            name: "featureList".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: feature_list as *mut _,
        },
        NativeMethod {
            name: "featureGet".into(),
            sig: "(I)J".into(),
            fn_ptr: feature_get as *mut _,
        },
        NativeMethod {
            name: "featureSet".into(),
            sig: "(IJ)I".into(),
            fn_ptr: feature_set as *mut _,
        },
        NativeMethod {
            name: "getSlotSuffix".into(),
            sig: "()Ljava/lang/String;".into(),
            fn_ptr: get_slot_suffix as *mut _,
        },
        NativeMethod {
            name: "findVendorBoot".into(),
            sig: "(Ljava/lang/String;Z)Ljava/lang/String;".into(),
            fn_ptr: find_vendor_boot as *mut _,
        },
    ];
    if env.register_native_methods(class, &methods).is_err() {
        log::error!("register_native_methods failed");
    }
    JNI_VERSION_1_6
}

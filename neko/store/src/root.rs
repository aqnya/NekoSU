use std::io;

pub fn ensure_root() -> io::Result<()> {
    {
        let ok =
            unsafe { libc::setresuid(0, 0, 0) } == 0 && unsafe { libc::setresgid(0, 0, 0) } == 0;
        if !ok {
            let err = io::Error::last_os_error();
            log::error!("setresuid/setresgid failed: {err}");
            return Err(err);
        }
    }
    Ok(())
}

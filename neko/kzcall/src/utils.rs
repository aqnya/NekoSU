pub fn put_u32(buf: &mut [u8], off: usize, v: u32) {
    buf[off..off + 4].copy_from_slice(&v.to_ne_bytes());
}

pub fn put_u64(buf: &mut [u8], off: usize, v: u64) {
    buf[off..off + 8].copy_from_slice(&v.to_ne_bytes());
}

pub fn get_u64(buf: &[u8], off: usize) -> u64 {
    u64::from_ne_bytes(buf[off..off + 8].try_into().unwrap())
}

pub fn put_cstr(field: &mut [u8], value: &str) {
    let Some(max) = field.len().checked_sub(1) else {
        return;
    };
    let len = value.len().min(max);
    field[..len].copy_from_slice(&value.as_bytes()[..len]);
    field[len] = 0;
}

pub fn cstr_lossy(data: &[u8]) -> String {
    let end = data.iter().position(|b| *b == 0).unwrap_or(data.len());
    String::from_utf8_lossy(&data[..end]).into_owned()
}

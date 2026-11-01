// SPDX-License-Identifier: GPL-3.0-or-later
//! Small helpers shared by the parsers and the patcher.

use zerocopy::FromBytes;

/// Round `n` up to the next multiple of `alignment`.
pub fn align_to(n: usize, alignment: usize) -> usize {
    debug_assert!(
        alignment.is_power_of_two(),
        "alignment must be a power of two"
    );
    (n + alignment - 1) & !(alignment - 1)
}

/// Read a little-endian `u32` at `offset`, or `None` when the slice is short.
pub fn read_u32_le(bytes: &[u8], offset: usize) -> Option<u32> {
    let end = offset.checked_add(4)?;
    Some(u32::from_le_bytes(bytes.get(offset..end)?.try_into().ok()?))
}

/// Reinterpret the first `size_of::<T>()` bytes as `T`.
pub fn read_struct<T: FromBytes>(bytes: &[u8]) -> Option<T> {
    T::read_from_bytes(bytes.get(..core::mem::size_of::<T>())?).ok()
}

/// Reinterpret `size_of::<T>()` bytes at `offset` as `T`.
pub fn read_struct_at<T: FromBytes>(bytes: &[u8], offset: usize) -> Option<T> {
    read_struct(bytes.get(offset..)?)
}

/// Strip everything from the first NUL byte on.
pub fn trim_nul(bytes: &[u8]) -> &[u8] {
    &bytes[..bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len())]
}

/// Grow `out` with zero bytes until its length is a multiple of `alignment`.
pub fn pad_to(out: &mut Vec<u8>, alignment: usize) {
    let aligned = align_to(out.len(), alignment);
    out.resize(aligned, 0);
}

//! Android Binary XML (ABX) reader — user-space port of `manager/abx.c`.
#![allow(dead_code)]

const ABX_TOKEN_START_DOCUMENT: u8 = 0;
const ABX_TOKEN_END_DOCUMENT: u8 = 1;
const ABX_TOKEN_START_TAG: u8 = 2;
const ABX_TOKEN_END_TAG: u8 = 3;
const ABX_TOKEN_TEXT: u8 = 4;
const ABX_TOKEN_ATTRIBUTE: u8 = 15;

const ABX_TYPE_NULL: u8 = 0x10;
const ABX_TYPE_STRING: u8 = 0x20;
const ABX_TYPE_STRING_INTERNED: u8 = 0x30;
const ABX_TYPE_BYTES_HEX: u8 = 0x40;
const ABX_TYPE_BYTES_BASE64: u8 = 0x50;
const ABX_TYPE_INT: u8 = 0x60;
const ABX_TYPE_INT_HEX: u8 = 0x70;
const ABX_TYPE_LONG: u8 = 0x80;
const ABX_TYPE_LONG_HEX: u8 = 0x90;
const ABX_TYPE_FLOAT: u8 = 0xa0;
const ABX_TYPE_DOUBLE: u8 = 0xb0;
const ABX_TYPE_BOOLEAN_TRUE: u8 = 0xc0;
const ABX_TYPE_BOOLEAN_FALSE: u8 = 0xd0;

const ABX_INTERNED_SENTINEL: u16 = 0xffff;
const ABX_MAX_INTERNED_STRINGS: usize = 512;

const ABX_MAGIC: &[u8; 4] = b"ABX\0";

/// Information captured from a `<package>` entry.
#[derive(Debug, Default, Clone)]
pub struct Package {
    /// DER certificate, owned copy of the slice in `buf`.
    pub cert: Vec<u8>,
    pub name: String,
    pub uid: u32,
    pub has_uid: bool,
    pub code_path: String,
    pub native_lib_path: String,
}

#[derive(Debug)]
pub enum AbxOutcome {
    Found(Package),
    /// Document parsed to the end, but no package matched.
    NotFound,
    /// Truncated, malformed, or out of memory.
    ParseError,
}

struct Reader {
    buf: Vec<u8>,
    pos: usize,
    interned: Vec<Vec<u8>>,
}

impl Reader {
    fn read_byte(&mut self) -> Option<u8> {
        let b = *self.buf.get(self.pos)?;
        self.pos += 1;
        Some(b)
    }

    fn read_u16(&mut self) -> Option<u16> {
        if self.pos + 2 > self.buf.len() {
            return None;
        }
        let v = ((self.buf[self.pos] as u16) << 8) | self.buf[self.pos + 1] as u16;
        self.pos += 2;
        Some(v)
    }

    fn read_u64(&mut self) -> Option<u64> {
        if self.pos + 8 > self.buf.len() {
            return None;
        }
        let mut v: u64 = 0;
        for i in 0..8 {
            v = (v << 8) | self.buf[self.pos + i] as u64;
        }
        self.pos += 8;
        Some(v)
    }

    fn read_utf(&mut self) -> Option<Vec<u8>> {
        let n = self.read_u16()? as usize;
        if self.pos + n > self.buf.len() {
            return None;
        }
        let s = self.buf[self.pos..self.pos + n].to_vec();
        self.pos += n;
        Some(s)
    }

    fn read_interned(&mut self) -> Option<Vec<u8>> {
        let r = self.read_u16()?;
        if r == ABX_INTERNED_SENTINEL {
            let s = self.read_utf()?;
            if self.interned.len() >= ABX_MAX_INTERNED_STRINGS {
                return None;
            }
            self.interned.push(s);
            self.interned.last().cloned()
        } else {
            self.interned.get(r as usize).cloned()
        }
    }
}

fn truncate_to_string(data: &[u8], max: usize) -> String {
    let n = data.len().min(max);
    String::from_utf8_lossy(&data[..n]).into_owned()
}

/// Find the first `<package>` whose certificate is accepted by `match_fn`.
pub fn find_package_by_cert<F>(buf: Vec<u8>, mut match_fn: F) -> AbxOutcome
where
    F: FnMut(&[u8]) -> bool,
{
    if buf.len() < ABX_MAGIC.len() || &buf[..4] != ABX_MAGIC {
        return AbxOutcome::ParseError;
    }

    let mut r = Reader {
        buf,
        pos: ABX_MAGIC.len(),
        interned: Vec::new(),
    };

    let mut depth: i32 = 0;
    let mut pkg_depth: i32 = -1;
    let mut sigs_depth: i32 = -1;

    let mut pkg = Package::default();
    let mut cert: Option<Vec<u8>> = None;
    let mut cert_checked = false;

    let mut document_complete = false;

    while r.pos < r.buf.len() {
        let ev = match r.read_byte() {
            Some(b) => b,
            None => break,
        };
        let tok = ev & 0x0f;
        let typ = ev & 0xf0;

        match tok {
            ABX_TOKEN_START_DOCUMENT => continue,
            ABX_TOKEN_END_DOCUMENT => {
                document_complete = depth == 0;
                break;
            }
            ABX_TOKEN_START_TAG => {
                let tag_name = match r.read_interned() {
                    Some(s) => s,
                    None => return AbxOutcome::ParseError,
                };
                depth += 1;

                let is_sigs = pkg_depth > 0
                    && sigs_depth < 0
                    && depth == pkg_depth + 1
                    && tag_name.as_slice() == b"sigs";
                let is_cert =
                    sigs_depth > 0 && depth == sigs_depth + 1 && tag_name.as_slice() == b"cert";

                let mut name_attr: Option<Vec<u8>> = None;
                let mut code_attr: Option<Vec<u8>> = None;
                let mut nlib_attr: Option<Vec<u8>> = None;
                let mut shared_uid: u32 = 0;
                let mut own_uid: u32 = 0;
                let mut shared_seen = false;
                let mut own_seen = false;

                // Consume attributes until the next non-attribute event.
                loop {
                    let save = r.pos;
                    let ev2 = match r.read_byte() {
                        Some(b) => b,
                        None => {
                            r.pos = save;
                            break;
                        }
                    };
                    if (ev2 & 0x0f) != ABX_TOKEN_ATTRIBUTE {
                        r.pos = save;
                        break;
                    }
                    let typ2 = ev2 & 0xf0;
                    let aname = match r.read_interned() {
                        Some(s) => s,
                        None => return AbxOutcome::ParseError,
                    };
                    if !read_attr(
                        &mut r,
                        typ2,
                        &aname,
                        is_cert,
                        &mut cert,
                        &mut name_attr,
                        &mut code_attr,
                        &mut nlib_attr,
                        &mut shared_uid,
                        &mut own_uid,
                        &mut shared_seen,
                        &mut own_seen,
                    ) {
                        return AbxOutcome::ParseError;
                    }
                }

                let is_pkg = pkg_depth < 0 && tag_name.as_slice() == b"package";

                if is_pkg {
                    pkg_depth = depth;
                    cert = None;
                    cert_checked = false;
                    pkg = Package {
                        cert: Vec::new(),
                        name: truncate_to_string(&name_attr.unwrap_or_default(), 255),
                        code_path: truncate_to_string(&code_attr.unwrap_or_default(), 255),
                        native_lib_path: truncate_to_string(&nlib_attr.unwrap_or_default(), 255),
                        uid: if shared_seen { shared_uid } else { own_uid },
                        has_uid: shared_seen || own_seen,
                    };
                }
                if is_sigs {
                    sigs_depth = depth;
                }

                if let Some(c) = cert.take() {
                    if !cert_checked {
                        cert_checked = true;
                        if match_fn(&c) {
                            pkg.cert = c;
                            return AbxOutcome::Found(pkg);
                        }
                    }
                }
            }
            ABX_TOKEN_END_TAG => {
                if r.read_interned().is_none() {
                    return AbxOutcome::ParseError;
                }
                if depth == pkg_depth {
                    pkg_depth = -1;
                    cert = None;
                }
                if depth == sigs_depth {
                    sigs_depth = -1;
                }
                depth -= 1;
                if depth < 0 {
                    return AbxOutcome::ParseError;
                }
            }
            // TEXT(4), CDSECT(5), ENTITY_REF(6), IGNORABLE_WHITESPACE(7),
            // PROCESSING_INSTRUCTION(8), COMMENT(9), DOCDECL(10)
            ABX_TOKEN_TEXT | 5 | 6 | 7 | 8 | 9 | 10 => match typ {
                ABX_TYPE_STRING => {
                    let n = match r.read_u16() {
                        Some(v) => v as usize,
                        None => return AbxOutcome::ParseError,
                    };
                    if r.pos + n > r.buf.len() {
                        return AbxOutcome::ParseError;
                    }
                    r.pos += n;
                }
                ABX_TYPE_STRING_INTERNED => {
                    if r.read_interned().is_none() {
                        return AbxOutcome::ParseError;
                    }
                }
                ABX_TYPE_NULL => {}
                _ => return AbxOutcome::ParseError,
            },
            _ => return AbxOutcome::ParseError,
        }
    }

    if document_complete {
        AbxOutcome::NotFound
    } else {
        AbxOutcome::ParseError
    }
}

#[allow(clippy::too_many_arguments)]
fn read_attr(
    r: &mut Reader,
    typ: u8,
    aname: &[u8],
    is_cert: bool,
    cert: &mut Option<Vec<u8>>,
    name_attr: &mut Option<Vec<u8>>,
    code_attr: &mut Option<Vec<u8>>,
    nlib_attr: &mut Option<Vec<u8>>,
    shared_uid: &mut u32,
    own_uid: &mut u32,
    shared_seen: &mut bool,
    own_seen: &mut bool,
) -> bool {
    match typ {
        ABX_TYPE_BYTES_HEX | ABX_TYPE_BYTES_BASE64 => {
            let blen = match r.read_u16() {
                Some(v) => v as usize,
                None => return false,
            };
            if r.pos + blen > r.buf.len() {
                return false;
            }
            if is_cert && aname == b"key" {
                *cert = Some(r.buf[r.pos..r.pos + blen].to_vec());
            }
            r.pos += blen;
        }
        ABX_TYPE_STRING => {
            let s = match r.read_utf() {
                Some(s) => s,
                None => return false,
            };
            assign_string(aname, s, name_attr, code_attr, nlib_attr);
        }
        ABX_TYPE_STRING_INTERNED => {
            let s = match r.read_interned() {
                Some(s) => s,
                None => return false,
            };
            assign_string(aname, s, name_attr, code_attr, nlib_attr);
        }
        ABX_TYPE_INT | ABX_TYPE_INT_HEX => {
            if r.pos + 4 > r.buf.len() {
                return false;
            }
            let v = u32::from_be_bytes([
                r.buf[r.pos],
                r.buf[r.pos + 1],
                r.buf[r.pos + 2],
                r.buf[r.pos + 3],
            ]);
            r.pos += 4;
            if aname == b"sharedUserId" {
                *shared_uid = v;
                *shared_seen = true;
            } else if aname == b"userId" {
                *own_uid = v;
                *own_seen = true;
            }
        }
        ABX_TYPE_FLOAT => {
            if r.pos + 4 > r.buf.len() {
                return false;
            }
            r.pos += 4;
        }
        ABX_TYPE_LONG | ABX_TYPE_LONG_HEX | ABX_TYPE_DOUBLE => {
            if r.read_u64().is_none() {
                return false;
            }
        }
        ABX_TYPE_BOOLEAN_TRUE | ABX_TYPE_BOOLEAN_FALSE | ABX_TYPE_NULL => {}
        _ => return false,
    }
    true
}

fn assign_string(
    aname: &[u8],
    s: Vec<u8>,
    name_attr: &mut Option<Vec<u8>>,
    code_attr: &mut Option<Vec<u8>>,
    nlib_attr: &mut Option<Vec<u8>>,
) {
    if aname == b"name" {
        *name_attr = Some(s);
    } else if aname == b"codePath" {
        *code_attr = Some(s);
    } else if aname == b"nativeLibraryPath" {
        *nlib_attr = Some(s);
    }
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_MANAGER_ABX_H
#define NKSU_MANAGER_ABX_H

#include <linux/types.h>

/*
 * Minimal parser for Android Binary XML (ABX), the format
 * com.android.modules.utils.BinaryXmlSerializer produces.  Used to pull a
 * package's signing certificate and on-disk paths out of
 * /data/system/packages.xml.
 *
 * The wire format: every event is a single byte whose low nibble is an
 * XmlPullParser token and high nibble an optional value-type tag.  Strings are
 * a 2-byte big-endian length followed by Modified UTF-8.  Names written
 * through writeInternedUTF() are canonicalized: the first occurrence is a
 * 0xffff sentinel followed by the full string (appended to the intern table),
 * later occurrences a 2-byte big-endian index.
 */

struct abx_package {
	/* DER certificate from <sigs><cert key="...">: a view into the
	 * caller's buffer, not NUL-terminated. */
	const u8 *cert;
	size_t cert_len;
	/* The matched package's own name attribute (its installed name), so the
	 * caller can resolve its uid without hard-coding the name. */
	char name[256];
	/* codePath / nativeLibraryPath attributes, NUL-terminated. */
	char code_path[256];
	char native_lib_path[256];
};

/*
 * Find the first <package> whose sharedUserId attribute equals `shared_user`
 * in the ABX document buf[0..len) and fill `out`.  Matching the shared user
 * (instead of a fixed package name) lets the manager be reinstalled under a
 * new package name as long as it keeps the same sharedUserId and signing key,
 * and lets every app in that shared uid count as the manager.  Returns true
 * when such a package and a certificate were found.
 */
bool abx_find_package_by_shared_user(const u8 *buf, size_t len,
				     const char *shared_user,
				     struct abx_package *out);

#endif /* NKSU_MANAGER_ABX_H */

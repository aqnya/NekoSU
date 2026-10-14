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
	/* codePath / nativeLibraryPath attributes, NUL-terminated. */
	char code_path[256];
	char native_lib_path[256];
};

/*
 * Find <package name="package_name"> in the ABX document buf[0..len) and fill
 * `out`.  Returns true when the package and a certificate were found.
 */
bool abx_find_package(const u8 *buf, size_t len, const char *package_name,
		      struct abx_package *out);

#endif /* NKSU_MANAGER_ABX_H */

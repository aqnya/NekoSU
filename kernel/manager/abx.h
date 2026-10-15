/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_MANAGER_ABX_H
#define NKSU_MANAGER_ABX_H

#include <linux/types.h>

/*
 * Minimal parser for Android Binary XML (ABX), the format
 * com.android.modules.utils.BinaryXmlSerializer produces.  Used to pull a
 * package's signing certificate, user id and on-disk paths out of
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
	/* The package's own name attribute (its installed name). */
	char name[256];
	/* The package's user id: the numeric sharedUserId for a shared-user
	 * member, otherwise the numeric userId.  Both are written as ints. */
	u32 uid;
	bool has_uid;
	/* codePath / nativeLibraryPath attributes, NUL-terminated. */
	char code_path[256];
	char native_lib_path[256];
};

/* Accept or reject a package by its DER certificate.  Return true to take it. */
typedef bool (*abx_cert_match)(const u8 *cert, size_t cert_len, void *ctx);

/*
 * Find the first <package> whose certificate is accepted by `match` and fill
 * `out` (including its name and user id).  Matching the signing certificate
 * means the manager is recognised no matter what package name it is installed
 * under, and lets the caller read the shared uid straight from the entry.
 * Returns true when such a package and a certificate were found.
 */
bool abx_find_package_by_cert(const u8 *buf, size_t len,
			      abx_cert_match match, void *ctx,
			      struct abx_package *out);

#endif /* NKSU_MANAGER_ABX_H */

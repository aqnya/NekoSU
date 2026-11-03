/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NKSU_NCORE_SIGNATURE_H
#define NKSU_NCORE_SIGNATURE_H

#include <linux/fs.h>
#include <linux/types.h>

/*
 * ncore is built and signed by neko/build.gradle.kts: the last 512 bytes of
 * libncore.so are an RSA-4096/SHA-256 PKCS#1 v1.5 signature over everything
 * before them.  These helpers verify that signature with a public key pinned
 * in the kernel, so only a binary built by the release signing key is ever
 * installed or allowed to grant manager rights.
 *
 * The path/file variants fail closed on any read, crypto or signature error.
 * nksu_ncore_verify_caller() verifies the executable of the calling task.
 */
int nksu_ncore_verify_file(struct file *file, const char *what);
int nksu_ncore_verify_path(const char *path);
int nksu_ncore_verify_caller(void);

#endif /* NKSU_NCORE_SIGNATURE_H */

// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Kernel feature registry, driven by the manager over IOC_FEATURE_*.
 *
 * KernelSU keeps an equivalent table (policy/feature.c) and lets ksud flip the
 * switches; here the manager talks to the kernel directly through the control
 * fd, so there is no userspace daemon in the loop.
 */
#include "klog.h"
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "manager/feature.h"
#include "selinux/selinux.h"

#define NKSU_MAX_FEATURES 16

static DEFINE_MUTEX(nksu_feature_mutex);
static const struct nksu_feature *nksu_features[NKSU_MAX_FEATURES];
static int nksu_feature_count;

static const struct nksu_feature *nksu_feature_find(u32 id)
{
    int i;

    for (i = 0; i < nksu_feature_count; i++) {
        if (nksu_features[i]->id == id)
            return nksu_features[i];
    }
    return NULL;
}

int nksu_feature_register(const struct nksu_feature *feature)
{
    int i, ret = 0;

    if (!feature || !feature->name)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    for (i = 0; i < nksu_feature_count; i++) {
        if (nksu_features[i]->id == feature->id) {
            ret = -EEXIST;
            goto out;
        }
    }
    if (nksu_feature_count >= NKSU_MAX_FEATURES) {
        ret = -ENOSPC;
        goto out;
    }
    nksu_features[nksu_feature_count++] = feature;
out:
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_get(u32 id, u64 *value)
{
    const struct nksu_feature *feature;
    int ret = -ENOENT;

    if (!value)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    feature = nksu_feature_find(id);
    if (feature && feature->get)
        ret = feature->get(value);
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_set(u32 id, u64 value)
{
    const struct nksu_feature *feature;
    int ret = -ENOENT;

    mutex_lock(&nksu_feature_mutex);
    feature = nksu_feature_find(id);
    if (feature && feature->set)
        ret = feature->set(value);
    mutex_unlock(&nksu_feature_mutex);
    return ret;
}

int nksu_feature_render(char *buf, size_t size)
{
    int i, len = 0;

    if (!buf || size == 0)
        return -EINVAL;

    mutex_lock(&nksu_feature_mutex);
    for (i = 0; i < nksu_feature_count; i++) {
        const struct nksu_feature *feature = nksu_features[i];
        u64 value = 0;

        if (feature->get)
            feature->get(&value);

        len += scnprintf(buf + len, size - len, "%u %s %llu\n", feature->id,
                         feature->name, (unsigned long long)value);
        if ((size_t)len >= size)
            break;
    }
    mutex_unlock(&nksu_feature_mutex);

    return len;
}

/*
 * ── persistent enabled-feature set ──
 *
 * /data/adb/nksu/feature holds one enabled feature name per line.  The kernel
 * owns it and edits only its own entry, so several features can share the file.
 * File I/O borrows the nksu domain (unconfined, and the only domain allowed to
 * touch /data/adb); the cred is built per operation, since toggles and the boot
 * read are rare.
 */
#define NKSU_FEATURE_FILE "/data/adb/nksu/feature"
#define NKSU_FEATURE_FILE_MAX 4096

/* True when `line` is exactly `name` (surrounding blanks ignored). */
static bool nksu_feature_name_in_line(const char *line, const char *name)
{
    size_t nlen = strlen(name), llen;

    while (*line == ' ' || *line == '\t' || *line == '\r')
        line++;
    llen = strlen(line);
    while (llen && (line[llen - 1] == ' ' || line[llen - 1] == '\t' ||
                    line[llen - 1] == '\r'))
        llen--;

    return llen == nlen && memcmp(line, name, nlen) == 0;
}

bool nksu_feature_persist_has(const char *name)
{
    const struct cred *old;
    struct file *fp;
    char *buf;
    loff_t pos = 0, size;
    bool found = false;

    if (!name || !name[0])
        return false;

    old = nksu_borrow_creds_begin();
    if (!old)
        return false;

    fp = filp_open(NKSU_FEATURE_FILE, O_RDONLY, 0);
    if (IS_ERR(fp))
        goto out;

    size = i_size_read(fp->f_inode);
    if (size <= 0 || size > NKSU_FEATURE_FILE_MAX) {
        filp_close(fp, NULL);
        goto out;
    }

    buf = kvmalloc((size_t)size + 1, GFP_KERNEL);
    if (!buf) {
        filp_close(fp, NULL);
        goto out;
    }

    {
        ssize_t rd = kernel_read(fp, buf, (size_t)size, &pos);

        filp_close(fp, NULL);
        if (rd > 0) {
            char *cursor = buf;

            buf[rd] = '\0';
            while (cursor && *cursor) {
                char *line = strsep(&cursor, "\n");

                if (line && nksu_feature_name_in_line(line, name)) {
                    found = true;
                    break;
                }
            }
        }
    }
    kvfree(buf);

out:
    nksu_borrow_creds_end(old);
    return found;
}

int nksu_feature_persist_set(const char *name, bool enabled)
{
    const struct cred *old;
    struct file *fp;
    char *in = NULL, *out;
    loff_t pos = 0, size;
    size_t out_cap, out_len = 0;
    bool present = false;
    int ret = 0;

    if (!name || !name[0])
        return -EINVAL;

    old = nksu_borrow_creds_begin();
    if (!old)
        return -ENOMEM;

    fp = filp_open(NKSU_FEATURE_FILE, O_RDONLY, 0);
    if (!IS_ERR(fp)) {
        size = i_size_read(fp->f_inode);
        if (size > 0 && size <= NKSU_FEATURE_FILE_MAX) {
            in = kvmalloc((size_t)size + 1, GFP_KERNEL);
            if (in) {
                ssize_t rd = kernel_read(fp, in, (size_t)size, &pos);

                if (rd > 0)
                    in[rd] = '\0';
                else {
                    kvfree(in);
                    in = NULL;
                }
            }
        }
        filp_close(fp, NULL);
    } else if (PTR_ERR(fp) != -ENOENT) {
        ret = PTR_ERR(fp);
        goto out;
    }

    out_cap = NKSU_FEATURE_FILE_MAX + strlen(name) + 2;
    out = kvmalloc(out_cap, GFP_KERNEL);
    if (!out) {
        kvfree(in);
        ret = -ENOMEM;
        goto out;
    }

    if (in) {
        char *cursor = in;

        while (cursor && *cursor) {
            char *line = strsep(&cursor, "\n");

            if (!line || !*line)
                continue;
            if (nksu_feature_name_in_line(line, name)) {
                present = true;
                if (!enabled)
                    continue; /* drop our own entry */
            }
            out_len += scnprintf(out + out_len, out_cap - out_len, "%s\n",
                                 line);
        }
    }

    if (enabled && !present)
        out_len += scnprintf(out + out_len, out_cap - out_len, "%s\n", name);

    fp = filp_open(NKSU_FEATURE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (IS_ERR(fp)) {
        ret = PTR_ERR(fp);
    } else {
        pos = 0;
        kernel_write(fp, out, out_len, &pos);
        filp_close(fp, NULL);
    }

    kvfree(out);
    kvfree(in);

out:
    nksu_borrow_creds_end(old);
    return ret;
}

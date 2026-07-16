/*
 * vfs_path.c - lexical path normalization (pure).
 *
 * Turns any path into its canonical absolute form before resolution,
 * so "." and ".." never reach a filesystem. Lexical ".." handling is
 * *correct* here, not merely convenient, because the graphfs v1
 * namespace is a strict tree (single-parent NAMESPACE nodes, no
 * symlinks): every directory has exactly one name, and dropping the
 * last component of a canonical path is exactly its parent.
 *
 * Pure C over the caller's buffers — no kernel dependencies — so the
 * host suite tests it under ASan/UBSan (tests/host/test_vfs_path.c).
 */
#include <vfs.h>

#include <errno.h>
#include <string.h>

long vfs_path_norm(const char *in, char *out, size_t cap) {
    if (in == NULL || in[0] == '\0') {
        return -EINVAL;
    }
    if (cap < 2) {
        return -ENAMETOOLONG;
    }
    out[0] = '/';
    size_t olen = 1;

    const char *p = in;
    while (*p == '/') {
        p++;
    }
    while (*p != '\0') {
        size_t n = 0;
        while (p[n] != '\0' && p[n] != '/') {
            n++;
        }
        if (n == 1 && p[0] == '.') {
            /* "." - stay */
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            /* ".." - pop the last component; above the root, stay
             * at the root (POSIX resolution, /.. = /). */
            while (olen > 1 && out[olen - 1] != '/') {
                olen--;
            }
            if (olen > 1) {
                olen--; /* the separator too */
            }
        } else {
            if (n > VFS_NAME_MAX) {
                return -ENAMETOOLONG;
            }
            size_t sep = (olen > 1) ? 1 : 0;
            if (olen + sep + n + 1 > cap) {
                return -ENAMETOOLONG;
            }
            if (sep != 0) {
                out[olen++] = '/';
            }
            memcpy(out + olen, p, n);
            olen += n;
        }
        p += n;
        while (*p == '/') {
            p++;
        }
    }
    out[olen] = '\0';
    return 0;
}

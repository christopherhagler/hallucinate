/*
 * test_vfs_path.c - the VFS lexical path normalizer.
 *
 * vfs_path_norm is the only piece of path handling that is not a
 * filesystem's own resolver, and everything downstream (mount match,
 * gfs_resolve) assumes its output is canonical: absolute, no ".",
 * "..", empty components, or trailing slash. Table-driven happy
 * paths plus every rejection.
 */
#include "test.h"

#include <vfs.h>

/* The kernel's errno values, pinned so a host <errno.h> pulled in
 * transitively can never skew the expectations. */
#undef EINVAL
#define EINVAL 22
#undef ENAMETOOLONG
#define ENAMETOOLONG 36

TEST(vfs_path_canonical_forms) {
    static const struct {
        const char *in;
        const char *want;
    } cases[] = {
        {"/", "/"},
        {"//", "/"},
        {"///", "/"},
        {"/bin/init", "/bin/init"},
        {"/bin//init", "/bin/init"},
        {"/bin/init/", "/bin/init"},
        {"/./bin/./init", "/bin/init"},
        {"/bin/../dev/console", "/dev/console"},
        {"/a/b/c/../../d", "/a/d"},
        {"/a/..", "/"},
        {"/..", "/"},
        {"/../..", "/"},
        {"/../bin", "/bin"},
        {"/...", "/..."}, /* three dots is a real name */
        {"/..a/b", "/..a/b"},
        /* Relative input: the working directory is fixed at "/". */
        {"bin/init", "/bin/init"},
        {".", "/"},
        {"..", "/"},
        {"dev/../bin", "/bin"},
    };
    char out[VFS_PATH_MAX];
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        long rc = vfs_path_norm(cases[i].in, out, sizeof(out));
        ASSERT_EQ_INT(0, rc);
        ASSERT_EQ_STR(cases[i].want, out);
    }
}

TEST(vfs_path_rejections) {
    char out[VFS_PATH_MAX];
    ASSERT_EQ_INT(-EINVAL, vfs_path_norm("", out, sizeof(out)));
    ASSERT_EQ_INT(-EINVAL, vfs_path_norm(NULL, out, sizeof(out)));

    /* A component longer than VFS_NAME_MAX. */
    char big[VFS_NAME_MAX + 3];
    big[0] = '/';
    for (unsigned i = 1; i <= VFS_NAME_MAX + 1; i++) {
        big[i] = 'x';
    }
    big[VFS_NAME_MAX + 2] = '\0';
    ASSERT_EQ_INT(-ENAMETOOLONG, vfs_path_norm(big, out, sizeof(out)));

    /* A result that does not fit the caller's buffer. */
    char tiny[8];
    ASSERT_EQ_INT(-ENAMETOOLONG, vfs_path_norm("/12345678", tiny, sizeof(tiny)));
    ASSERT_EQ_INT(0, vfs_path_norm("/123456", tiny, sizeof(tiny)));
    ASSERT_EQ_STR("/123456", tiny);

    /* cap < 2 cannot even hold "/". */
    ASSERT_EQ_INT(-ENAMETOOLONG, vfs_path_norm("/", tiny, 1));
}

TEST(vfs_path_dotdot_stays_inside_buffer) {
    /* ".." popping must never underrun: a storm of them ends at "/". */
    char out[VFS_PATH_MAX];
    ASSERT_EQ_INT(0, vfs_path_norm("/a/../../../../b", out, sizeof(out)));
    ASSERT_EQ_STR("/b", out);
}

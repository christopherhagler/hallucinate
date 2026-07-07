/*
 * hello.c - the execve target: proves a second program can replace a
 * forked child's image, and that the SysV argv contract holds.
 *
 * Exit status is the report (checked by init's wait4): 42 means
 * every check passed, other values name the first failed check.
 */
#include "syscall.h"
#include "ulib.h"

static const char banner[] = "hello from execve\n";

int main(int argc, char **argv) { /* NOLINT(misc-use-internal-linkage) */
    if (sys_write(1, banner, str_len(banner)) != (long)str_len(banner)) {
        return 1;
    }
    if (argc != 2) {
        return 2;
    }
    if (!str_eq(argv[0], "/bin/hello")) {
        return 3;
    }
    if (!str_eq(argv[1], "via-fork-exec")) {
        return 4;
    }
    if (argv[2] != 0) {
        return 5;
    }
    return 42;
}

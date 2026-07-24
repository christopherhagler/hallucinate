/*
 * stat.h - file metadata ABI: struct stat, dirent64, open flags.
 *
 * Layouts and values match the Linux x86_64 ABI exactly (like the
 * syscall numbers and errno values) so the Phase 7 personality layer
 * inherits them unchanged. Only what the kernel implements is
 * defined; grow as syscalls grow.
 */
#pragma once

#include <stdint.h>

/* st_mode file-type field. */
#define S_IFMT   0170000u
#define S_IFSOCK 0140000u
#define S_IFREG  0100000u
#define S_IFDIR  0040000u
#define S_IFCHR  0020000u
#define S_IFIFO  0010000u

/* open(2) flags. Everything the kernel implements; other bits are
 * rejected with -EINVAL rather than ignored. */
#define O_RDONLY    00000000
#define O_WRONLY    00000001
#define O_RDWR      00000002
#define O_ACCMODE   00000003
#define O_CREAT     00000100 /* create a missing regular file */
#define O_EXCL      00000200 /* with O_CREAT: fail if the path exists */
#define O_TRUNC     00001000 /* truncate an existing regular file to 0 */
#define O_APPEND    00002000 /* every write lands at EOF */
#define O_DIRECTORY 00200000 /* fail unless the path is a directory */

/* lseek(2) whence. */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* getdents64 d_type values. */
#define DT_UNKNOWN 0u
#define DT_CHR     2u
#define DT_DIR     4u
#define DT_REG     8u

/* Linux x86_64 struct stat: 144 bytes, asserted in vfs.c. Times are
 * zero until the kernel keeps a clock worth reporting (no RTC yet). */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t pad0_;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks; /* 512-byte units, per the ABI */
    int64_t st_atime_sec, st_atime_nsec;
    int64_t st_mtime_sec, st_mtime_nsec;
    int64_t st_ctime_sec, st_ctime_nsec;
    int64_t unused_[3];
};

/* getdents64 record: header + NUL-terminated name, d_reclen bytes
 * total (8-aligned). d_off is the offset of the *next* record. */
struct dirent64 {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[]; /* flexible: name follows the header */
};

#define DIRENT64_HDR 19u /* offsetof(struct dirent64, d_name) */

/*
 * errno.h - kernel error numbers.
 *
 * Values match the Linux x86_64 ABI on purpose: the native syscall
 * interface uses Linux numbering from day one so the Phase 7 Linux
 * personality layer shares one error vocabulary. Only errors the
 * kernel actually returns are defined; grow as syscalls grow.
 */
#pragma once

#define EPERM        1  /* operation not permitted */
#define ENOENT       2  /* no such file or directory */
#define EIO          5  /* I/O error */
#define E2BIG        7  /* argument list too long */
#define ENOEXEC      8  /* exec format error */
#define EBADF        9  /* bad file descriptor */
#define ECHILD       10 /* no child processes */
#define EAGAIN       11 /* resource temporarily unavailable */
#define ENOMEM       12 /* out of memory */
#define EFAULT       14 /* bad address */
#define ENODEV       19 /* no such device */
#define ENOTDIR      20 /* not a directory */
#define EISDIR       21 /* is a directory */
#define EINVAL       22 /* invalid argument */
#define EMFILE       24 /* per-process fd table full */
#define ESPIPE       29 /* illegal seek (not a seekable file) */
#define EROFS        30 /* read-only filesystem */
#define ENAMETOOLONG 36 /* file name too long */
#define ENOSYS       38 /* syscall not implemented */

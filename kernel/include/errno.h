/*
 * errno.h - kernel error numbers.
 *
 * Values match the Linux x86_64 ABI on purpose: the native syscall
 * interface uses Linux numbering from day one so the Phase 7 Linux
 * personality layer shares one error vocabulary. Only errors the
 * kernel actually returns are defined; grow as syscalls grow.
 */
#pragma once

#define EPERM  1  /* operation not permitted */
#define ENOENT 2  /* no such file or directory */
#define EBADF  9  /* bad file descriptor */
#define ENOMEM 12 /* out of memory */
#define EFAULT 14 /* bad address */
#define EINVAL 22 /* invalid argument */
#define ENOSYS 38 /* syscall not implemented */

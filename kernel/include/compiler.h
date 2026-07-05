/*
 * compiler.h - compiler attribute helpers.
 *
 * Every attribute the kernel relies on is wrapped here so that a future
 * compiler change touches exactly one file.
 */
#ifndef HL_COMPILER_H
#define HL_COMPILER_H

#define NORETURN      __attribute__((noreturn))
#define PACKED        __attribute__((packed))
#define ALIGNED(n)    __attribute__((aligned(n)))
#define UNUSED        __attribute__((unused))
#define PRINTF(f, a)  __attribute__((format(printf, f, a)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#endif /* HL_COMPILER_H */

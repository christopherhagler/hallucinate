/*
 * string.h - freestanding memory and string primitives.
 *
 * The kernel is built with -ffreestanding; the compiler is still entitled to
 * emit calls to memcpy/memmove/memset/memcmp, so those four must always be
 * present and correct.
 */
#ifndef HL_STRING_H
#define HL_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);

#endif /* HL_STRING_H */

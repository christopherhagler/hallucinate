/*
 * kmalloc.h - kernel heap.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Initialize the global kernel heap. Requires pmm_init() and
 * vmm_init() (allocations are handed out through the HHDM). */
void kmalloc_init(void);

/* Allocate `size` bytes, 16-byte aligned; NULL when out of memory or
 * for size 0. */
void *kmalloc(size_t size);

/* kmalloc + zero fill. */
void *kzalloc(size_t size);

/* Free a kmalloc/kzalloc pointer. NULL is a no-op; any other invalid
 * pointer (double free, foreign address) panics. */
void kfree(void *ptr);

/* Live statistics for tests and diagnostics. */
uint64_t kmalloc_live_objects(void);
uint64_t kmalloc_live_pages(void);

/*
 * crc32c.c - CRC-32C (Castagnoli), reflected bitwise form.
 *
 * Stateless and table-free on purpose: the module stays pure (no mutable
 * globals, no lazy-init race) so it is identical for the kernel, the host
 * fsck/mkfs tools, and the sanitizer test build. graphfs metadata blocks
 * are 4 KiB and checked at I/O time, where the bitwise loop is not the
 * bottleneck; a slice-by-N table is a later optimization if profiling ever
 * asks for one.
 */
#include <crc32c.h>

/* 0x82F63B78 is 0x1EDC6F41 bit-reversed (reflected algorithm). */
#define CRC32C_POLY_REFLECTED 0x82F63B78u

uint32_t crc32c_update(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            /* mask = 0xFFFFFFFF when the low bit is set, else 0. */
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (CRC32C_POLY_REFLECTED & mask);
        }
    }
    return crc;
}

/*
 * crc32c.h - CRC-32C (Castagnoli) checksum.
 *
 * Polynomial 0x1EDC6F41, reflected input/output, init/xorout 0xFFFFFFFF
 * (the iSCSI/SCTP/ext4/btrfs parameters). This is the integrity primitive
 * for graphfs metadata: every on-disk metadata block is covered by a
 * crc32c stored in the structure that points at it.
 *
 * Pure and freestanding (table-driven, no kernel dependencies) so it is
 * shared verbatim by the kernel, the host filesystem tools, and the host
 * test suite. Known-answer: crc32c("123456789") == 0xE3069283.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Rolling form: fold `len` bytes into a running crc. Seed a fresh
 * checksum with GFS/CRC32C_INIT and finalize with crc32c_final(). */
#define CRC32C_INIT 0xFFFFFFFFu

uint32_t crc32c_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t crc32c_final(uint32_t crc) {
    return crc ^ 0xFFFFFFFFu;
}

/* One-shot checksum of a buffer. */
static inline uint32_t crc32c(const void *data, size_t len) {
    return crc32c_final(crc32c_update(CRC32C_INIT, data, len));
}

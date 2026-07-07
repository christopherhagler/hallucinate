/*
 * elf64.h - ELF64 executable images: validation and loading.
 *
 * Split like the other core/kernel pairs:
 *
 *   elf64_validate() / elf64_phdr_get() / elf64_strerror()
 *       pure functions over a byte buffer (kernel/lib/elf64.c),
 *       compiled for the host and tested under ASan/UBSan.
 *
 *   elf64_load()
 *       the kernel side (kernel/proc/elf_load.c): materializes a
 *       validated image into a user address space with per-segment
 *       W^X page permissions.
 *
 * Scope: statically linked ET_EXEC for EM_X86_64, little-endian.
 * Anything else is rejected with a specific error code.
 */
#pragma once

#include <stdint.h>

/* e_ident layout (ELF-64 object file format). */
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6
#define EI_NIDENT  16

#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EV_CURRENT  1

#define ET_EXEC   2
#define EM_X86_64 62

#define PT_LOAD 1

#define PF_X 0x1u
#define PF_W 0x2u
#define PF_R 0x4u

struct elf64_ehdr {
    uint8_t e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

/* More PT_LOAD segments than any sane static executable carries. */
#define ELF64_PHNUM_MAX 16

enum {
    ELF64_OK = 0,
    ELF64_ETRUNC = -1,     /* buffer smaller than the ELF header */
    ELF64_EMAGIC = -2,     /* \x7fELF missing */
    ELF64_ECLASS = -3,     /* not ELFCLASS64 */
    ELF64_EENDIAN = -4,    /* not little-endian */
    ELF64_EVERSION = -5,   /* ident or file version not EV_CURRENT */
    ELF64_ETYPE = -6,      /* not ET_EXEC (static executables only) */
    ELF64_EMACHINE = -7,   /* not EM_X86_64 */
    ELF64_EPHENTSIZE = -8, /* e_phentsize != sizeof(struct elf64_phdr) */
    ELF64_EPHNUM = -9,     /* zero or more than ELF64_PHNUM_MAX phdrs */
    ELF64_EPHOFF = -10,    /* phdr table escapes the buffer */
    ELF64_ESEGFILE = -11,  /* segment file range escapes the buffer */
    ELF64_ESEGSIZE = -12,  /* p_filesz > p_memsz */
    ELF64_ESEGVA = -13,    /* vaddr range outside [va_min, va_limit) */
    ELF64_ESEGALIGN = -14, /* p_vaddr and p_offset disagree mod page size */
    ELF64_EOVERLAP = -15,  /* two PT_LOAD segments share a page */
    ELF64_EWX = -16,       /* segment both writable and executable */
    ELF64_ENOLOAD = -17,   /* no loadable segment */
    ELF64_EENTRY = -18,    /* entry outside every executable segment's bytes */
    ELF64_ENOMEM = -19,    /* loader only: out of physical frames */
};

/* Facts the loader needs, extracted during validation. */
struct elf64_info {
    uint64_t entry;
    uint16_t phnum;
};

/*
 * Validate `image` (`size` bytes) as a loadable executable whose
 * PT_LOAD segments all lie within [va_min, va_limit). Both bounds
 * must be page-aligned. Every field the loader will consume is
 * checked here, overflow-safely, so the loader can KASSERT instead
 * of re-checking. Returns ELF64_OK and fills *info (when non-NULL),
 * or the specific ELF64_E* code for the first defect found.
 */
int elf64_validate(const uint8_t *image, uint64_t size, uint64_t va_min, uint64_t va_limit,
                   struct elf64_info *info);

/*
 * Copy out program header `index`. Only valid on an image that
 * elf64_validate() accepted, with index < info.phnum.
 */
void elf64_phdr_get(const uint8_t *image, uint16_t index, struct elf64_phdr *out);

/* Human-readable name for an ELF64_E* code. */
const char *elf64_strerror(int err);

/*
 * Kernel loader: validate `image` against the user address bounds
 * and map every PT_LOAD segment into `as` — fresh zeroed frames,
 * file bytes copied in, page permissions from the segment flags
 * (PF_X => executable, PF_W => writable, never both; everything
 * else NX). Stores the entry point in *entry_out.
 *
 * Returns ELF64_OK, a validation error (nothing mapped), or
 * ELF64_ENOMEM (address space partially populated — tear it down
 * with paging_user_destroy()).
 */
struct addrspace;
int elf64_load(struct addrspace *as, const uint8_t *image, uint64_t size, uint64_t *entry_out);

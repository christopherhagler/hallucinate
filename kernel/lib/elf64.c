/*
 * elf64.c - pure ELF64 executable validation.
 *
 * Everything here operates on a byte buffer through memcpy (the
 * buffer carries no alignment guarantee) and touches no kernel
 * state, so the identical source runs in the host test suite under
 * ASan/UBSan (tests/host/test_elf64.c).
 *
 * The validation contract: after elf64_validate() returns ELF64_OK,
 * every arithmetic step the loader performs on the accepted fields
 * is overflow-free and in-bounds. The checks below are ordered so
 * each one may rely on the ones before it.
 */
#include <elf64.h>

#include <string.h>

#define ELF64_PAGE 4096ull

_Static_assert(sizeof(struct elf64_ehdr) == 64, "ELF64 header is 64 bytes");
_Static_assert(sizeof(struct elf64_phdr) == 56, "ELF64 program header is 56 bytes");

static void ehdr_get(const uint8_t *image, struct elf64_ehdr *out) {
    memcpy(out, image, sizeof(*out));
}

void elf64_phdr_get(const uint8_t *image, uint16_t index, struct elf64_phdr *out) {
    struct elf64_ehdr eh;
    ehdr_get(image, &eh);
    memcpy(out, image + eh.e_phoff + ((uint64_t)index * sizeof(*out)), sizeof(*out));
}

static uint64_t page_down(uint64_t addr) {
    return addr & ~(ELF64_PAGE - 1);
}

static uint64_t page_up(uint64_t addr) {
    return (addr + ELF64_PAGE - 1) & ~(ELF64_PAGE - 1);
}

int elf64_validate(const uint8_t *image, uint64_t size, uint64_t va_min, uint64_t va_limit,
                   struct elf64_info *info) {
    if (size < sizeof(struct elf64_ehdr)) {
        return ELF64_ETRUNC;
    }
    struct elf64_ehdr eh;
    ehdr_get(image, &eh);

    static const uint8_t magic[4] = {0x7F, 'E', 'L', 'F'};
    if (memcmp(eh.e_ident, magic, sizeof(magic)) != 0) {
        return ELF64_EMAGIC;
    }
    if (eh.e_ident[EI_CLASS] != ELFCLASS64) {
        return ELF64_ECLASS;
    }
    if (eh.e_ident[EI_DATA] != ELFDATA2LSB) {
        return ELF64_EENDIAN;
    }
    if (eh.e_ident[EI_VERSION] != EV_CURRENT || eh.e_version != EV_CURRENT) {
        return ELF64_EVERSION;
    }
    if (eh.e_type != ET_EXEC) {
        return ELF64_ETYPE;
    }
    if (eh.e_machine != EM_X86_64) {
        return ELF64_EMACHINE;
    }
    if (eh.e_phentsize != sizeof(struct elf64_phdr)) {
        return ELF64_EPHENTSIZE;
    }
    if (eh.e_phnum == 0 || eh.e_phnum > ELF64_PHNUM_MAX) {
        return ELF64_EPHNUM;
    }
    /* phnum <= ELF64_PHNUM_MAX, so the table size cannot overflow. */
    uint64_t table_bytes = (uint64_t)eh.e_phnum * sizeof(struct elf64_phdr);
    if (eh.e_phoff > size || table_bytes > size - eh.e_phoff) {
        return ELF64_EPHOFF;
    }

    /* Page-rounded spans of the PT_LOADs seen so far, for the
     * pairwise overlap check (n is tiny; O(n^2) is fine). */
    uint64_t load_start[ELF64_PHNUM_MAX];
    uint64_t load_end[ELF64_PHNUM_MAX];
    int nloads = 0;
    int entry_ok = 0;

    for (uint16_t i = 0; i < eh.e_phnum; i++) {
        struct elf64_phdr ph;
        elf64_phdr_get(image, i, &ph);
        if (ph.p_type != PT_LOAD) {
            continue; /* PT_NOTE, PT_GNU_STACK, ... carry no bytes to map */
        }
        if ((ph.p_flags & PF_W) && (ph.p_flags & PF_X)) {
            return ELF64_EWX; /* W^X is policy for user segments too */
        }
        if (ph.p_filesz > ph.p_memsz) {
            return ELF64_ESEGSIZE;
        }
        if (ph.p_memsz == 0) {
            continue; /* nothing to map */
        }
        if (ph.p_offset > size || ph.p_filesz > size - ph.p_offset) {
            return ELF64_ESEGFILE;
        }
        /* vaddr in [va_min, va_limit) and memsz fits below va_limit;
         * checked in this order so nothing can overflow. */
        if (ph.p_vaddr < va_min || ph.p_vaddr >= va_limit) {
            return ELF64_ESEGVA;
        }
        if (ph.p_memsz > va_limit - ph.p_vaddr) {
            return ELF64_ESEGVA;
        }
        /* The loader copies page by page; it needs file offset and
         * vaddr congruent modulo the page size (the standard ELF
         * loadable-segment constraint). */
        if (((ph.p_vaddr ^ ph.p_offset) & (ELF64_PAGE - 1)) != 0) {
            return ELF64_ESEGALIGN;
        }

        /* va_limit is page-aligned and vaddr+memsz <= va_limit, so
         * page_up() cannot overflow here. */
        uint64_t start = page_down(ph.p_vaddr);
        uint64_t end = page_up(ph.p_vaddr + ph.p_memsz);
        for (int j = 0; j < nloads; j++) {
            if (start < load_end[j] && load_start[j] < end) {
                return ELF64_EOVERLAP;
            }
        }
        load_start[nloads] = start;
        load_end[nloads] = end;
        nloads++;

        if ((ph.p_flags & PF_X) && eh.e_entry >= ph.p_vaddr &&
            eh.e_entry < ph.p_vaddr + ph.p_filesz) {
            entry_ok = 1;
        }
    }

    if (nloads == 0) {
        return ELF64_ENOLOAD;
    }
    if (!entry_ok) {
        return ELF64_EENTRY;
    }

    if (info != NULL) {
        info->entry = eh.e_entry;
        info->phnum = eh.e_phnum;
    }
    return ELF64_OK;
}

const char *elf64_strerror(int err) {
    switch (err) {
    case ELF64_OK:
        return "ok";
    case ELF64_ETRUNC:
        return "truncated ELF header";
    case ELF64_EMAGIC:
        return "bad ELF magic";
    case ELF64_ECLASS:
        return "not a 64-bit ELF";
    case ELF64_EENDIAN:
        return "not little-endian";
    case ELF64_EVERSION:
        return "unsupported ELF version";
    case ELF64_ETYPE:
        return "not a static executable (ET_EXEC)";
    case ELF64_EMACHINE:
        return "not an x86_64 binary";
    case ELF64_EPHENTSIZE:
        return "bad program header entry size";
    case ELF64_EPHNUM:
        return "bad program header count";
    case ELF64_EPHOFF:
        return "program header table out of bounds";
    case ELF64_ESEGFILE:
        return "segment file range out of bounds";
    case ELF64_ESEGSIZE:
        return "segment file size exceeds memory size";
    case ELF64_ESEGVA:
        return "segment outside the user address range";
    case ELF64_ESEGALIGN:
        return "segment misaligned (vaddr vs offset)";
    case ELF64_EOVERLAP:
        return "loadable segments overlap";
    case ELF64_EWX:
        return "segment is both writable and executable";
    case ELF64_ENOLOAD:
        return "no loadable segment";
    case ELF64_EENTRY:
        return "entry point outside executable code";
    case ELF64_ENOMEM:
        return "out of memory";
    default:
        return "unknown ELF error";
    }
}

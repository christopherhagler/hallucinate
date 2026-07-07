/* test_elf64.c - ELF64 validator: a crafted well-formed executable,
 * then one targeted mutation per rejection path. */
#include <stdint.h>

#include <elf64.h>

#include "test.h"

/* Bounds passed to every validation in these tests. */
#define VA_MIN   0x1000ull
#define VA_LIMIT 0x0000800000000000ull

/* The crafted image: ehdr, three phdrs, then two pages of payload. */
#define IMG_CAP  0x3000u
#define IMG_SIZE 0x2080ull
#define PHOFF    64ull

#define TEXT_VADDR  0x400000ull
#define TEXT_OFFSET 0x1000ull
#define TEXT_FILESZ 0x100ull
#define DATA_VADDR  0x401000ull
#define DATA_OFFSET 0x2000ull
#define DATA_FILESZ 0x80ull
#define DATA_MEMSZ  0x200ull

#define PT_NOTE 4u

static void eh_get(const uint8_t *img, struct elf64_ehdr *eh) {
    memcpy(eh, img, sizeof(*eh));
}

static void eh_set(uint8_t *img, const struct elf64_ehdr *eh) {
    memcpy(img, eh, sizeof(*eh));
}

static void ph_get(const uint8_t *img, int index, struct elf64_phdr *ph) {
    memcpy(ph, img + PHOFF + ((uint64_t)index * sizeof(*ph)), sizeof(*ph));
}

static void ph_set(uint8_t *img, int index, const struct elf64_phdr *ph) {
    memcpy(img + PHOFF + ((uint64_t)index * sizeof(*ph)), ph, sizeof(*ph));
}

/* Build a valid static executable: RX text, RW data with bss tail,
 * and a PT_NOTE that must be ignored. Entry at the start of text. */
static void make_exec(uint8_t *img) {
    memset(img, 0, IMG_CAP);

    struct elf64_ehdr eh = {0};
    eh.e_ident[EI_MAG0] = 0x7F;
    eh.e_ident[EI_MAG1] = 'E';
    eh.e_ident[EI_MAG2] = 'L';
    eh.e_ident[EI_MAG3] = 'F';
    eh.e_ident[EI_CLASS] = ELFCLASS64;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_type = ET_EXEC;
    eh.e_machine = EM_X86_64;
    eh.e_version = EV_CURRENT;
    eh.e_entry = TEXT_VADDR;
    eh.e_phoff = PHOFF;
    eh.e_ehsize = sizeof(struct elf64_ehdr);
    eh.e_phentsize = sizeof(struct elf64_phdr);
    eh.e_phnum = 3;
    eh_set(img, &eh);

    struct elf64_phdr text = {0};
    text.p_type = PT_LOAD;
    text.p_flags = PF_R | PF_X;
    text.p_offset = TEXT_OFFSET;
    text.p_vaddr = TEXT_VADDR;
    text.p_filesz = TEXT_FILESZ;
    text.p_memsz = TEXT_FILESZ;
    text.p_align = 0x1000;
    ph_set(img, 0, &text);

    struct elf64_phdr data = {0};
    data.p_type = PT_LOAD;
    data.p_flags = PF_R | PF_W;
    data.p_offset = DATA_OFFSET;
    data.p_vaddr = DATA_VADDR;
    data.p_filesz = DATA_FILESZ;
    data.p_memsz = DATA_MEMSZ; /* memsz > filesz: zero-filled bss tail */
    data.p_align = 0x1000;
    ph_set(img, 1, &data);

    struct elf64_phdr note = {0};
    note.p_type = PT_NOTE;
    note.p_flags = PF_R;
    note.p_offset = 0x40;
    note.p_filesz = 0x10;
    note.p_memsz = 0x10;
    ph_set(img, 2, &note);
}

static int validate(const uint8_t *img, uint64_t size, struct elf64_info *info) {
    return elf64_validate(img, size, VA_MIN, VA_LIMIT, info);
}

TEST(elf64_accepts_wellformed) {
    uint8_t img[IMG_CAP];
    make_exec(img);

    struct elf64_info info = {0};
    ASSERT_EQ_INT(ELF64_OK, validate(img, IMG_SIZE, &info));
    ASSERT_EQ_INT(TEXT_VADDR, (long long)info.entry);
    ASSERT_EQ_INT(3, info.phnum);

    /* NULL info is allowed. */
    ASSERT_EQ_INT(ELF64_OK, validate(img, IMG_SIZE, NULL));

    /* Trailing garbage after the mapped payload is not an error. */
    ASSERT_EQ_INT(ELF64_OK, validate(img, IMG_CAP, &info));
}

TEST(elf64_rejects_bad_header) {
    uint8_t img[IMG_CAP];
    struct elf64_ehdr eh;

    make_exec(img);
    ASSERT_EQ_INT(ELF64_ETRUNC, validate(img, sizeof(struct elf64_ehdr) - 1, NULL));

    make_exec(img);
    img[EI_MAG1] = 'F';
    ASSERT_EQ_INT(ELF64_EMAGIC, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    img[EI_CLASS] = 1; /* ELFCLASS32 */
    ASSERT_EQ_INT(ELF64_ECLASS, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    img[EI_DATA] = 2; /* big-endian */
    ASSERT_EQ_INT(ELF64_EENDIAN, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    img[EI_VERSION] = 0;
    ASSERT_EQ_INT(ELF64_EVERSION, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_version = 2;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EVERSION, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_type = 3; /* ET_DYN: dynamic executables are a later slice */
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_ETYPE, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_machine = 0xB7; /* EM_AARCH64 */
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EMACHINE, validate(img, IMG_SIZE, NULL));
}

TEST(elf64_rejects_bad_phdr_table) {
    uint8_t img[IMG_CAP];
    struct elf64_ehdr eh;

    make_exec(img);
    eh_get(img, &eh);
    eh.e_phentsize = 32;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EPHENTSIZE, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_phnum = 0;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EPHNUM, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_phnum = ELF64_PHNUM_MAX + 1;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EPHNUM, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_phoff = IMG_SIZE - 1; /* table runs off the end */
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EPHOFF, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    eh_get(img, &eh);
    eh.e_phoff = UINT64_MAX - 8; /* offset arithmetic must not wrap */
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EPHOFF, validate(img, IMG_SIZE, NULL));
}

TEST(elf64_rejects_bad_segments) {
    uint8_t img[IMG_CAP];
    struct elf64_phdr ph;

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_offset = IMG_SIZE - 0x10;                   /* file bytes run off the end */
    ph.p_vaddr = TEXT_VADDR + (ph.p_offset & 0xFFF); /* keep congruence */
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGFILE, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_offset = UINT64_MAX - 0xFFF; /* offset+filesz must not wrap */
    ph.p_vaddr = TEXT_VADDR + (ph.p_offset & 0xFFF);
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGFILE, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_filesz = ph.p_memsz + 1;
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGSIZE, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_vaddr = 0; /* below va_min: would map the null page */
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGVA, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_vaddr = VA_LIMIT; /* at/above the user limit */
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGVA, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 1, &ph);
    ph.p_vaddr = VA_LIMIT - 0x1000; /* memsz crosses the limit */
    ph.p_memsz = 0x2000;
    ph_set(img, 1, &ph);
    ASSERT_EQ_INT(ELF64_ESEGVA, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_vaddr = TEXT_VADDR + 0x500; /* offset stays 0x1000-congruent */
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_ESEGALIGN, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 1, &ph);
    ph.p_vaddr = TEXT_VADDR + 0x800; /* shares text's page */
    ph.p_offset = 0x1800;            /* stays congruent and inside the file */
    ph_set(img, 1, &ph);
    ASSERT_EQ_INT(ELF64_EOVERLAP, validate(img, IMG_SIZE, NULL));

    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_flags = PF_R | PF_W | PF_X;
    ph_set(img, 0, &ph);
    ASSERT_EQ_INT(ELF64_EWX, validate(img, IMG_SIZE, NULL));
}

TEST(elf64_rejects_bad_entry) {
    uint8_t img[IMG_CAP];
    struct elf64_ehdr eh;
    struct elf64_phdr ph;

    /* No PT_LOAD at all. */
    make_exec(img);
    for (int i = 0; i < 2; i++) {
        ph_get(img, i, &ph);
        ph.p_type = PT_NOTE;
        ph_set(img, i, &ph);
    }
    ASSERT_EQ_INT(ELF64_ENOLOAD, validate(img, IMG_SIZE, NULL));

    /* A PT_LOAD with memsz 0 maps nothing and must not count. */
    make_exec(img);
    ph_get(img, 0, &ph);
    ph.p_filesz = 0;
    ph.p_memsz = 0;
    ph_set(img, 0, &ph);
    ph_get(img, 1, &ph);
    ph.p_type = PT_NOTE;
    ph_set(img, 1, &ph);
    ASSERT_EQ_INT(ELF64_ENOLOAD, validate(img, IMG_SIZE, NULL));

    /* Entry in the data segment (not PF_X). */
    make_exec(img);
    eh_get(img, &eh);
    eh.e_entry = DATA_VADDR;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EENTRY, validate(img, IMG_SIZE, NULL));

    /* Entry one past text's file-backed bytes. */
    make_exec(img);
    eh_get(img, &eh);
    eh.e_entry = TEXT_VADDR + TEXT_FILESZ;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EENTRY, validate(img, IMG_SIZE, NULL));

    /* Entry nowhere near a segment. */
    make_exec(img);
    eh_get(img, &eh);
    eh.e_entry = 0x700000;
    eh_set(img, &eh);
    ASSERT_EQ_INT(ELF64_EENTRY, validate(img, IMG_SIZE, NULL));
}

TEST(elf64_phdr_get_roundtrip) {
    uint8_t img[IMG_CAP];
    make_exec(img);

    struct elf64_info info = {0};
    ASSERT_EQ_INT(ELF64_OK, validate(img, IMG_SIZE, &info));

    struct elf64_phdr ph;
    elf64_phdr_get(img, 1, &ph);
    ASSERT_EQ_INT(PT_LOAD, ph.p_type);
    ASSERT_EQ_INT(PF_R | PF_W, ph.p_flags);
    ASSERT_EQ_INT(DATA_OFFSET, (long long)ph.p_offset);
    ASSERT_EQ_INT(DATA_VADDR, (long long)ph.p_vaddr);
    ASSERT_EQ_INT(DATA_FILESZ, (long long)ph.p_filesz);
    ASSERT_EQ_INT(DATA_MEMSZ, (long long)ph.p_memsz);
}

TEST(elf64_strerror_names_every_code) {
    const char *unknown = elf64_strerror(-1000);
    ASSERT_EQ_STR("unknown ELF error", unknown);
    ASSERT_EQ_STR("ok", elf64_strerror(ELF64_OK));
    for (int err = ELF64_ENOMEM; err < ELF64_OK; err++) {
        const char *msg = elf64_strerror(err);
        ASSERT_TRUE(msg != NULL);
        ASSERT_TRUE(strcmp(msg, unknown) != 0);
    }
}

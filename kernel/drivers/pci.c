/*
 * pci.c - PCI configuration mechanism #1 and boot-time bus scan.
 *
 * Single CPU and the table is written once during pci_init() (before
 * the scheduler can interleave drivers), so config-space access needs
 * no locking. The 0xCF8/0xCFC pair is x86-only by nature; a future
 * port replaces this file with an ECAM implementation behind the
 * same header.
 */
#include <pci.h>

#include <stddef.h>

#include <arch/x86_64/io.h>
#include <kprintf.h>
#include <panic.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define PCI_ENABLE_BIT 0x80000000u

static struct pci_dev devs[PCI_MAX_DEVS];
static int ndevs;

static uint32_t cfg_read32_raw(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

static void cfg_write32_raw(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFCu);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, val);
}

uint32_t pci_cfg_read32(const struct pci_dev *d, uint8_t off) {
    return cfg_read32_raw(d->bus, d->dev, d->func, off);
}

uint16_t pci_cfg_read16(const struct pci_dev *d, uint8_t off) {
    uint32_t v = pci_cfg_read32(d, off);
    return (uint16_t)(v >> ((off & 2u) * 8));
}

uint8_t pci_cfg_read8(const struct pci_dev *d, uint8_t off) {
    uint32_t v = pci_cfg_read32(d, off);
    return (uint8_t)(v >> ((off & 3u) * 8));
}

void pci_cfg_write16(const struct pci_dev *d, uint8_t off, uint16_t val) {
    uint32_t v = pci_cfg_read32(d, off);
    unsigned shift = (off & 2u) * 8;
    v = (v & ~(0xFFFFu << shift)) | ((uint32_t)val << shift);
    cfg_write32_raw(d->bus, d->dev, d->func, off, v);
}

uint8_t pci_cap_find(const struct pci_dev *d, uint8_t cap_id, uint8_t prev) {
    if ((pci_cfg_read16(d, PCI_CFG_STATUS) & PCI_STATUS_CAP_LIST) == 0) {
        return 0;
    }
    uint8_t off =
        (prev == 0) ? pci_cfg_read8(d, PCI_CFG_CAP_PTR) : pci_cfg_read8(d, (uint8_t)(prev + 1));
    /* Bound the walk: a malformed list must not loop forever. */
    for (int guard = 0; off >= 0x40 && guard < 48; guard++) {
        off &= 0xFCu; /* capability structures are dword-aligned */
        if (pci_cfg_read8(d, off) == cap_id) {
            return off;
        }
        off = pci_cfg_read8(d, (uint8_t)(off + 1));
    }
    return 0;
}

uint64_t pci_bar_phys(const struct pci_dev *d, uint8_t bar) {
    uint8_t off = (uint8_t)(PCI_CFG_BAR0 + (4u * bar));
    uint32_t lo = pci_cfg_read32(d, off);
    if (lo & 1u) {
        return 0; /* I/O space BAR */
    }
    uint64_t addr = lo & ~0xFull;
    if (((lo >> 1) & 3u) == 2u) { /* 64-bit memory BAR */
        addr |= (uint64_t)pci_cfg_read32(d, (uint8_t)(off + 4)) << 32;
    }
    return addr;
}

void pci_enable_device(const struct pci_dev *d) {
    uint16_t cmd = pci_cfg_read16(d, PCI_CFG_COMMAND);
    pci_cfg_write16(d, PCI_CFG_COMMAND, cmd | PCI_COMMAND_MEM | PCI_COMMAND_MASTER);
}

int pci_dev_count(void) {
    return ndevs;
}

const struct pci_dev *pci_dev_get(int index) {
    KASSERT(index >= 0 && index < ndevs);
    return &devs[index];
}

const struct pci_dev *pci_find(uint16_t vendor, uint16_t device) {
    for (int i = 0; i < ndevs; i++) {
        if (devs[i].vendor == vendor && devs[i].device == device) {
            return &devs[i];
        }
    }
    return NULL;
}

static void scan_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = cfg_read32_raw(bus, dev, func, PCI_CFG_VENDOR);
    uint16_t vendor = (uint16_t)id;
    if (vendor == 0xFFFF) {
        return; /* no device */
    }
    if (ndevs == PCI_MAX_DEVS) {
        /* A machine with more devices than the table is a config we
         * have never tested; refuse loudly rather than drop devices. */
        panic("pci: more than %d functions present", PCI_MAX_DEVS);
    }
    uint32_t class_reg = cfg_read32_raw(bus, dev, func, PCI_CFG_REVISION);
    struct pci_dev *d = &devs[ndevs++];
    d->bus = bus;
    d->dev = dev;
    d->func = func;
    d->vendor = vendor;
    d->device = (uint16_t)(id >> 16);
    d->prog_if = (uint8_t)(class_reg >> 8);
    d->subclass = (uint8_t)(class_reg >> 16);
    d->class_code = (uint8_t)(class_reg >> 24);
    d->header_type = (uint8_t)(cfg_read32_raw(bus, dev, func, 0x0C) >> 16);
    kprintf("pci: %02x:%02x.%x %04x:%04x class %02x.%02x\n", bus, dev, func, d->vendor, d->device,
            d->class_code, d->subclass);
}

void pci_init(void) {
    /* Flat scan: the BIOS has programmed bridge bus numbers, so every
     * function answers on its final bus; no recursion required. */
    for (unsigned bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t id = cfg_read32_raw((uint8_t)bus, dev, 0, PCI_CFG_VENDOR);
            if ((uint16_t)id == 0xFFFF) {
                continue;
            }
            scan_function((uint8_t)bus, dev, 0);
            uint8_t htype = (uint8_t)(cfg_read32_raw((uint8_t)bus, dev, 0, 0x0C) >> 16);
            if (htype & 0x80u) { /* multi-function device */
                for (uint8_t func = 1; func < 8; func++) {
                    scan_function((uint8_t)bus, dev, func);
                }
            }
        }
    }
    kprintf("pci: %d functions\n", ndevs);
}

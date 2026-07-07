/*
 * pci.h - PCI bus enumeration and configuration space access.
 *
 * Configuration mechanism #1 (ports 0xCF8/0xCFC). pci_init() scans
 * every bus/device/function once at boot — the BIOS POST has already
 * assigned bus numbers and BARs, so a flat scan finds everything —
 * and stores the result in a fixed table that is read-only
 * afterwards. Drivers locate their device with pci_find() and talk
 * to it through the config accessors and BAR helpers.
 */
#pragma once

#include <stdint.h>

#define PCI_MAX_DEVS 32

/* Config-space register offsets (type 0 header). */
#define PCI_CFG_VENDOR      0x00
#define PCI_CFG_DEVICE      0x02
#define PCI_CFG_COMMAND     0x04
#define PCI_CFG_STATUS      0x06
#define PCI_CFG_REVISION    0x08
#define PCI_CFG_PROG_IF     0x09
#define PCI_CFG_SUBCLASS    0x0A
#define PCI_CFG_CLASS       0x0B
#define PCI_CFG_HEADER_TYPE 0x0E
#define PCI_CFG_BAR0        0x10
#define PCI_CFG_CAP_PTR     0x34

#define PCI_COMMAND_MEM    0x0002 /* memory space decoding */
#define PCI_COMMAND_MASTER 0x0004 /* bus mastering (DMA) */

#define PCI_STATUS_CAP_LIST 0x0010

#define PCI_CAP_ID_VENDOR 0x09

struct pci_dev {
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t header_type;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
};

/* Scan the bus and log every function found. */
void pci_init(void);

int pci_dev_count(void);
const struct pci_dev *pci_dev_get(int index);

/* First device matching vendor:device, or NULL. */
const struct pci_dev *pci_find(uint16_t vendor, uint16_t device);

uint8_t pci_cfg_read8(const struct pci_dev *d, uint8_t off);
uint16_t pci_cfg_read16(const struct pci_dev *d, uint8_t off);
uint32_t pci_cfg_read32(const struct pci_dev *d, uint8_t off);
void pci_cfg_write16(const struct pci_dev *d, uint8_t off, uint16_t val);

/*
 * Walk the capability list: returns the offset of the first
 * capability with `cap_id` strictly after `prev` (pass 0 to start),
 * or 0 when there are no more.
 */
uint8_t pci_cap_find(const struct pci_dev *d, uint8_t cap_id, uint8_t prev);

/*
 * Physical address programmed into a memory BAR (32- or 64-bit).
 * Returns 0 for an I/O BAR or an unimplemented one.
 */
uint64_t pci_bar_phys(const struct pci_dev *d, uint8_t bar);

/* Enable memory decoding and bus mastering for the device. */
void pci_enable_device(const struct pci_dev *d);

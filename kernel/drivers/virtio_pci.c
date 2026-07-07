/*
 * virtio_pci.c - VIRTIO 1.2 modern PCI transport (§4.1).
 *
 * MMIO regions are reached through the direct map (BARs sit below
 * MMIO_LIMIT, mapped uncached by vmm_init). All virtio registers are
 * little-endian, which is native here; a future big-endian port adds
 * conversion in the accessors.
 */
#include <drivers/virtio_pci.h>

#include <stddef.h>

#include <errno.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pci.h>
#include <pmm.h>
#include <string.h>

/* virtio_pci_cap.cfg_type values (§4.1.4). */
#define VIRTIO_PCI_CAP_COMMON 1u
#define VIRTIO_PCI_CAP_NOTIFY 2u
#define VIRTIO_PCI_CAP_DEVICE 4u

/* Common configuration register offsets (§4.1.4.3). */
#define COMMON_DFSELECT  0x00 /* u32 device_feature_select */
#define COMMON_DFEATURE  0x04 /* u32 device_feature (RO) */
#define COMMON_GFSELECT  0x08 /* u32 driver_feature_select */
#define COMMON_GFEATURE  0x0C /* u32 driver_feature */
#define COMMON_NUMQ      0x12 /* u16 num_queues (RO) */
#define COMMON_STATUS    0x14 /* u8  device_status */
#define COMMON_CFGGEN    0x15 /* u8  config_generation (RO) */
#define COMMON_QSELECT   0x16 /* u16 queue_select */
#define COMMON_QSIZE     0x18 /* u16 queue_size */
#define COMMON_QENABLE   0x1C /* u16 queue_enable */
#define COMMON_QNOTIFOFF 0x1E /* u16 queue_notify_off (RO) */
#define COMMON_QDESC     0x20 /* u64 queue_desc */
#define COMMON_QDRIVER   0x28 /* u64 queue_driver (avail ring) */
#define COMMON_QDEVICE   0x30 /* u64 queue_device (used ring) */

#define VIRTQ_SIZE_MAX 128u /* cap: two PMM frames of ring memory */

static uint8_t mmio_read8(const volatile uint8_t *base, uint32_t off) {
    return *(const volatile uint8_t *)(base + off);
}

static uint16_t mmio_read16(const volatile uint8_t *base, uint32_t off) {
    return *(const volatile uint16_t *)(base + off);
}

static uint32_t mmio_read32(const volatile uint8_t *base, uint32_t off) {
    return *(const volatile uint32_t *)(base + off);
}

static void mmio_write8(volatile uint8_t *base, uint32_t off, uint8_t v) {
    *(volatile uint8_t *)(base + off) = v;
}

static void mmio_write16(volatile uint8_t *base, uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(base + off) = v;
}

static void mmio_write32(volatile uint8_t *base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base + off) = v;
}

static void mmio_write64(volatile uint8_t *base, uint32_t off, uint64_t v) {
    /* Split per §4.1.3.1: 64-bit fields are written as two 32-bit
     * accesses, low dword first. */
    mmio_write32(base, off, (uint32_t)v);
    mmio_write32(base, off + 4, (uint32_t)(v >> 32));
}

/*
 * Resolve one vendor capability to a pointer inside the BAR it
 * names. Returns NULL if absent or if the region falls outside the
 * direct-mapped MMIO window.
 */
static volatile uint8_t *cap_region(const struct pci_dev *pdev, uint8_t cfg_type,
                                    uint32_t *len_out) {
    for (uint8_t cap = pci_cap_find(pdev, PCI_CAP_ID_VENDOR, 0); cap != 0;
         cap = pci_cap_find(pdev, PCI_CAP_ID_VENDOR, cap)) {
        if (pci_cfg_read8(pdev, (uint8_t)(cap + 3)) != cfg_type) {
            continue;
        }
        uint8_t bar = pci_cfg_read8(pdev, (uint8_t)(cap + 4));
        uint32_t offset = pci_cfg_read32(pdev, (uint8_t)(cap + 8));
        uint32_t length = pci_cfg_read32(pdev, (uint8_t)(cap + 12));
        uint64_t phys = pci_bar_phys(pdev, bar);
        if (phys == 0 || bar > 5) {
            return NULL;
        }
        if (phys + offset + length > MMIO_LIMIT) {
            kprintf("virtio: BAR%u region beyond the %llu MiB direct map\n", bar,
                    (unsigned long long)(MMIO_LIMIT >> 20));
            return NULL;
        }
        if (len_out != NULL) {
            *len_out = length;
        }
        return (volatile uint8_t *)phys_to_virt(phys + offset);
    }
    return NULL;
}

uint8_t virtio_pci_cfg_read8(const struct virtio_dev *vd, uint32_t off) {
    return mmio_read8(vd->device_cfg, off);
}

uint64_t virtio_pci_cfg_read64(const struct virtio_dev *vd, uint32_t off) {
    /* §2.5.3: re-read if the generation counter moved mid-read. */
    for (;;) {
        uint8_t gen = mmio_read8(vd->common, COMMON_CFGGEN);
        uint64_t lo = mmio_read32(vd->device_cfg, off);
        uint64_t hi = mmio_read32(vd->device_cfg, off + 4);
        if (mmio_read8(vd->common, COMMON_CFGGEN) == gen) {
            return (hi << 32) | lo;
        }
    }
}

void virtio_pci_notify_q0(struct virtio_dev *vd) {
    /* Compiler barrier: every ring store above must be issued before
     * the doorbell (x86 TSO keeps them ordered at the CPU level). */
    __asm__ volatile("" ::: "memory");
    volatile uint8_t *door = vd->notify + ((uint64_t)vd->q0_notify_off * vd->notify_mult);
    *(volatile uint16_t *)door = 0; /* queue index */
}

static int queue0_setup(struct virtio_dev *vd) {
    mmio_write16(vd->common, COMMON_QSELECT, 0);
    uint16_t qsize = mmio_read16(vd->common, COMMON_QSIZE);
    if (qsize == 0) {
        return -ENODEV;
    }
    if (qsize > VIRTQ_SIZE_MAX) {
        qsize = VIRTQ_SIZE_MAX; /* the driver may shrink the queue */
        mmio_write16(vd->common, COMMON_QSIZE, qsize);
    }

    /* Ring memory: descriptor table + avail ring share a frame, the
     * used ring gets its own (alignment 16/2/4 all hold). Sizes for
     * 128 entries: 2048 + 260 and 1028 bytes — one frame each. */
    KASSERT(virtq_desc_bytes(qsize) + virtq_avail_bytes(qsize) <= PAGE_SIZE);
    KASSERT(virtq_used_bytes(qsize) <= PAGE_SIZE);
    uint64_t da_phys = pmm_alloc_frame();
    uint64_t used_phys = pmm_alloc_frame();
    if (da_phys == 0 || used_phys == 0) {
        return -ENOMEM;
    }
    memset(phys_to_virt(da_phys), 0, PAGE_SIZE);
    memset(phys_to_virt(used_phys), 0, PAGE_SIZE);

    uint64_t avail_phys = da_phys + virtq_desc_bytes(qsize);
    virtq_init(&vd->vq, qsize, phys_to_virt(da_phys), phys_to_virt(avail_phys),
               phys_to_virt(used_phys));

    mmio_write64(vd->common, COMMON_QDESC, da_phys);
    mmio_write64(vd->common, COMMON_QDRIVER, avail_phys);
    mmio_write64(vd->common, COMMON_QDEVICE, used_phys);
    vd->q0_notify_off = mmio_read16(vd->common, COMMON_QNOTIFOFF);
    mmio_write16(vd->common, COMMON_QENABLE, 1);
    return 0;
}

int virtio_pci_setup(struct virtio_dev *vd, const struct pci_dev *pdev, uint64_t features) {
    vd->pdev = pdev;
    vd->common = cap_region(pdev, VIRTIO_PCI_CAP_COMMON, NULL);
    vd->device_cfg = cap_region(pdev, VIRTIO_PCI_CAP_DEVICE, NULL);

    /* The notify capability carries an extra field: the offset
     * multiplier lives right after the generic header (§4.1.4.4). */
    vd->notify = NULL;
    for (uint8_t cap = pci_cap_find(pdev, PCI_CAP_ID_VENDOR, 0); cap != 0;
         cap = pci_cap_find(pdev, PCI_CAP_ID_VENDOR, cap)) {
        if (pci_cfg_read8(pdev, (uint8_t)(cap + 3)) == VIRTIO_PCI_CAP_NOTIFY) {
            vd->notify = cap_region(pdev, VIRTIO_PCI_CAP_NOTIFY, NULL);
            vd->notify_mult = pci_cfg_read32(pdev, (uint8_t)(cap + 16));
            break;
        }
    }
    if (vd->common == NULL || vd->notify == NULL) {
        kprintf("virtio: %04x:%04x lacks modern capabilities\n", pdev->vendor, pdev->device);
        return -ENODEV;
    }

    pci_enable_device(pdev);

    /* Status handshake (§3.1.1). Reset completes when status reads
     * back 0; bound the wait so a wedged device cannot hang boot. */
    mmio_write8(vd->common, COMMON_STATUS, 0);
    int spins = 1000000;
    while (mmio_read8(vd->common, COMMON_STATUS) != 0) {
        if (--spins == 0) {
            kprintf("virtio: device stuck in reset\n");
            return -ENODEV;
        }
    }
    uint8_t status = VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_write8(vd->common, COMMON_STATUS, status);
    status |= VIRTIO_STATUS_DRIVER;
    mmio_write8(vd->common, COMMON_STATUS, status);

    mmio_write32(vd->common, COMMON_DFSELECT, 0);
    uint64_t dev_features = mmio_read32(vd->common, COMMON_DFEATURE);
    mmio_write32(vd->common, COMMON_DFSELECT, 1);
    dev_features |= (uint64_t)mmio_read32(vd->common, COMMON_DFEATURE) << 32;

    uint64_t want = features | VIRTIO_F_VERSION_1;
    if ((dev_features & want) != want) {
        kprintf("virtio: features %#llx unavailable (device offers %#llx)\n",
                (unsigned long long)want, (unsigned long long)dev_features);
        mmio_write8(vd->common, COMMON_STATUS, VIRTIO_STATUS_FAILED);
        return -ENODEV;
    }
    mmio_write32(vd->common, COMMON_GFSELECT, 0);
    mmio_write32(vd->common, COMMON_GFEATURE, (uint32_t)want);
    mmio_write32(vd->common, COMMON_GFSELECT, 1);
    mmio_write32(vd->common, COMMON_GFEATURE, (uint32_t)(want >> 32));

    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_write8(vd->common, COMMON_STATUS, status);
    if ((mmio_read8(vd->common, COMMON_STATUS) & VIRTIO_STATUS_FEATURES_OK) == 0) {
        kprintf("virtio: device rejected feature selection\n");
        mmio_write8(vd->common, COMMON_STATUS, VIRTIO_STATUS_FAILED);
        return -ENODEV;
    }

    int rc = queue0_setup(vd);
    if (rc != 0) {
        mmio_write8(vd->common, COMMON_STATUS, VIRTIO_STATUS_FAILED);
        return rc;
    }

    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_write8(vd->common, COMMON_STATUS, status);
    return 0;
}

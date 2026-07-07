/*
 * virtio_pci.h - VIRTIO 1.2 modern PCI transport.
 *
 * Locates the device's common/notify/device-config regions through
 * the vendor capabilities (VIRTIO 1.2 §4.1.4), performs the status
 * handshake and feature negotiation, and sets up queue 0. Drivers
 * (virtio_blk.c today, virtio-serial in Phase 6) build on this and
 * own their virtqueue usage. Legacy (pre-1.0) devices are not
 * supported: the device must expose the modern capability set.
 *
 * v1 scope: single queue (queue 0), no MSI-X — completions are
 * polled, which suits the synchronous single-CPU kernel. IRQ-driven
 * completion arrives when the block layer grows async I/O.
 */
#pragma once

#include <stdint.h>

#include <virtq_core.h>

struct pci_dev;

#define VIRTIO_PCI_VENDOR 0x1AF4u

/* Device status bits (VIRTIO 1.2 §2.1). */
#define VIRTIO_STATUS_ACKNOWLEDGE 1u
#define VIRTIO_STATUS_DRIVER      2u
#define VIRTIO_STATUS_DRIVER_OK   4u
#define VIRTIO_STATUS_FEATURES_OK 8u
#define VIRTIO_STATUS_FAILED      128u

/* Feature bits used today. */
#define VIRTIO_F_VERSION_1 (1ull << 32)

struct virtio_dev {
    const struct pci_dev *pdev;
    volatile uint8_t *common;     /* common config registers */
    volatile uint8_t *notify;     /* notify region base */
    volatile uint8_t *device_cfg; /* device-specific config, may be NULL */
    uint32_t notify_mult;         /* notify offset multiplier */
    uint16_t q0_notify_off;
    struct virtq vq; /* queue 0 */
};

/*
 * Bring the device from reset to DRIVER_OK: map the capability
 * regions, negotiate VIRTIO_F_VERSION_1 | `features`, and set up
 * queue 0 (ring memory from the PMM). Returns 0, or a negative errno
 * on any failure — the device is marked FAILED and left quiescent.
 */
int virtio_pci_setup(struct virtio_dev *vd, const struct pci_dev *pdev, uint64_t features);

/* Tell the device queue 0 has new available descriptors. */
void virtio_pci_notify_q0(struct virtio_dev *vd);

/* Device-specific config read (handles the generation counter). */
uint8_t virtio_pci_cfg_read8(const struct virtio_dev *vd, uint32_t off);
uint64_t virtio_pci_cfg_read64(const struct virtio_dev *vd, uint32_t off);

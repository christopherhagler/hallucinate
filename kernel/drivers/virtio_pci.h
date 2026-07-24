/*
 * virtio_pci.h - VIRTIO 1.2 modern PCI transport.
 *
 * Locates the device's common/notify/device-config regions through
 * the vendor capabilities (VIRTIO 1.2 §4.1.4) and performs the status
 * handshake and feature negotiation. Drivers (virtio_blk.c, and the
 * virtio-console in Phase 6) build on this: they set up whatever
 * virtqueues they need with virtio_pci_queue_setup — one for the
 * block device, several for a multi-queue console — and each owns its
 * own struct virtq. Legacy (pre-1.0) devices are not supported: the
 * device must expose the modern capability set.
 *
 * v1 scope: no MSI-X — completions are polled, which suits the
 * synchronous single-CPU kernel. IRQ-driven completion arrives when
 * the block layer grows async I/O.
 *
 * Bring-up sequence a driver follows (VIRTIO 1.2 §3.1.1):
 *   virtio_pci_setup()        reset → ACKNOWLEDGE → DRIVER → FEATURES_OK
 *   virtio_pci_queue_setup()  once per virtqueue the driver uses
 *   virtio_pci_driver_ok()    DRIVER_OK — the device is now live
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

/* Transport-only state. Virtqueues belong to the driver, not here:
 * a device may use one queue (block) or many (console), so each
 * driver declares its own struct virtq(s) and the notify offset each
 * one returned from virtio_pci_queue_setup. */
struct virtio_dev {
    const struct pci_dev *pdev;
    volatile uint8_t *common;     /* common config registers */
    volatile uint8_t *notify;     /* notify region base */
    volatile uint8_t *device_cfg; /* device-specific config, may be NULL */
    uint32_t notify_mult;         /* notify offset multiplier */
};

/*
 * Bring the device from reset through feature negotiation: map the
 * capability regions, negotiate VIRTIO_F_VERSION_1 | `features`, and
 * leave the device in FEATURES_OK (not yet DRIVER_OK — the driver
 * sets up its queues first). Returns 0, or a negative errno on any
 * failure — the device is marked FAILED and left quiescent.
 */
int virtio_pci_setup(struct virtio_dev *vd, const struct pci_dev *pdev, uint64_t features);

/*
 * Configure virtqueue `index`: allocate its ring memory from the PMM,
 * publish the descriptor/avail/used addresses, enable it, and bind
 * the pure bookkeeping handle `vq`. *notify_off_out (if non-NULL)
 * receives the queue_notify_off the driver passes back to
 * virtio_pci_notify. Returns 0, -ENODEV (queue absent), or -ENOMEM.
 * Call between virtio_pci_setup and virtio_pci_driver_ok.
 */
int virtio_pci_queue_setup(struct virtio_dev *vd, uint16_t index, struct virtq *vq,
                           uint16_t *notify_off_out);

/* Set DRIVER_OK: the device goes live. Call once, after every queue
 * the driver uses has been set up. */
void virtio_pci_driver_ok(struct virtio_dev *vd);

/* Ring the doorbell for queue `index` (whose queue_notify_off, from
 * virtio_pci_queue_setup, is `notify_off`): tell the device that
 * queue has new available descriptors. */
void virtio_pci_notify(struct virtio_dev *vd, uint16_t index, uint16_t notify_off);

/* Device-specific config read (handles the generation counter). */
uint8_t virtio_pci_cfg_read8(const struct virtio_dev *vd, uint32_t off);
uint64_t virtio_pci_cfg_read64(const struct virtio_dev *vd, uint32_t off);

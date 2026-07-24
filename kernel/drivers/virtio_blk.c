/*
 * virtio_blk.c - virtio block device driver (VIRTIO 1.2 §5.2).
 *
 * Synchronous: each request is a three-descriptor chain (header out,
 * one 4 KiB data buffer, status in) and the driver polls the used
 * ring with a timer-based timeout. Good enough for a single-CPU
 * kernel whose callers block anyway; IRQ completion comes with async
 * I/O later.
 *
 * The driver exposes 4 KiB blocks (BLOCK_SIZE) to the block layer
 * and translates to the device's 512-byte sectors.
 */
#include <stddef.h>

#include <block.h>
#include <drivers/virtio_pci.h>
#include <errno.h>
#include <kprintf.h>
#include <memlayout.h>
#include <panic.h>
#include <pci.h>
#include <pmm.h>
#include <timer.h>
#include <virtio_blk.h>
#include <virtq_core.h>

#define VIRTIO_BLK_DEV_MODERN       0x1042u
#define VIRTIO_BLK_DEV_TRANSITIONAL 0x1001u

#define VIRTIO_BLK_T_IN  0u /* read */
#define VIRTIO_BLK_T_OUT 1u /* write */

#define VIRTIO_BLK_S_OK 0u

#define SECTORS_PER_BLOCK (BLOCK_SIZE / 512u)

#define REQ_TIMEOUT_MS 2000u

struct vblk_hdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static struct virtio_dev vdev;
static struct virtq vblk_vq;     /* the single request queue (queue 0) */
static uint16_t vblk_notify_off; /* its doorbell offset */
static struct bdev vblk_bdev;
static uint64_t req_phys; /* frame holding header + status byte */

static int vblk_rw(struct bdev *bd, uint64_t lba, void *buf, const void *cbuf) {
    (void)bd;
    struct vblk_hdr *hdr = phys_to_virt(req_phys);
    volatile uint8_t *status = (volatile uint8_t *)phys_to_virt(req_phys + sizeof(*hdr));
    int is_write = (cbuf != NULL);

    hdr->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    hdr->reserved = 0;
    hdr->sector = lba * SECTORS_PER_BLOCK;
    *status = 0xFF;

    /* Data buffers handed to the device must be physically
     * contiguous; the block layer guarantees frame-backed buffers. */
    uint64_t data_phys = virt_to_phys(is_write ? cbuf : buf);
    struct virtq_sg sgs[3] = {
        {req_phys, sizeof(*hdr)},
        {data_phys, BLOCK_SIZE},
        {req_phys + sizeof(*hdr), 1},
    };
    int head = virtq_add(&vblk_vq, sgs, (uint16_t)(is_write ? 2 : 1), (uint16_t)(is_write ? 1 : 2));
    KASSERT(head >= 0); /* queue is empty between synchronous requests */
    virtio_pci_notify(&vdev, 0, vblk_notify_off);

    uint64_t deadline = timer_ticks() + ((uint64_t)REQ_TIMEOUT_MS * timer_hz() / 1000u) + 1;
    for (;;) {
        __asm__ volatile("" ::: "memory"); /* re-read the used ring */
        int id = virtq_take_used(&vblk_vq, NULL);
        if (id == head) {
            break;
        }
        if (id == -2) {
            panic("virtio-blk: device corrupted the used ring");
        }
        KASSERT(id == -1); /* only one request can be in flight */
        if (timer_ticks() > deadline) {
            kprintf("virtio-blk: request timeout (lba %llu)\n", (unsigned long long)lba);
            return -EIO;
        }
    }
    if (*status != VIRTIO_BLK_S_OK) {
        kprintf("virtio-blk: device error %u (lba %llu, %s)\n", *status, (unsigned long long)lba,
                is_write ? "write" : "read");
        return -EIO;
    }
    return 0;
}

static int vblk_read(struct bdev *bd, uint64_t lba, void *buf) {
    return vblk_rw(bd, lba, buf, NULL);
}

static int vblk_write(struct bdev *bd, uint64_t lba, const void *buf) {
    return vblk_rw(bd, lba, NULL, buf);
}

void virtio_blk_init(void) {
    const struct pci_dev *pdev = pci_find(VIRTIO_PCI_VENDOR, VIRTIO_BLK_DEV_MODERN);
    if (pdev == NULL) {
        pdev = pci_find(VIRTIO_PCI_VENDOR, VIRTIO_BLK_DEV_TRANSITIONAL);
    }
    if (pdev == NULL) {
        kprintf("virtio-blk: no device\n");
        return;
    }
    if (virtio_pci_setup(&vdev, pdev, 0) != 0) {
        kprintf("virtio-blk: transport setup failed\n");
        return;
    }
    if (virtio_pci_queue_setup(&vdev, 0, &vblk_vq, &vblk_notify_off) != 0) {
        kprintf("virtio-blk: request queue setup failed\n");
        return;
    }
    virtio_pci_driver_ok(&vdev);

    req_phys = pmm_alloc_frame();
    if (req_phys == 0) {
        panic("virtio-blk: out of frames");
    }

    /* Device config §5.2.4: capacity is in 512-byte sectors. */
    uint64_t sectors = virtio_pci_cfg_read64(&vdev, 0);
    kprintf("virtio-blk: %llu MiB (%llu sectors), queue size %u\n",
            (unsigned long long)(sectors * 512u >> 20), (unsigned long long)sectors, vblk_vq.size);

    vblk_bdev.name = "virtio-blk";
    vblk_bdev.nblocks = sectors / SECTORS_PER_BLOCK;
    vblk_bdev.read = vblk_read;
    vblk_bdev.write = vblk_write;
    vblk_bdev.priv = NULL;
    block_register(&vblk_bdev);
}

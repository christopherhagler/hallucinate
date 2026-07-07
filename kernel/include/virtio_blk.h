/*
 * virtio_blk.h - virtio block device driver.
 */
#pragma once

/*
 * Find the virtio-blk PCI function, bring it to DRIVER_OK, and
 * register it with the block layer. A machine without one boots
 * fine — storage-dependent features report the absence themselves.
 * Requires pci_init(), pmm/vmm, and the timer (I/O timeouts).
 */
void virtio_blk_init(void);

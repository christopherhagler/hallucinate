# Storage: PCI, virtio-blk, and the Block Layer

Phase 5 gives the kernel a disk. This document covers the PCI bus scan, the
VIRTIO 1.2 modern PCI transport, the virtio-blk driver, and the block layer
that filesystems build on. The native filesystem itself (graphfs) has its
own document: [graphfs.md](graphfs.md) (lands with slice 5b).

## Topology

The **boot disk** (kernel image) stays on the BIOS/INT13 path — the
bootloader owns it and the kernel never touches it again. The **filesystem
disk** (`build/fs.img`) is a second drive attached as a modern virtio-blk
PCI device:

```
qemu ... -drive file=fs.img,format=raw,if=none,id=fsdisk \
         -device virtio-blk-pci,drive=fsdisk,disable-legacy=on
```

`disable-legacy=on` makes QEMU expose the pure VIRTIO 1.x interface
(device id `1af4:1042`); the driver does not implement the pre-1.0 legacy
layout at all. virtio is not a shortcut: it is the industry-standard
paravirtual device interface, implemented here against the VIRTIO 1.2
specification. Bare-metal drivers (AHCI, NVMe) are additive later work
behind the same block API — a stated requirement, since the OS will
eventually be installed on real hardware.

## PCI (`kernel/drivers/pci.c`)

Configuration mechanism #1: the `0xCF8`/`0xCFC` port pair. `pci_init()`
performs a flat scan of every bus/device/function — the BIOS POST has
already assigned bus numbers and BARs, so everything answers at its final
address — logging each function and recording them in a fixed table:

```
pci: 00:01.1 8086:7010 class 01.01     ← PIIX3 IDE
pci: 00:04.0 1af4:1042 class 01.00     ← virtio-blk, modern
pci: 7 functions
```

The API (`pci.h`) offers config-space accessors, a bounded capability-list
walker, memory-BAR decoding (32- and 64-bit), and `pci_enable_device()`
(memory decoding + bus mastering). Port-based config access is inherently
x86; a future arch port swaps in ECAM behind the same header.

## virtio transport (`kernel/drivers/virtio_pci.c`)

The modern transport locates the device's register regions through
vendor-specific PCI capabilities (VIRTIO 1.2 §4.1.4): common config,
notify, and device config, each naming a BAR and offset. The regions are
reached through the direct map (BARs sit below 4 GiB, which `vmm_init()`
maps uncached); a BAR outside that window is rejected loudly.

Bring-up follows §3.1.1 exactly: reset (bounded wait), ACKNOWLEDGE, DRIVER,
feature negotiation — `VIRTIO_F_VERSION_1` is required, anything else is
per-driver — FEATURES_OK (verified by reading it back), queue setup,
DRIVER_OK. Any failure marks the device FAILED and leaves it quiescent.

**Virtqueue.** The split-ring bookkeeping — descriptor chains, free-list
recycling, available/used index math — is a pure module
(`kernel/drivers/virtq_core.c`) with no kernel dependencies, unit-tested on
the host under ASan/UBSan where the tests play the device's side of the
protocol (`tests/host/test_virtq.c`). The kernel transport supplies what a
pure module cannot: ring memory (two PMM frames per queue), physical
addresses, memory barriers, and the notify doorbell. The core also defends
against a misbehaving device: an out-of-range used id or a corrupted chain
cannot loop or overrun the driver.

v1 scope, stated: queue 0 only, no MSI-X, completions are polled. The
kernel is single-CPU and its callers block on I/O anyway, so interrupt
completion buys nothing yet; it arrives with async I/O.

## virtio-blk (`kernel/drivers/virtio_blk.c`)

Each request is the §5.2.6 three-descriptor chain: a 16-byte header
(type + starting sector, device-readable), one 4 KiB data buffer, and a
status byte (device-writable). The driver polls the used ring with a
2-second timer deadline — a dead device yields `-EIO`, never a hang — and
checks the device status byte before declaring success. Capacity is read
from device config via the generation counter, translating the device's
512-byte sectors to the kernel's 4 KiB blocks.

## Block layer (`kernel/block/block.c`)

Filesystems see an array of `BLOCK_SIZE` (4 KiB) blocks:

- `block_read`/`block_write` go through a 64-entry LRU cache
  (256 KiB of PMM frames). Writes are **write-through**: the cache never
  holds dirty data, so a crash can only lose what the filesystem had not
  yet ordered. `fsync`-driven flushing arrives with the write path (5d).
- Driver buffers must be physically contiguous; the cache's frame-backed
  entries satisfy this, and callers above the cache may pass any kernel
  memory.
- v1 concurrency contract: one caller at a time, asserted (`busy` guard).
  I/O takes milliseconds, so callers must not hold interrupts off. A
  sleeping lock replaces the contract when the VFS gives multiple
  processes concurrent disk access (5c).

Boot runs a self-test that round-trips a pattern through the *raw driver
ops* on the last block (a cached read-after-write would pass without
touching hardware), restores the original contents, then verifies the
cached path agrees:

```
virtio-blk: 16 MiB (32768 sectors), queue size 128
block: virtio-blk, 16 MiB (4096 blocks of 4096), cache 256 KiB
block: selftest passed (write/readback/restore)
```

A machine without a virtio-blk device still boots; storage-dependent
features report the absence explicitly.

## Known limits of this slice (lifted in later slices)

- Single block device; no partitions (the fs disk is one filesystem).
- Polled completion; no MSI-X, no async I/O, no request batching.
- Write-through cache only; no dirty tracking until fsync lands (5d).
- The block layer is single-caller by contract until the VFS adds a
  sleeping lock (5c).

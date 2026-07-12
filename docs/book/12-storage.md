# Chapter 12 — Storage: PCI, virtio, and the Block Layer

To load a program from a disk, the kernel first has to *find* the disk on the
bus, learn how to talk to it, and build a layer that turns "the device" into "an
array of blocks I can read and write." This chapter is the storage stack from the
bus scan up: PCI enumeration, a modern virtio-blk driver written against the
VIRTIO 1.2 specification, the split-virtqueue mechanism underneath it, and a
caching block layer. It is also the chapter where "implement the published spec,
completely" replaces "invent something that works," because the device on the
other end obeys a standard and you must too.

## 12.1 PCI: finding what is on the bus

Devices announce themselves through PCI configuration space, reachable via the
legacy `0xCF8`/`0xCFC` I/O port pair (configuration mechanism #1). `pci_init()`
(`kernel/drivers/pci.c`) walks bus/device/function tuples, reading each function's
vendor and device IDs and class code. A vendor ID of `0xFFFF` means "no function
here." The boot log shows the enumeration:

```
pci: 00:04.0 1af4:1042 class 01.00
```

Vendor `1af4` is Red Hat / virtio; device `1042` is a modern virtio-blk device;
class `01.00` is a mass-storage block device. That one line is the kernel
discovering its disk. The scan is deliberately *flat* — enumerate functions,
match the ones you have drivers for — rather than a full recursive
bridge-traversal, because in the target (QEMU) environment the devices sit
directly on bus 0. That is complete-or-absent again: the scan covers exactly the
topology the system runs on, and a real-hardware bridge walk is additive later
work rather than speculative complexity now.

## 12.2 virtio: implementing a spec, not inventing an interface

The disk is a **virtio** device — the industry-standard paravirtual device
interface, the same one real cloud VMs use. It is worth being clear-eyed about
why: virtio is not a QEMU shortcut, it is a *published specification* (VIRTIO
1.2), and driving it correctly means implementing that spec faithfully, the same
way the ELF loader implements the ELF spec and the bootloader implements the BIOS
and long-mode contracts. "From scratch" in this project has never meant "make
something up"; it means "no third-party code," while every *external* interface is
implemented against its authoritative document.

QEMU is launched with `disable-legacy=on`, so it exposes the pure VIRTIO 1.x
interface (device id `1af4:1042`) and the driver does not implement the pre-1.0
legacy layout *at all*. Supporting one clean interface completely beats
half-supporting two. The modern transport (`virtio_pci.c`) discovers the device's
register regions through **PCI capabilities** — a linked list in config space
that points at the common, notify, and device-specific configuration structures —
and drives the standard bring-up handshake: reset the device, set the ACKNOWLEDGE
and DRIVER status bits, negotiate feature flags, set up the queues, then set
DRIVER_OK. Each of those steps is mandated by the spec and done in order; skipping
or reordering them is undefined behavior on the device side.

## 12.3 The virtqueue: the shared-memory ring at the heart of virtio

All virtio data transfer flows through **virtqueues** — split rings in memory
shared between driver and device. A split virtqueue has three parts: a
**descriptor table** (each entry: a physical buffer address, a length, flags, and
a `next` index to chain descriptors), an **available ring** (where the driver
publishes descriptor-chain heads it wants processed), and a **used ring** (where
the device publishes the chains it has finished). A disk request is a chain of
descriptors — a header (sector, read/write), the data buffer, and a status byte —
whose head the driver puts on the available ring; the driver then notifies the
device, the device performs the I/O and posts the head on the used ring.

The ring index arithmetic — allocating and freeing descriptors, wrapping the
available and used ring indices, matching completions back to requests — is
exactly the kind of intricate, off-by-one-prone bookkeeping that Chapter 1's
pattern exists for. So it is factored into a **pure** `virtq_core.c`, host-tested
in `tests/host/test_virtq.c` against a simulated device that plays the other side
of the ring. The bookkeeping is proven on the host under sanitizers; only the
MMIO doorbell writes and the physical-address plumbing are trusted to the kernel
driver. A shared-memory protocol with a device is precisely where a subtle ring
bug corrupts I/O in ways that are agonizing to debug on real hardware — so it is
the last place you want to be debugging without a safety net, and the first place
the pure-core discipline earns its cost.

`virtio_blk.c` builds on that: it assembles the three-descriptor request (header,
data, status), submits it, and — in this v1 — **polls** for completion with a
bounded timeout rather than sleeping on an interrupt. Polling is the simpler,
completely-implemented choice for a single synchronous requester; interrupt-driven
completion and request concurrency are noted as later work, gated behind a real
VFS that would have multiple callers.

## 12.4 The block layer: from a device to an array of blocks

Above the driver sits the block layer (`kernel/block/block.c`), which presents the
abstraction the filesystem actually wants: a flat array of fixed-size **4 KiB
blocks** you can read and write by number, over whatever driver happens to be
registered. Two design points matter.

First, **the block size is 4 KiB even though the device's native sector is 512
bytes.** 4 KiB matches the page size and the filesystem's block size, so a block
is a page is a filesystem block — one number everywhere, no impedance mismatch
between layers. Choosing your internal granularity to align across subsystem
boundaries eliminates a whole class of conversion bugs.

Second, there is a **write-through LRU cache** in front of the device (the boot
log: "cache 256 KiB"). Reads check the cache first; writes update the cache *and*
go straight to the device (write-through, so a crash never loses an acknowledged
write — there is no dirty data sitting only in RAM). LRU eviction keeps the
working set hot. Write-through rather than write-back is a deliberate durability-
over-throughput choice appropriate to a filesystem that is itself designed around
crash consistency (next chapter) — the cache must never become a place where
committed data can be lost.

A current simplification, honestly scoped: the block layer assumes a **single
caller at a time** until the VFS adds a sleeping lock. The comment says so at the
point it matters. When the VFS arrives with concurrent filesystem operations, a
proper lock replaces the assumption — the same "documented shortcut at the
load-bearing site" pattern as the scheduler's interrupts-off lock.

## 12.5 Proving the disk works before trusting it with a filesystem

`block_selftest()` runs on every boot and does a **write / readback / restore**
round trip against the *real* device: save a block's contents, write a known
pattern, read it back and verify, then restore the original bytes. The boot log:

```
block: selftest passed (write/readback/restore)
```

This is the storage stack's version of the recurring standard — do not assume the
device works, make it demonstrate a correct round trip on every boot, and do it
non-destructively so the test itself is safe to run against a live disk. Before
the filesystem stakes anything on the block layer, the block layer has proven,
this boot, on this hardware, that a byte written comes back.

## 12.6 The transferable lessons

- **Implement the spec, completely, for the one interface you target.** Modern
  virtio only, `disable-legacy=on`; the ELF/BIOS/VIRTIO contracts are all
  *implemented against their documents*, never approximated. "From scratch"
  means no borrowed code, not invented protocols.
- **The intricate ring bookkeeping is pure — host-test it.** A shared-memory
  device protocol is the worst place to debug an off-by-one on real hardware, so
  `virtq_core.c` is proven under sanitizers against a simulated peer.
- **Align granularity across layers.** 4 KiB block = page = filesystem block
  removes conversion bugs at every boundary.
- **Choose the cache policy that matches the durability contract.** Write-through
  keeps the cache from ever being a place committed data can be lost — the right
  call under a crash-consistent filesystem.
- **Make the device prove a round trip every boot.** Non-destructive
  write/readback/restore, asserted, before anything depends on it.

The kernel can now read and write blocks reliably. What it does not yet have is
*structure* — files, directories, names. Turning a flat array of blocks into a
filesystem, and doing it with the crash-consistency and integrity guarantees of a
modern design, is the final subsystem chapter.

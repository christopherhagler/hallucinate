# Real Hardware: Flashing and Booting

Every other test in this project runs the kernel under QEMU, which is honest
about most things and generous about a few: it always has a disk if you ask
for one, its firmware is predictable, and its E820 map, VBE modes, and A20
handling never surprise anyone. "I will run this on real hardware so it must
work on a real machine" (the project's own stated requirement) means that
generosity has to be tested against, not just documented — early, before the
gap between QEMU and a physical board turns into a long debugging session on
borrowed hardware. This appendix covers the current smoke test: getting the
kernel to boot on a real machine, with no filesystem disk yet, and observing
that it degrades the way Chapter 14 §14.8 says it should instead of
panicking.

## What this test proves, and what it doesn't

There is no AHCI or NVMe driver yet — only the virtio-blk driver, which no
real desktop board speaks. So this smoke test necessarily boots **without a
filesystem disk**, exercising the disk-less path documented in Appendix J
and gated on every commit by `make check-boot-nodisk`. It proves:

- The bootloader's real-mode → protected-mode → long-mode transition, A20
  handling, and E820 parsing survive contact with real BIOS firmware instead
  of QEMU's.
- GDT/TSS/IDT setup, the PIC, PMM/VMM bring-up, the kernel heap, and the
  scheduler all initialize on real silicon.
- The PS/2 keyboard driver and VGA text console (or a real serial port —
  see below) work against real hardware, not an emulated 8042 and a fake
  UART.
- The graceful-degradation path added alongside this appendix — no panic
  when `vfs_init` finds no disk — actually holds outside QEMU, not just in
  the two `run_qemu.py` configurations.

It does **not** prove storage works on real hardware (no driver exists for
the board's AHCI/NVMe controller yet), and it does not launch userspace
(`process_run_init` has nothing to load `/bin/init` from). Both are future
work tracked in Chapter 17. This is a firmware-and-core-kernel test, not a
full-system one.

## Target hardware

The reference machine for this project (see the repo's project notes) is:

- **CPU:** AMD Ryzen 9, socket AM4 (Zen 2/3) — no integrated GPU, so a
  discrete GPU is required for video output.
- **Motherboard:** MSI B550 GAMING PLUS, MSI Click BIOS 5.
- **Storage:** SATA/AHCI and an M.2 NVMe slot — neither has a driver yet
  (see above).
- **I/O relevant to this OS:** a physical PS/2 combo port (this kernel's
  PS/2 driver talks to it directly, no USB HID layer needed) and a JCOM1
  header for a real RS-232 serial console (verify pin-out against the board
  manual before wiring a bracket — get this wrong and you risk the board,
  not just a boot).

Any similarly-specced board should work; the BIOS menu names below are
MSI's, adjust for other vendors.

## Step 1: build the image

```
make usbimg
```

This builds `build/disk.img` (identical to what `make run`/`make check` use)
and prints the flashing command. `disk.img` (`tools/mkimage.py`) is already
a raw MBR image — stage 1 boot sector at LBA 0, stage 2 and the kernel ELF
padded and laid out after it, the whole thing rounded up to a MiB. Writing
it byte-for-byte to a USB stick's raw block device makes that stick boot the
same way QEMU's `-drive file=disk.img,format=raw` does; there is no separate
"USB format" step.

## Step 2: flash a USB stick

**This step writes directly to a block device. Getting the target wrong
overwrites an arbitrary disk, up to and including the one macOS is running
from.** `tools/flash_usb.sh` exists specifically to make that mistake hard:
it refuses to run without an explicit device argument, refuses to touch
whatever `diskutil info /` reports as the boot disk, and requires you to
retype the device path at a confirmation prompt before it unmounts anything.

```
diskutil list                      # find the stick, e.g. /dev/disk4 (NOT diskNsM)
make flash-usb DEV=/dev/disk4      # or: tools/flash_usb.sh /dev/disk4
```

It unmounts the disk, `dd`s the image to its **raw** device node
(`/dev/rdiskN` — an order of magnitude faster than the buffered node on
macOS), and ejects when done. Press Ctrl+T during the `dd` for a progress
report (BSD `dd` honors `SIGINFO`; there is no `status=progress` on macOS).

On Linux, there is no `diskutil`; find the device with `lsblk`, make sure
nothing under it is mounted, and run
`sudo dd if=build/disk.img of=/dev/sdX bs=4M status=progress conv=fsync`
by hand, same caution about the target applying.

## Step 3: BIOS configuration (MSI Click BIOS 5)

This bootloader is a classic BIOS/MBR chain (`INT 13h` extended reads, a
0xAA55 signature) — it needs **CSM** (Compatibility Support Module), MSI's
"Legacy+UEFI" boot mode, not pure UEFI. Enable it before the first boot
attempt:

1. Enter setup: power on, tap **Del** (or **F2**) repeatedly.
2. Switch to **Advanced Mode** (F7 toggles it) if the board opens to EZ
   Mode.
3. **Settings → Advanced → Windows OS Configuration → BIOS Legacy/UEFI
   mode** → set to **Legacy+UEFI** (this is the CSM toggle on this board;
   pure **Legacy** also works if offered and UEFI boot of anything else
   isn't needed).
4. **Settings → Advanced → Integrated Graphics Configuration**, if present
   and a discrete GPU is installed: confirm the discrete GPU is the primary
   display adapter.
5. Save and exit (**F10**), then immediately tap **F11** for the one-time
   boot menu and pick the USB stick, or set **Settings → Boot → Boot
   Option #1** to the USB device for a persistent change.

**CSM caveat:** the discrete GPU needs a legacy VBIOS option ROM to show any
video output at all before an OS driver loads (true for essentially every
GPU through the RTX 30 / RX 6000 generation; newer cards increasingly drop
legacy VBIOS support entirely — check the card's spec page if video output
is blank). If video never comes up, the serial console (next section) is
the fallback for diagnosis regardless.

## Step 4: watching it boot

**VGA (default):** the kernel's text-mode console is the fallback path and
needs no configuration; connect a monitor to the discrete GPU.

**Serial (recommended if the JCOM1 header is wired to a bracket):** this
kernel also writes every `kprintf` to the first UART, same as QEMU's
`-serial stdio`. From another machine:

```
screen /dev/tty.usbserial-XXXX 115200   # macOS, adjust device name
```

A serial console is strictly better for this test: it survives a video-side
failure, and the transcript can be copied for comparison against the
`PASS_MARKERS_NO_DISK` list in `tests/run_qemu.py` (Appendix L) line for
line.

## What a successful boot looks like

The transcript should match the shape of `PASS_MARKERS_NO_DISK`
(`tests/run_qemu.py`) — the same sequence `make check-boot-nodisk` asserts
in CI, just on real firmware instead of QEMU's:

```
Hallucinate OS v0.5.0 (x86_64)
cpu: GDT/TSS loaded, IDT ready (256 vectors), PIC remapped and masked
e820: N entries
...
pmm: ...
vmm: kernel page tables active
heap: slab allocator ready
sched: online, round-robin, 10 ms timeslice
syscall: SYSCALL/SYSRET ready (Linux x86_64 ABI numbering)
timer: 100 Hz, ticking (slept N ticks)
pci: N functions
virtio-blk: no device
block: selftest skipped (no device)
vfs: no block device found — booting without a root filesystem
vfs: devfs at /dev (console)
selftest: ... (all levels pass)
selftest: fs write-path test skipped (no root filesystem)
selftest: passed (N assertions)
process: no root filesystem, skipping init
boot: complete
keyboard: type on this console; input echoes here
```

Once `boot: complete` prints, typing on the keyboard should echo to the
console: that loop is standing in for a shell until Phase 6, and on real
hardware it is also the first proof that the PS/2 driver and IRQ routing
work outside an emulator.

## Diverging from the expected transcript

Expect the *shape* to survive but the *numbers* not to: real E820 maps have
more entries and different reserved regions than QEMU's, usable RAM will
differ, and PCI enumeration will list real devices instead of QEMU's
synthetic ones. A difference in a count or address is not a bug by itself —
compare against the marker list's fixed strings (Appendix L), not the
interpolated numbers.

If the machine hangs before any output: check CSM/Legacy mode is really
active (some boards partially ignore the setting for USB devices
specifically), try the other USB port type (2.0 vs 3.0 controllers are
sometimes on different legacy code paths), and confirm the stick was
written to the raw device node, not a partition. If it hangs after some
output: the last marker printed identifies the failing subsystem directly.

A `PANIC` on real hardware that never triggers in the `check-boot-nodisk`
QEMU run is itself a finding — it means a firmware assumption QEMU was
quietly generous about (E820 shape, an unexpected PCI device class, a BIOS
quirk in the `INT 13h` extended-read path) needs a fix, which is exactly
what an *early* smoke test is for.

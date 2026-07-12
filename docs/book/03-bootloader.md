# Chapter 3 — The Bootloader

This is the chapter where the machine is at its most primitive and least
forgiving. When the firmware hands control to your code, the CPU is pretending
it is 1981: 16-bit real mode, a 20-bit address space, no memory protection, no
paging, and a BIOS whose services vanish the moment you leave real mode. Your
job is to climb from there to 64-bit long mode with paging on and a kernel
loaded at a high virtual address — and to do it without a debugger, a stack you
can trust, or a `printf`. It is the purest systems programming in the book.

## 3.1 What the firmware guarantees (and nothing more)

On a legacy BIOS boot, the firmware reads the first 512 bytes of the boot disk —
the Master Boot Record — into physical address `0x7C00`, checks that its last
two bytes are `0x55 0xAA`, and far-jumps to it. That is the entire contract. You
get:

- The CPU in **16-bit real mode**. Addresses are `segment:offset`, computed as
  `segment * 16 + offset`, giving a ~1 MiB address space. There is no memory
  protection whatsoever.
- `DL` = the BIOS drive number you booted from. You will need it for every disk
  read.
- 512 bytes of code, minus the 2-byte signature, to work with. That is not
  enough to do anything real, which is why boot is *two stages*: the MBR's only
  job is to load a bigger second stage.
- The BIOS interrupt services (`INT 0x10` video, `INT 0x13` disk, `INT 0x15`
  memory map) — available *only* in real mode.

Everything else — the stack, the segment registers' contents, whether the A20
line is enabled — is unspecified. Professional boot code assumes nothing it was
not handed.

## 3.2 Stage 1: the 512-byte handoff

`boot/stage1.asm` does exactly one thing well: load stage 2 and jump to it. Read
its opening:

```asm
start:
    ; Normalize segments and stack; some BIOSes jump here with CS=07C0.
    cli
    jmp 0x0000:.canon
.canon:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    cld
    mov [boot_drive], dl
```

Every line defends against an unstated assumption. Some BIOSes enter with
`CS=0x07C0, IP=0x0000` and others with `CS=0x0000, IP=0x7C00` — the same
physical address, different segmentation. The far `jmp 0x0000:.canon`
**canonicalizes** `CS:IP` so the rest of the code knows its own addressing. The
segment registers are then zeroed so `[boot_drive]` and every other absolute
reference resolves correctly, and the stack is planted at `0x7C00` growing down
(into the space below the code, which is free). `cld` fixes the string-op
direction flag, which the BIOS leaves in an unknown state. Only *then* does it
save `DL`. This is defensive coding at the lowest level: before you use a
resource, put it in a known state, because the layer below you promised nothing.

Next, stage 1 refuses to guess about the disk:

```asm
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13            ; INT 13h extensions present?
    jc  .err_no_ext
    cmp bx, 0xAA55
    jne .err_no_ext
```

It checks for INT 13h *extensions*, which provide LBA (linear block address)
reads via a Disk Address Packet. The old CHS (cylinder-head-sector) geometry is
deliberately **not** supported — a documented minimum-platform requirement, true
on everything since the mid-90s and always in QEMU. This is complete-or-absent
again: rather than a fragile CHS fallback that would rot untested, an explicit
capability check and a clear error. The read itself retries three times with a
controller reset between attempts, because real drives fail transiently right
after power-on — a robustness detail that only matters on the real hardware this
project intends to eventually boot on, written in now so it is not a scramble
later.

Finally, stage 1 verifies stage 2 actually loaded (its first dword is the magic
`"HLS2"`) before jumping to it. It does not trust the disk read to have done
what it asked; it checks. And every failure path prints a message containing
`ERR:` and halts — the string the test harness keys on to fail a boot
immediately instead of hanging. **Make your failures loud and machine-detectable.**

## 3.3 Stage 2: the climb, in seven moves

Stage 2 (`boot/stage2.asm`, ~530 lines) has room to do real work. It executes a
precise sequence, each step a prerequisite for the next. The full contract is
`docs/boot-protocol.md`; here is what each move *is* and why it is hard.

**1. Enable the A20 line.** For backwards compatibility with the 1 MiB
wraparound behavior of the original 8086, PCs boot with address line 20 forced
low, so physical addresses above 1 MiB alias down. You cannot use memory above
1 MiB until A20 is on. Stage 2 tests for wraparound (writes differ at `0x0500`
and `0x100500`), and if disabled tries three methods in order — the BIOS
(`INT 15h AX=2401h`), then the "fast A20" port `0x92`, then the ancient 8042
keyboard controller. Every keyboard-controller poll is bounded (64K iterations)
so a dead controller cannot hang the machine forever. The lesson in that
bounding: **in boot code, every wait must have a ceiling**, because there is no
watchdog above you to break a spin loop.

**2. Get the memory map (E820).** The kernel needs to know which physical
address ranges are actual RAM versus reserved/MMIO/ACPI. The only portable way
to learn this is `INT 15h, AX=E820h`, which the BIOS answers one entry at a
time. Stage 2 walks it, storing up to 64 raw 24-byte entries directly into the
bootinfo block the kernel will read. A subtle correctness detail: it presets the
ACPI extended-attributes dword to 1 before each call, so that BIOSes which
return only the older 20-byte entry still leave a valid attribute field. Getting
this wrong gives you a memory map that looks fine and occasionally isn't — the
worst kind of bug.

**3. Load the kernel with unreal mode.** The kernel ELF must land at physical
16 MiB, but real mode can only address ~1 MiB. The trick is **unreal mode**:
briefly enter protected mode to load a segment descriptor with a 4 GiB limit,
then drop back to real mode. The segment's cached limit stays 4 GiB, so
real-mode code can now use 32-bit offsets (`a32 rep movsd`) to touch high
memory while still calling BIOS disk services. Stage 2 reads the kernel in 32
KiB chunks to a low buffer, then copies each chunk high. Critically, it
re-enters unreal mode after *every* INT 13h call, because the BIOS may reset the
cached descriptor limits behind its back — exactly the kind of "the layer below
me silently changed my state" hazard that defines this environment.

**4. Enter 32-bit protected mode.** Load a Global Descriptor Table (null, 32-bit
code, data, and a 64-bit code descriptor prepared for the next step), set
`CR0.PE`, and far-jump to flush the pipeline into 32-bit code.

**5. Build page tables.** Long mode *requires* paging — you cannot enter it with
paging off. Stage 2 hand-builds a minimal 4-level table at `0x70000` mapping the
first 1 GiB of physical memory twice with 2 MiB pages: an **identity** map
(`PML4[0]`, so virtual == physical, needed for the instruction pointer right
after the switch) and a **higher-half** map at `0xffffffff80000000`
(`PML4[511] → PDPT[510]`, the kernel's link address). Both point at one shared
page directory, so 1 GiB is described by a handful of tables. Chapter 7 explains
paging structures properly; here the point is that *just enough* mapping is
built to make the jump survivable, and the kernel will throw all of it away and
build real tables later.

**6. Enter long mode.** The canonical dance: set `CR4.PAE`, load `CR3` with the
PML4, set `EFER.LME` (the long-mode-enable bit in MSR `0xC0000080`), then set
`CR0.PG` to turn paging on — and the CPU transitions to long mode. A far jump
into the 64-bit code segment flushes the pipeline. Order matters absolutely
here; PAE before paging, LME before PG. Get it wrong and the CPU faults with no
handler installed.

**7. Load the ELF.** Now in 64-bit mode, stage 2 parses the staged kernel image
as ELF64 and, for each `PT_LOAD` segment, validates it (magic, 64-bit,
little-endian, `EM_X86_64`, `filesz ≤ memsz`, target `≥ 1 MiB`, ends below the
staging area, entry point in the higher half) and then copies the file bytes to
the segment's physical address and zeroes the BSS tail. It is a miniature,
paranoid ELF loader — the same job the kernel's own loader does for userspace in
Chapter 11, done here for the kernel itself.

Finally it jumps to the ELF entry point with `RDI` holding the physical address
of the bootinfo block — the first argument, per the System V ABI, to the C
function `kmain` will become.

One more detail that captures the environment's cruelty: **once you leave real
mode, the BIOS is gone**, so stage 2's post-protected-mode error paths cannot
call `INT 0x10` to print. They write white-on-red bytes directly into the VGA
text buffer at physical `0xB8000` instead. When you climb past a layer's
services, you must bring your own replacement for everything it did for you,
including how you scream when you fail.

## 3.4 The boot protocol as a versioned contract

Notice what the last step actually is: stage 2 fills a `struct bootinfo` at a
fixed physical address (`0x6000`) and passes its pointer to the kernel. That
struct — magic `"HLCN"`, a version, the boot drive, and the E820 array — is a
**versioned interface** (`docs/boot-protocol.md`, `BOOTINFO_VERSION`). The
kernel validates all of it on entry (`bootinfo_get()` in `main.c`) and panics on
any mismatch: bad magic, wrong version, zero or too-many E820 entries.

This is the professional move that turns two piles of assembly and C into
*components*. The bootloader and kernel are developed and can fail
independently, but they meet at a single documented, versioned, runtime-checked
contract. Any incompatible change bumps the version in the header and the doc in
the same commit. When you build a boundary between two pieces of low-level code
— and a kernel is nothing but such boundaries — give it a magic number, a
version, and a validator on the receiving side. It is three lines of code and it
converts a class of silent-corruption bugs into a loud, early panic.

## 3.5 The transferable lessons

- **Assume nothing the layer below you did not promise in writing.** Stage 1
  canonicalizes `CS:IP`, zeroes segments, and fixes the direction flag before
  doing anything, because the BIOS guaranteed none of them.
- **Every wait has a ceiling.** There is no watchdog beneath the bootloader; an
  unbounded spin is a hang with no recovery.
- **Bring your own everything after you leave a layer.** Past protected mode,
  no BIOS: your own page tables, your own screen output, your own ELF loader.
- **Boundaries get magic numbers, versions, and validators.** The bootinfo
  block is the template for every inter-component contract in the system.

Next, the kernel's C code takes over — and the very first thing it does is
throw away the CPU tables the bootloader set up and install its own.

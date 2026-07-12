# Chapter 4 — Kernel Entry and the CPU's Tables

The bootloader jumps to `_start` in 64-bit long mode, and now C code — your code
— owns the machine. But the CPU is still using the bootloader's throwaway
descriptor tables, has no interrupt handlers, and could not survive a single
exception. This chapter is about the CPU-level furniture every x86_64 kernel
must build before it can do anything else: a stack, a GDT, a TSS, and an IDT.
These are the tables the *hardware itself* reads, and getting their formats
exactly right is non-negotiable.

## 4.1 The entry stub: the smallest possible bridge

`kernel/arch/x86_64/entry.asm` is the entire assembly boundary between the
bootloader and C:

```asm
_start:
    cli
    cld
    lea rsp, [rel kstack_top]
    xor ebp, ebp
    call kmain
.hang:
    cli
    hlt
    jmp .hang

section .bss
align 16
kstack:
    resb 16384                       ; 16 KiB boot stack
global kstack_top
kstack_top:
```

It does four things and stops. Interrupts off (there is no IDT yet, so an
interrupt would be fatal). Direction flag cleared. **A stack installed** — the
boot protocol explicitly guarantees no usable stack, so the kernel provides its
own 16 KiB in `.bss`. `rbp` zeroed to terminate stack-unwind chains at the root.
Then `call kmain`, which never returns; the `hlt` loop is a backstop for the
impossible.

Two details reward attention. The stack lives in `.bss`, which the bootloader's
ELF loader zeroed, so it starts clean. And `kstack_top` is `global` with a
comment — "the scheduler adopts this as thread 0's stack" — because Chapter 9's
scheduler will treat this boot stack as the stack of the very first thread. The
smallest file in the arch directory already anticipates a subsystem three
chapters away. That is what it looks like when the boundaries are designed
rather than accreted.

## 4.2 kmain: bring-up as a total ordering

`kmain` (`kernel/main.c`) is the spine of the whole kernel — the exact order in
which subsystems come alive. The ordering is not stylistic; it is a dependency
graph, and every edge is real:

```c
void kmain(uint64_t bootinfo_phys) {
    console_init();                 // 1. output first — you must be able to see failures
    gdt_init(); idt_init(); pic_init(); irq_init();  // 2. survive exceptions
    const struct bootinfo *bi = bootinfo_get(bootinfo_phys);  // 3. validate the contract
    pmm_init(bi);                   // 4. physical frames  (needs the E820 map)
    vmm_init(bi);                   // 5. real page tables  (needs frames to build them)
    kmalloc_init();                 // 6. heap  (needs virtual memory)
    sched_init();                   // 7. threads  (needs a heap for stacks)
    syscall_init();                 // 8. SYSCALL/SYSRET MSRs
    timer_init(100); keyboard_init(); cpu_enable_interrupts();  // 9. now safe to take IRQs
    pci_init(); virtio_blk_init(); block_selftest();  // 10. storage
    selftest_run();                 // 11. prove the lower half works
    process_run_init();             // 12. ring 3
    kprintf("boot: complete\n");
}
```

The first non-obvious choice: **`console_init()` is first**, before even the CPU
tables, so that if anything after it panics you can see the message. You bring up
your eyes before you bring up anything you might need to watch. The second:
interrupts are enabled (step 9) only *after* the IDT, PIC, timer, and keyboard
are all ready — turn them on any earlier and the first stray IRQ jumps through an
uninitialized vector. The third: memory management comes up in strict layers —
physical frames, then virtual mapping built out of those frames, then a heap
carved from that virtual space, then thread stacks from the heap. You cannot
reorder any of it. When you design a bring-up sequence, write it as a dependency
list first; the code order falls out of it.

## 4.3 The GDT and TSS: segmentation's vestige, and the double-fault stack

In long mode, segmentation is *mostly* dead — the flat 64-bit address space
means code and data segments have base 0 and no limit. But the Global Descriptor
Table has not disappeared, because two things still need it:

1. **Privilege levels.** The CPU determines ring (0 = kernel, 3 = user) from the
   code segment selector's low bits. You need a ring-0 code/data pair and a
   ring-3 code/data pair. `SYSCALL`/`SYSRET` (Chapter 10) also derive their
   selectors from the GDT layout by fixed offset arithmetic, which is why
   `gdt.h` lays the user descriptors out in a specific order — a Phase 2
   decision made to serve a Phase 4 feature.

2. **The Task State Segment.** The one field of the TSS that matters in long
   mode is `rsp0`: the stack pointer the CPU loads automatically when an
   interrupt or trap crosses from ring 3 to ring 0. Without it, an interrupt
   taken while userspace is running would try to push the interrupt frame onto
   the *user* stack — a security hole and a reliability disaster. The scheduler
   updates `TSS.rsp0` on every context switch to point at the incoming thread's
   kernel stack (Chapter 10).

The TSS also carries the **Interrupt Stack Table**, and this codebase uses one
IST slot for a specific, sharp reason: the double-fault handler runs on a
*dedicated* stack. A double fault (`#DF`) is what the CPU raises when it faults
while trying to deliver another fault — for example, a page fault whose handler
itself page-faults because the kernel stack overflowed into an unmapped guard
page. If `#DF` tried to use the same broken stack, it would fault again and
escalate to a triple fault, which resets the machine. Giving `#DF` its own known-
good IST stack means even a kernel-stack-overflow produces a diagnostic instead
of a silent reboot. That is a deliberate investment in *debuggability of the
worst case* — exactly where beginners under-invest.

## 4.4 The IDT: 256 vectors and one dispatcher

The Interrupt Descriptor Table maps each of the 256 interrupt/exception vectors
to a handler. The CPU uses vectors 0–31 for architectural exceptions (0 = divide
error, 6 = invalid opcode, 13 = general protection, 14 = page fault, ...), and
0x20 upward will be the hardware IRQs after the PIC is remapped (Chapter 5).

Writing 256 near-identical assembly stubs by hand is exactly the kind of tedium
that breeds copy-paste bugs, so `isr.asm` generates them with a macro. Each stub
pushes the vector number (and a dummy error code for the exceptions that do not
push one, so the stack frame is *uniform* across all vectors — a small
normalization that makes the C side vastly simpler), then jumps to a common path
that saves registers and calls one C dispatcher, `trap_dispatch` (`trap.c`).
Uniform frames from non-uniform hardware is a classic and worth copying: absorb
the irregularity at the lowest layer so everything above it sees one shape.

The dispatcher's policy is where the design shows. For an *unhandled* exception
in kernel mode, it dumps every register and panics — a kernel exception is a
kernel bug by definition, and you want the full state at the moment of death.
But `trap_dispatch` first checks the saved code segment's privilege bits (the
RPL), and this is the crucial fault-isolation decision the whole userspace model
rests on:

> A hardware exception raised in **ring 3** is never the kernel's problem: the
> dispatcher logs one line and kills the offending process with the matching
> Linux signal, while the kernel and every other process keep running. Only
> ring-0 faults — and machine-level events like NMI, `#DF`, `#MC` — still panic.

We will not have processes to kill until Chapter 11, but the *mechanism* — "look
at who was running when the fault hit, and respond differently for kernel vs.
user" — is built into the trap dispatcher from the start. The right place to
decide whether a fault is fatal is the single choke point every fault flows
through, and that place exists precisely because the IDT funnels all 256 vectors
into one dispatcher.

## 4.5 The transferable lessons

- **Bring up your output before anything that might fail.** You cannot debug
  what you cannot see, and the first subsystem to break is often an early one.
- **Absorb hardware irregularity at the lowest layer.** Uniform interrupt
  frames, synthesized from vectors that do and do not push error codes, make
  every layer above simpler and less bug-prone.
- **Invest in the worst case.** The dedicated double-fault stack costs one IST
  entry and turns a silent triple-fault reboot into a readable panic. The
  cheapest time to build worst-case diagnostics is before you need them.
- **Funnel decisions through one choke point.** Kernel-vs-user fatality is
  decided in the single dispatcher all faults reach, which is why the policy is
  one `if` instead of scattered special-casing.

The CPU can now survive an exception and knows its privilege levels. Next it
needs to handle the *asynchronous* interrupts — the timer and the keyboard —
that turn a program that runs top-to-bottom into a system that responds to the
world.

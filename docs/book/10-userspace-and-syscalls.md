# Chapter 10 — Userspace and System Calls

This chapter crosses the most important boundary in the system: from ring 0,
where code can do anything, to ring 3, where a process is boxed into its own
address space and can only affect the outside world by asking the kernel. Getting
this transition *exactly* right — every register, every stack, every privilege
bit — is the difference between an isolation boundary and a security theater. The
whole point of an OS is that a buggy or malicious program cannot take down the
system or read another program's memory, and that guarantee is enforced entirely
by the mechanisms in this chapter.

## 10.1 The address space is the isolation

Chapter 7 built the kernel's page tables. A user process gets its *own* PML4,
built by `vmm_addrspace_create_user()`, and the design is elegant:

> The user PML4's **upper half aliases the kernel PML4's entries 256–511**: the
> kernel is fully mapped into every process, so interrupts, syscalls, and the
> scheduler need no `CR3` gymnastics — but it is inaccessible from ring 3,
> because no kernel mapping carries `PTE_US`.

This is the `US`-bit rule from Chapter 7 cashed in. The kernel is present in
every address space (so a syscall or interrupt from any process lands in mapped
kernel code without switching page tables), yet ring 3 faults the instant it
touches a kernel address, because the walk requires `US` at every level and the
kernel's entries never set it. One PML4 per process, upper half shared and
protected, lower half private. A process literally cannot *name* another
process's memory or the kernel's — the addresses either are not mapped in its
space or are mapped without user permission.

One invariant falls out of sharing the upper half by aliasing: **the kernel never
adds a new top-level PML4 entry after `vmm_init()`.** If it did, existing
processes — whose upper halves were aliased at creation time — would not see the
new entry, and the kernel would be partially unmapped in old address spaces. All
kernel growth happens *underneath* the PML4 slots populated at init. That is a
non-obvious constraint that the address-space design silently imposes, and the
kind of thing you must write down (the userspace doc does) because nothing in the
code stops you from violating it — it just breaks mysteriously three processes
later.

The scheduler ties it together: it tracks an address space per thread (`NULL` for
pure kernel threads), and on a context switch reloads `CR3` **only if the target
differs from what is active**. Pure kernel threads never pay a TLB flush; two
threads of the same process do not flush on a switch between them. And on every
switch it points `TSS.rsp0` and the syscall-stack global at the incoming thread's
kernel stack — the `rsp0` from Chapter 4 and the `syscall_kstack` you are about
to meet, kept current so that *whenever* the CPU crosses into the kernel, it lands
on the right stack.

## 10.2 The syscall ABI: standing on Linux's shoulders

The native system-call convention is **identical to the Linux x86_64 ABI** — same
`syscall` instruction, same `rax` for the number, same `rdi rsi rdx r10 r8 r9`
for arguments, same `-errno` return convention. This is a strategic decision, not
laziness: Phase 7's goal is to run unmodified Linux binaries, and if the native
ABI already *is* the Linux ABI, the personality layer shares one numbering, one
register convention, and one error vocabulary with native code. Choosing your
interfaces today to match where you are going tomorrow is architecture; it costs
nothing now and saves a rewrite later.

The hardware clobbers exactly two registers on `syscall`: `rcx` (which it loads
with the return `rip`) and `r11` (the saved `rflags`). The ABI promises userspace
that *every other register survives the call*. That promise is the entire reason
the entry path looks the way it does.

## 10.3 The entry path, register by register

`syscall` is fast precisely because it does *not* switch stacks or save state —
it just jumps to the address in the `LSTAR` MSR with `rcx`/`r11` set. That
minimalism is the kernel's problem to clean up. Here is the real entry stub
(`kernel/arch/x86_64/syscall_entry.asm`), which is worth reading in full because
every line defends a specific hazard:

```asm
syscall_entry:
    mov [rel saved_user_rsp], rsp
    mov rsp, [rel syscall_kstack]   ; adopt this thread's kernel stack

    ; Build a struct syscall_frame: the COMPLETE user register state.
    push qword [rel saved_user_rsp] ; frame.rsp
    push rcx                        ; frame.rip   (hardware put user rip here)
    push r11                        ; frame.rflags(hardware put user rflags here)
    push rax                        ; frame.rax:  syscall nr in, return value out
    push rdi
    push rsi
    push rdx
    push r10
    push r8
    push r9
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    sti                             ; syscalls may block or be preempted
    mov rdi, rsp                    ; syscall_dispatch(frame)
    cld
    call syscall_dispatch

    cli                             ; no kernel-stack-less interrupt window on the way out
    pop r15
    ... (restore all)
    pop rax                         ; return value (dispatch wrote frame.rax)
    pop r11                         ; user rflags (sysret restores)
    pop rcx                         ; user rip
    pop rsp                         ; user stack
    o64 sysret
```

Walk the hazards it closes:

- **`syscall` runs on the user stack.** The first instruction saves the user
  `rsp` and switches to the kernel stack — read from the `syscall_kstack` global
  the scheduler keeps current (§10.1). Until that switch, the kernel is standing
  on memory the user controls, so `SFMASK` cleared `IF` (interrupts masked) at
  entry: **nothing can interrupt while we are on the user stack.** The `sti`
  comes only after we are safely on the kernel stack and the frame is built.
- **It saves *every* caller-saved user register**, not just the ones the syscall
  reads. The ABI promised userspace they survive; the C dispatcher, being a
  normal C function, will freely clobber them. So they are all pushed and all
  restored. Init actually *tests this contract*: it issues a syscall with
  sentinel values in all six argument registers and verifies them afterward.
- **The save area is precisely `struct syscall_frame`** — the field layout is
  asserted with `offsetof`. This is the linchpin of the next chapter: because the
  frame is the complete user context laid out as a struct, `fork` becomes a
  *struct copy*. The assembly and the C struct are two views of the same bytes,
  and the `offsetof` assertion is what keeps them from silently drifting apart.
- **The return path disables interrupts before switching back to the user
  stack**, so there is never a window where an interrupt could fire while the CPU
  is on a user-controlled stack with kernel expectations. Symmetric with entry.

Sixteen pushes (a detail the comment flags) keep `rsp` 16-byte aligned at the
`call`, as the ABI requires — the same alignment discipline as the thread
trampoline in Chapter 9. Alignment bugs in the entry path are especially nasty
because they only bite inside the callee, far from the cause.

The MSR setup that makes this work (`usermode.c`): `EFER.SCE` enables the
`syscall` instruction, `LSTAR` holds the entry stub's address, `SFMASK` says
which flag bits to clear on entry (`IF|TF|DF|AC`), and `STAR` holds the segment
selectors. That last one is why `gdt.h` laid out the user descriptors in a
specific order back in Chapter 4 — `sysret` computes the user selectors from
`STAR` by fixed `+8/+16` arithmetic, so the GDT had to be arranged to feed it.
A Phase 4 instruction constrains a Phase 2 table; the layout was chosen in
advance to serve it.

## 10.4 One entry primitive for every drop into ring 3

Descending *into* ring 3 — whether launching a brand-new process or resuming
`fork`'s child — is a single primitive, `user_frame_enter(frame)`, an `iretq`
that loads the frame's complete register state atomically with the ring
transition:

```asm
user_frame_enter:
    cli
    push qword 0x23             ; ss  (user data selector | RPL 3)
    push qword [rdi + 15*8]     ; frame.rsp
    push qword [rdi + 13*8]     ; frame.rflags (IF set)
    push qword 0x2B             ; cs  (user code selector | RPL 3)
    push qword [rdi + 14*8]     ; frame.rip
    ... load every GPR from the frame ...
    iretq
```

A new process's first entry is just a *zeroed* frame with `rip`/`rsp`/`rflags`
set — so nothing of the kernel's register state leaks into ring 3 — and `iretq`
drops to the entry point. `fork`'s child is the parent's saved frame with `rax`
set to 0. The *same instruction sequence* handles both, because both are
"materialize this exact user context and go." Unifying process launch and fork
resumption into one primitive is what makes the process model in Chapter 11 so
small. When two operations are secretly the same operation, finding the shared
primitive collapses a lot of code and a lot of bugs.

## 10.5 Never trust a pointer from ring 3

A user program passes the kernel pointers — the buffer for `write`, the path for
`execve`. Those pointers are *adversarial input*: they might point at kernel
memory, at unmapped pages, at another process's data, or be arithmetic that
overflows. The kernel validates **every** user pointer before touching it
(`uaccess.c`), and the rule is exact:

> The range must lie entirely below `USER_VA_LIMIT` (`0x0000800000000000`, the
> canonical lower half), and every page it spans must be **present and
> user-accessible (`PTE_US`)** in the *caller's* address space. Anything else is
> `-EFAULT`, and nothing is touched.

Two things make this correct rather than approximate. It checks the *caller's*
address space, not "some" address space — the pointer's validity is
per-process. And it checks *every page* the range spans, not just the first byte
— a buffer can start in a mapped page and run off into an unmapped one, and
checking only the start is a classic exploitable bug. The kernel checks the whole
range before the first access, so a malicious length cannot walk it off the end
of valid memory. This is the software half of isolation; paging permissions are
the hardware half. (`SMEP`/`SMAP`, which let the CPU trap kernel access to user
pages, are noted as later additions; until then this validation plus the page
bits are the guarantee.)

## 10.6 The transferable lessons

- **Isolation is the address space.** Per-process page tables with a shared,
  `US`-cleared kernel upper half give you "kernel always mapped, never reachable"
  with no `CR3` tricks — and quietly forbid adding top-level entries post-init.
- **Choose interfaces for where you are going.** Adopting the Linux syscall ABI
  natively costs nothing now and makes the future compatibility layer share one
  vocabulary with native code.
- **Close every window on the boundary.** Interrupts stay masked whenever the CPU
  stands on a user-controlled stack; the full caller-saved set is preserved
  because the ABI promised it; entry and exit are exactly symmetric.
- **Find the shared primitive.** Process launch and fork resumption are one
  `iretq` over a frame, because both are "become this user context."
- **User pointers are adversarial.** Validate the whole range, in the caller's
  space, for presence and user-accessibility, before the first byte — or return
  `-EFAULT` and touch nothing.

The plumbing to run ring-3 code and service its syscalls now exists. The next
chapter uses it to build the Unix process model — loading an ELF, forking,
exec'ing, waiting — and to make a crashing process die alone.

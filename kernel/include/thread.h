/*
 * thread.h - kernel thread control block.
 *
 * Plain C data only (no arch or kernel dependencies) so the scheduler
 * core that manipulates these structures is unit-tested on the host
 * (tests/host/test_sched.c). The arch-specific parts — the saved
 * stack pointer contract and the initial stack frame — are documented
 * in docs/book/appendix-g-scheduling.md and implemented in kernel/sched/ and
 * kernel/arch/x86_64/ctx.asm.
 */
#pragma once

#include <stdint.h>

struct addrspace; /* arch paging (kernel-only field, opaque here) */

enum thread_state {
    THREAD_READY,    /* on the ready queue, waiting for the CPU */
    THREAD_RUNNING,  /* currently executing (at most one thread) */
    THREAD_SLEEPING, /* on the sleep list until wake_tick */
    THREAD_BLOCKED,  /* waiting on an event (currently: thread_join) */
    THREAD_ZOMBIE,   /* exited; TCB and stack persist until joined */
};

struct thread {
    /*
     * Saved kernel stack pointer while not running. Points at the
     * callee-saved register frame described in ctx.asm; meaningless
     * for the running thread.
     */
    uint64_t rsp;

    uint64_t id;
    enum thread_state state;
    const char *name;

    /* Absolute timer tick to wake at (valid while THREAD_SLEEPING). */
    uint64_t wake_tick;

    /* Single waiter blocked in thread_join() on this thread, if any. */
    struct thread *joiner;

    /* Intrusive link: ready queue or sleep list, never both. */
    struct thread *next;

    /* Base of the kmalloc'd kernel stack; NULL for bootstrap threads
     * (the boot thread runs on the boot stack, which is never freed). */
    void *stack_base;

    /* Top of the kernel stack: what TSS.rsp0 and the syscall entry
     * stack are set to while this thread runs ring 3 code. */
    uint64_t kstack_top;

    /* Address space active while this thread runs; NULL means the
     * kernel address space (all pure kernel threads). */
    struct addrspace *as;

    /* Owning user process (proc_core pid), 0 for pure kernel threads.
     * Set before the thread first runs; syscalls key off it. */
    int pid;
};

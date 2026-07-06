/*
 * sched.c - kernel threads and the round-robin scheduler.
 *
 * Policy lives in sched_core.c (host-tested); this file adds what
 * only the kernel can provide: stacks, the context switch, timer
 * integration, and interrupt-off critical sections. Single CPU: all
 * scheduler state is protected by disabling interrupts.
 *
 * Invariants (see docs/scheduling.md):
 *   - schedule() is entered with interrupts disabled, always.
 *   - `current` is never on the ready queue or the sleep list.
 *   - The idle thread never blocks, sleeps, exits, or queues; it is
 *     the fallback when the ready queue is empty.
 *   - A thread appears on at most one list at a time.
 */
#include <sched.h>

#include <stddef.h>

#include <arch/x86_64/cpu.h>
#include <arch/x86_64/ctx.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/paging.h>
#include <kmalloc.h>
#include <panic.h>
#include <sched_core.h>
#include <syscall.h>
#include <timer.h>
#include <vmm.h>

/* 16 KiB kernel stacks. No guard page yet: stacks come from the HHDM
 * where everything is mapped; VMA-based stacks with guards arrive
 * with per-process address spaces (Phase 4). */
#define THREAD_STACK_SIZE (16ULL * 1024)

static struct sched_core core;
static struct thread boot_thread; /* adopts the boot stack in sched_init */
static struct thread *idle_thread;
static struct thread *current;
static struct addrspace *active_as; /* what CR3 currently holds */
static volatile int need_resched;
static int started;
static uint64_t next_id;
static uint64_t nthreads;

/* Boot stack top (entry.asm); thread 0 runs on it. */
extern char kstack_top[];

static void idle_loop(void *arg) {
    (void)arg;
    for (;;) {
        cpu_wait_for_interrupt();
    }
}

/*
 * Pick and switch to the next thread. Interrupts must be disabled.
 * The caller has already placed `current` wherever it belongs (ready
 * queue, sleep list, joiner slot, or nowhere for a zombie).
 */
static void schedule(void) {
    KASSERT(!cpu_interrupts_enabled());
    struct thread *prev = current;
    struct thread *next = sched_core_dequeue(&core);

    if (next == NULL) {
        if (prev->state == THREAD_RUNNING) {
            return; /* nothing else to run; keep going */
        }
        next = idle_thread; /* everyone is blocked or sleeping */
    }
    if (next == prev) {
        prev->state = THREAD_RUNNING;
        return;
    }
    next->state = THREAD_RUNNING;
    current = next;

    /* Ring 3 entry points must land on the incoming thread's kernel
     * stack: interrupts via TSS.rsp0, syscalls via syscall_kstack. */
    gdt_set_rsp0(next->kstack_top);
    syscall_set_kstack(next->kstack_top);

    /* Kernel threads all share the kernel address space; CR3 only
     * moves when a user address space enters or leaves the picture. */
    struct addrspace *as = (next->as != NULL) ? next->as : vmm_kernel_addrspace();
    if (as != active_as) {
        paging_activate(as);
        active_as = as;
    }

    ctx_switch(&prev->rsp, next->rsp);
    /* Now back on `prev`'s stack, some context switches later. */
}

/* Timer tick (interrupt context, interrupts off): wake due sleepers
 * and request a reschedule whenever anyone is ready — the running
 * thread's timeslice is one tick. */
static void sched_timer_tick(uint64_t now) {
    sched_core_wake_due(&core, now);
    if (sched_core_has_ready(&core)) {
        need_resched = 1;
    }
}

void sched_preempt(void) {
    if (!started || !need_resched) {
        return;
    }
    need_resched = 0;
    if (current != idle_thread) {
        sched_core_enqueue(&core, current);
    }
    schedule();
}

static uint64_t stack_top(void *stack_base) {
    return ((uint64_t)stack_base + THREAD_STACK_SIZE) & ~(uint64_t)0xF;
}

struct thread *thread_create(const char *name, void (*entry)(void *), void *arg) {
    struct thread *t = kzalloc(sizeof(*t));
    if (t == NULL) {
        panic("thread_create(%s): out of memory for TCB", name);
    }
    t->stack_base = kmalloc(THREAD_STACK_SIZE);
    if (t->stack_base == NULL) {
        panic("thread_create(%s): out of memory for stack", name);
    }
    t->name = name;

    /*
     * Initial frame, matching ctx.asm's pop order. The trampoline's
     * address sits where a return address would, 8 below the 16-byte-
     * aligned top, so RSP is 16-aligned at the trampoline's call site
     * as the ABI requires.
     */
    uint64_t *sp = (uint64_t *)stack_top(t->stack_base);
    *--sp = (uint64_t)thread_entry_trampoline;
    *--sp = 0;               /* rbp */
    *--sp = 0;               /* rbx */
    *--sp = (uint64_t)entry; /* r12 */
    *--sp = (uint64_t)arg;   /* r13 */
    *--sp = 0;               /* r14 */
    *--sp = 0;               /* r15 */
    t->rsp = (uint64_t)sp;
    t->kstack_top = stack_top(t->stack_base);

    uint64_t flags = cpu_irq_save();
    t->id = next_id++;
    nthreads++;
    sched_core_enqueue(&core, t);
    cpu_irq_restore(flags);
    return t;
}

struct thread *thread_current(void) {
    return current;
}

void sched_set_addrspace(struct addrspace *as) {
    uint64_t flags = cpu_irq_save();
    current->as = as;
    struct addrspace *target = (as != NULL) ? as : vmm_kernel_addrspace();
    if (target != active_as) {
        paging_activate(target);
        active_as = target;
    }
    cpu_irq_restore(flags);
}

void sched_yield(void) {
    uint64_t flags = cpu_irq_save();
    if (current != idle_thread) {
        sched_core_enqueue(&core, current);
    }
    schedule();
    cpu_irq_restore(flags);
}

void sched_sleep_ticks(uint64_t ticks) {
    if (ticks == 0) {
        sched_yield();
        return;
    }
    uint64_t flags = cpu_irq_save();
    KASSERT(current != idle_thread);
    sched_core_sleep(&core, current, timer_ticks() + ticks);
    schedule();
    cpu_irq_restore(flags);
}

void sched_sleep_ms(uint64_t ms) {
    uint64_t hz = timer_hz();
    KASSERT(hz > 0);
    uint64_t ticks = ((ms * hz) + 999) / 1000;
    sched_sleep_ticks(ticks > 0 ? ticks : 1);
}

NORETURN void thread_exit(void) {
    cpu_disable_interrupts();
    struct thread *self = current;
    KASSERT(self != idle_thread);
    self->state = THREAD_ZOMBIE;
    if (self->joiner != NULL) {
        sched_core_enqueue(&core, self->joiner);
        self->joiner = NULL;
    }
    schedule();
    panic("thread %llu (%s) scheduled after exit", (unsigned long long)self->id, self->name);
}

void thread_join(struct thread *t) {
    KASSERT(t != NULL && t != current && t != idle_thread);
    uint64_t flags = cpu_irq_save();
    if (t->state != THREAD_ZOMBIE) {
        KASSERT(t->joiner == NULL); /* one joiner per thread */
        t->joiner = current;
        current->state = THREAD_BLOCKED;
        schedule();
    }
    KASSERT(t->state == THREAD_ZOMBIE);
    nthreads--;
    cpu_irq_restore(flags);

    /* The zombie switched off its stack before our wakeup could run,
     * so both allocations are dead weight now. */
    kfree(t->stack_base);
    kfree(t);
}

uint64_t sched_thread_count(void) {
    return nthreads;
}

void sched_init(void) {
    KASSERT(!started);
    sched_core_init(&core);

    /* Thread 0 is the context executing right now, on the boot stack. */
    boot_thread.name = "main";
    boot_thread.state = THREAD_RUNNING;
    boot_thread.id = next_id++;
    boot_thread.kstack_top = (uint64_t)kstack_top;
    current = &boot_thread;
    active_as = vmm_kernel_addrspace();
    nthreads = 1;

    gdt_set_rsp0(boot_thread.kstack_top);
    syscall_set_kstack(boot_thread.kstack_top);

    /* The idle thread is created like any other but never enqueued:
     * pull it straight back off the ready queue. */
    idle_thread = thread_create("idle", idle_loop, NULL);
    struct thread *dq = sched_core_dequeue(&core);
    KASSERT(dq == idle_thread);
    idle_thread->state = THREAD_READY;

    timer_set_tick_hook(sched_timer_tick);
    started = 1;
}

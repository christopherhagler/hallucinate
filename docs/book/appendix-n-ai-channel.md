# The AI Host Channel

Phase 6 makes the AI a system service: a privileged guest daemon (`aid`)
talks to a helper process on the host, which relays to the Claude API, and
every process reaches it through `/dev/ai`. All of that sits on one thing —
a byte pipe from the guest to the host. This appendix is the contract for
that pipe: `kernel/drivers/aichan.c` behind `kernel/include/aichan.h`.

## Why a real UART, not virtio-serial

The obvious transport is virtio-serial (`virtio-console`), and the previous
slice generalized the virtio-PCI transport to N queues specifically to make
room for it (Appendix I). We did not use it. Virtio-serial is a
paravirtual device: it exists because a hypervisor emulates it. The target
board (Appendix M) has a real 16550 UART on its COM header and no
hypervisor, so a virtio-console driver would work only under QEMU and leave
the AI channel — the whole point of Phase 6 — untested on the one machine
this project exists to run on.

A real 16550 works in both worlds. QEMU emulates the same chip QEMU exposes
to the console (Chapter 4), so the driver is byte-for-byte identical whether
the host peer is a Unix socket under QEMU or a USB-serial adapter plugged
into the board's COM header. The transport is deliberately thin — a byte
pipe, nothing more — so the framing, `/dev/ai`, and `aid` layer on top of it
unchanged, and when Phase 9 brings real networking the bytes move from a
UART to a TCP socket while everything above stays put.

The N-queue transport generalization was not wasted: it is correct
infrastructure that the next multi-queue virtio device (a NIC in Phase 9)
will use. It simply turned out not to be the right tool for *this* job.

## The device

COM2 (I/O port `0x2F8`) by default, so the channel never collides with the
COM1 debug console. The driver is the COM1 console driver's sibling — same
register map, same 115200 8N1 line setup, same loopback self-test — plus the
receive path the console never needs (the console only ever writes). On a
board whose only serial header is COM1, build the console on VGA and point
`AICHAN_PORT` at `0x3F8` instead (Appendix M).

It is **polled**, no interrupts, exactly like the virtio-blk v1 driver
(Chapter 12): every wait is bounded by a spin count or a millisecond
deadline off the timer, so a wedged line or a missing UART degrades quietly
instead of hanging the boot. The loopback self-test in `aichan_init` proves
the chip is present without anything wired to the port — it is internal to
the UART — so `aichan_present()` is true on real hardware even with no cable,
which is correct: the device is there; the peer may not be.

## The contract

| Function | Behaviour |
|---|---|
| `aichan_init()` | Configure the UART, loopback-probe it, print a boot marker either way. |
| `aichan_present()` | Nonzero once the UART is present and initialized. |
| `aichan_write(buf, len)` | Send every byte, polling THRE between bytes. Returns `len`, `-ENODEV` (absent), or `-EIO` (transmitter wedged). |
| `aichan_read(buf, len, timeout_ms)` | Wait up to `timeout_ms` for the first byte, then drain whatever is immediately available. Returns the count (0 on timeout), or `-ENODEV`. |
| `aichan_selftest()` | Boot-time round trip: greet the host, read one line, echo it back. A no-op when the UART is absent; bounded so a boot with no host peer never hangs. |

`aichan_read` returns only what is *immediately* available after the first
byte — it does not wait for a full line. Line assembly is the caller's job;
the selftest accumulates across reads until it sees a newline or a 2-second
budget expires, which is also how the `aid` daemon will frame messages later.

## The ready handshake, and a race worth remembering

The selftest greets the host *first*, then reads and echoes. That ordering
is not cosmetic. `aichan_init` resets the UART FIFO (the `FCR` write) as part
of bring-up, and it runs late in boot — well after the host peer has
connected. If the host sent its challenge immediately on connecting, those
bytes could already be sitting in the receive FIFO when `aichan_init`'s reset
flushed it, and the leading bytes would simply vanish. (This actually
happened during bring-up: the host sent `P247c\n`, the guest echoed `247c\n`,
and the missing `P` was the byte that had reached the FIFO first.)

The fix is a real handshake, and it is the same shape a port-open handshake
always takes: the guest announces the channel is ready (its greeting), and
the host sends nothing until it sees that announcement. By then bring-up is
finished and no further FIFO reset can eat the input. It costs one line each
way and removes the race entirely.

## Testing it: the harness is the host peer

The round trip needs a peer on the other end of the wire, so `run_qemu.py`
becomes one. It gives QEMU a second serial port backed by a Unix-domain
socket (`-chardev socket,…,server=on,wait=off` plus a second `-serial`), then
connects to that socket, waits for the guest's ready greeting, sends a
challenge carrying a random nonce (kept under the 16550's 16-byte FIFO), and
asserts the guest echoes it back verbatim. Both boot configurations — with a
disk and disk-less — run this check, so it is enforced on every commit
through `make check`.

This host-side socket plumbing is not throwaway test scaffolding: it is the
exact precursor to the `aid` bridge. The bridge will connect to the same kind
of endpoint, read framed requests from the guest, call the Claude API, and
write framed responses back. Building it first as a test peer means the
transport is proven end-to-end before a single API call is wired up.

## What sits on top of this next

- **`aid` + `/dev/ai` + `sys_ai_query`.** The daemon frames requests over
  this byte pipe and exposes them to userspace as a device and a syscall.
- **The host bridge.** A Python helper on the Mac that plays the role the
  test peer plays now, but relays to the Claude API instead of echoing.
  Supplying its credentials is a decision left to the project owner, not
  wired up unprompted.
- **The `hsh` shell** with an `ai "…"` builtin — the first userspace
  consumer, and the reason the shell exists at all this early.

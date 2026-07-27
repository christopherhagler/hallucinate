#!/usr/bin/env python3
"""run_qemu - boot the disk image headless and assert on serial output.

Passes when every expected marker appears on the serial console in order.
Fails immediately if a failure pattern ("PANIC", "ERR:") appears, or when the
timeout expires; either way the full serial transcript is printed for
diagnosis.

With --fsck, the filesystem image the guest booted (and wrote to: the block
selftest, the in-kernel fs stress test, init's write-path checks) is verified
by graphfs_fsck after the run — every boot test doubles as an end-to-end
crash-consistency test of the write path.

Without --fsimg, no virtio-blk device is attached at all — the same
disk-less state a real machine is in before an AHCI/NVMe driver exists
(docs/book/appendix-m-real-hardware.md). That is a distinct, still-must-pass
boot path: no panic, devfs still comes up, init is skipped rather than
crashing on a missing /bin/init. A different marker list is expected in this
mode; see PASS_MARKERS_NO_DISK below.
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

PASS_MARKERS_WITH_DISK = [
    "Hallucinate OS",
    "cpu: GDT/TSS loaded",
    "e820:",
    "pmm: ",
    "vmm: kernel page tables active",
    "heap: slab allocator ready",
    "sched: online",
    "syscall: SYSCALL/SYSRET ready",
    "timer: 100 Hz, ticking",
    "pci: ",
    "virtio-blk: ",
    "block: selftest passed",
    "vfs: graphfs root mounted rw",
    "vfs: devfs at /dev",
    "aichan: COM2 UART ready",
    "selftest: sched interleave",
    "selftest: fs write path ok",
    "selftest: passed",
    "aichan: selftest ok",
    "user: launching init (/bin/init from disk",
    "hello from ring 3",
    "hello from execve",
    "trap: user fault: #PF page fault",
    "killed by signal 11",
    "trap: user fault: #UD invalid opcode",
    "killed by signal 4",
    "user: console open via /dev/console ok",
    "user: C init: .data .bss .rodata ok",
    "user: init exited (status 0)",
    "boot: complete",
]

# No virtio-blk device attached at all — the state a real machine is in
# before an AHCI/NVMe driver exists. Every layer must degrade instead of
# panicking: virtio_blk_init finds nothing, block_selftest and vfs_init and
# the fs selftest and process_run_init all skip their disk-dependent work
# and say so, and the kernel still reaches the interactive keyboard loop.
PASS_MARKERS_NO_DISK = [
    "Hallucinate OS",
    "cpu: GDT/TSS loaded",
    "e820:",
    "pmm: ",
    "vmm: kernel page tables active",
    "heap: slab allocator ready",
    "sched: online",
    "syscall: SYSCALL/SYSRET ready",
    "timer: 100 Hz, ticking",
    "pci: ",
    "virtio-blk: no device",
    "block: selftest skipped (no device)",
    "vfs: no block device found",
    "vfs: devfs at /dev",
    "aichan: COM2 UART ready",
    "selftest: sched interleave",
    "selftest: fs write-path test skipped (no root filesystem)",
    "selftest: passed",
    "aichan: selftest ok",
    "process: no root filesystem, skipping init",
    "boot: complete",
]

FAIL_PATTERNS = [
    "PANIC",
    "ERR:",
]


class AichanPeer:
    """Host end of the guest's AI channel (COM2), exposed by QEMU as a Unix
    socket. Stands in for the future `aid` bridge: wait for the guest to
    announce the port is ready, send a challenge, and confirm the guest
    echoes it back — a real end-to-end round trip over the serial line, the
    same plumbing the Claude API bridge will use.

    The ready handshake matters: the challenge is sent only *after* the
    guest's greeting arrives, so it can't land in the receive FIFO before
    aichan_init's FIFO reset and get flushed. The challenge carries a random
    nonce and stays under the 16550's 16-byte FIFO.
    """

    READY = b"hello from guest\n"

    def __init__(self, sock_path: str) -> None:
        self.sock_path = sock_path
        self.challenge = b"P" + os.urandom(2).hex().encode() + b"\n"
        self.ok = False
        self._thread = threading.Thread(target=self._run, daemon=True)

    def start(self) -> None:
        self._thread.start()

    def _recv_until(self, conn: "socket.socket", needle: bytes,
                    buf: bytearray, end: float) -> bool:
        """Read from conn into buf until needle appears or the deadline hits."""
        while time.monotonic() < end:
            if needle in buf:
                return True
            try:
                chunk = conn.recv(256)
            except socket.timeout:
                continue
            except OSError:
                return False
            if not chunk:
                return needle in buf
            buf.extend(chunk)
        return needle in buf

    def _run(self) -> None:
        # QEMU (server=on,wait=off) creates the listening socket during
        # startup; retry until it is there.
        conn: "socket.socket | None" = None
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline and conn is None:
            try:
                conn = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                conn.connect(self.sock_path)
            except OSError:
                if conn is not None:
                    conn.close()
                conn = None
                time.sleep(0.05)
        if conn is None:
            return

        with conn:
            conn.settimeout(0.5)
            buf = bytearray()
            end = time.monotonic() + 25.0
            # Wait for the guest to announce readiness before challenging it.
            if not self._recv_until(conn, self.READY, buf, end):
                sys.stderr.write("[aichan] guest never announced ready\n")
                return
            conn.sendall(self.challenge)
            if self._recv_until(conn, self.challenge, buf, end):
                self.ok = True
                return
        sys.stderr.write(
            f"[aichan] sent={self.challenge!r} received={bytes(buf)!r}\n")

    def join(self, timeout: float) -> None:
        self._thread.join(timeout=timeout)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True)
    ap.add_argument("--fsimg", help="filesystem image, attached as virtio-blk")
    ap.add_argument("--fsck", help="graphfs_fsck binary: verify the written fs image post-boot")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    args = ap.parse_args()

    PASS_MARKERS = PASS_MARKERS_WITH_DISK if args.fsimg else PASS_MARKERS_NO_DISK

    # COM2 is the guest's AI host channel; expose it as a Unix socket and
    # act as its peer for the round-trip selftest. The path is kept short
    # (macOS caps AF_UNIX paths near 104 bytes, and $TMPDIR is long there).
    aichan_dir = tempfile.mkdtemp(prefix="aichan-", dir="/tmp")
    aichan_sock = os.path.join(aichan_dir, "com2.sock")

    cmd = [
        args.qemu,
        "-m", "256M",
        "-drive", f"file={args.image},format=raw",
        "-serial", "stdio",  # COM1: debug console
        # COM2: AI channel. Order matters — this is the second -serial.
        "-chardev", f"socket,id=aichan,path={aichan_sock},server=on,wait=off",
        "-serial", "chardev:aichan",
        "-display", "none",
        "-monitor", "none",
        "-no-reboot",
    ]

    # The guest writes to the fs disk (block selftest, later the
    # filesystem), so boot a throwaway copy and keep the build
    # artifact pristine.
    fs_copy = None
    if args.fsimg:
        fs_copy = tempfile.NamedTemporaryFile(suffix=".img", delete=False)
        fs_copy.close()
        shutil.copyfile(args.fsimg, fs_copy.name)
        cmd += [
            "-drive", f"file={fs_copy.name},format=raw,if=none,id=fsdisk",
            "-device", "virtio-blk-pci,drive=fsdisk,disable-legacy=on",
        ]

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    aichan = AichanPeer(aichan_sock)
    aichan.start()

    transcript = bytearray()
    lock = threading.Lock()

    def reader() -> None:
        assert proc.stdout is not None
        while True:
            # read1 returns as soon as any bytes are available; a plain
            # read(n) would block until exactly n bytes arrive.
            chunk = proc.stdout.read1(256)
            if not chunk:
                return
            with lock:
                transcript.extend(chunk)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    deadline = time.monotonic() + args.timeout
    next_marker = 0
    result: "str | None" = None

    while time.monotonic() < deadline and result is None:
        time.sleep(0.05)
        with lock:
            text = transcript.decode("utf-8", errors="replace")
        for pattern in FAIL_PATTERNS:
            if pattern in text:
                result = f"failure pattern {pattern!r} in output"
                break
        if result is not None:
            break
        while next_marker < len(PASS_MARKERS) and PASS_MARKERS[next_marker] in text:
            next_marker += 1
        if next_marker == len(PASS_MARKERS):
            result = "pass"
        if proc.poll() is not None:
            # QEMU exited before all markers appeared.
            time.sleep(0.2)
            if result != "pass":
                result = f"qemu exited early (status {proc.returncode})"

    # The echo and its COM1 marker are emitted back to back; give the peer
    # a moment to observe the echo over the socket before tearing QEMU down.
    if result == "pass":
        aichan.join(3.0)

    proc.kill()
    proc.wait()
    t.join(timeout=2)

    # The AI channel is a required part of the boot: the guest must have
    # echoed the host's challenge back over COM2.
    if result == "pass" and not aichan.ok:
        result = "aichan round-trip failed (guest did not echo the host challenge)"

    # The guest wrote to its disk all boot long; the image it leaves
    # behind must still be a perfectly consistent filesystem.
    if result == "pass" and args.fsck and fs_copy is not None:
        fsck = subprocess.run(
            [args.fsck, fs_copy.name],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        if fsck.returncode != 0:
            result = "post-boot fsck failed:\n" + fsck.stdout
        else:
            print("post-boot fsck: clean")

    if fs_copy is not None:
        os.unlink(fs_copy.name)
    shutil.rmtree(aichan_dir, ignore_errors=True)

    with lock:
        text = transcript.decode("utf-8", errors="replace")

    if result == "pass":
        print(f"boot test: PASS ({len(PASS_MARKERS)} markers, AI channel round-trip ok)")
        return 0

    print("boot test: FAIL")
    if result is None:
        print(f"  timed out after {args.timeout:.0f}s")
    else:
        print(f"  {result}")
    print(f"  markers seen: {next_marker}/{len(PASS_MARKERS)} "
          f"(next expected: {PASS_MARKERS[next_marker] if next_marker < len(PASS_MARKERS) else '-'!r})")
    print("---- serial transcript ----")
    print(text if text else "(no output)")
    print("---------------------------")
    return 1


if __name__ == "__main__":
    sys.exit(main())

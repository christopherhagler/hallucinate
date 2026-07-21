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
    "selftest: sched interleave",
    "selftest: fs write path ok",
    "selftest: passed",
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
    "selftest: sched interleave",
    "selftest: fs write-path test skipped (no root filesystem)",
    "selftest: passed",
    "process: no root filesystem, skipping init",
    "boot: complete",
]

FAIL_PATTERNS = [
    "PANIC",
    "ERR:",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True)
    ap.add_argument("--fsimg", help="filesystem image, attached as virtio-blk")
    ap.add_argument("--fsck", help="graphfs_fsck binary: verify the written fs image post-boot")
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    args = ap.parse_args()

    PASS_MARKERS = PASS_MARKERS_WITH_DISK if args.fsimg else PASS_MARKERS_NO_DISK

    cmd = [
        args.qemu,
        "-m", "256M",
        "-drive", f"file={args.image},format=raw",
        "-serial", "stdio",
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

    proc.kill()
    proc.wait()
    t.join(timeout=2)

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

    with lock:
        text = transcript.decode("utf-8", errors="replace")

    if result == "pass":
        print(f"boot test: PASS ({len(PASS_MARKERS)} markers)")
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

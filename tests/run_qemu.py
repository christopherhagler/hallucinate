#!/usr/bin/env python3
"""run_qemu - boot the disk image headless and assert on serial output.

Passes when every expected marker appears on the serial console in order.
Fails immediately if a failure pattern ("PANIC", "ERR:") appears, or when the
timeout expires; either way the full serial transcript is printed for
diagnosis.
"""

import argparse
import subprocess
import sys
import threading
import time

PASS_MARKERS = [
    "Hallucinate OS",
    "cpu: GDT/TSS loaded",
    "e820:",
    "pmm: ",
    "timer: 100 Hz, ticking",
    "selftest: passed",
    "boot: complete",
]

FAIL_PATTERNS = [
    "PANIC",
    "ERR:",
]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--image", required=True)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    args = ap.parse_args()

    cmd = [
        args.qemu,
        "-m", "256M",
        "-drive", f"file={args.image},format=raw",
        "-serial", "stdio",
        "-display", "none",
        "-monitor", "none",
        "-no-reboot",
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

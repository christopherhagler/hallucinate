#!/usr/bin/env bash
#
# flash_usb.sh - write build/disk.img to a USB stick for a real-hardware
# boot test.
#
# disk.img (tools/mkimage.py) is already a raw, MBR-formatted disk image:
# stage 1 boot sector at LBA 0, stage 2 and the kernel ELF after it, padded
# to a whole MiB. Writing it byte-for-byte to a USB drive's raw block
# device makes that drive BIOS-bootable exactly the way build/disk.img is
# QEMU-bootable. This script only automates the parts that are tedious and
# easy to get subtly wrong by hand (finding the raw device node, confirming
# it, unmounting first); it refuses to guess a target device for you.
#
# Usage: tools/flash_usb.sh /dev/diskN   (macOS; run: diskutil list)
#
# Safety: this script writes directly to a block device with dd. Getting
# the device argument wrong can overwrite ANY disk visible to the machine,
# including the one macOS is running from. It will not proceed without an
# explicit device argument, and it will not proceed past the confirmation
# prompt unless you retype the exact device path back.

set -euo pipefail

IMG="${IMG:-build/disk.img}"

die() {
    echo "flash_usb: error: $*" >&2
    exit 1
}

if [[ "$(uname -s)" != "Darwin" ]]; then
    die "this script only knows macOS's diskutil; on Linux, use lsblk + dd" \
        "by hand (see docs/book/appendix-m-real-hardware.md)"
fi

if [[ $# -ne 1 ]]; then
    die "usage: $0 /dev/diskN
  1. Insert the USB stick.
  2. Run 'diskutil list' and find its /dev/diskN (NOT a partition like diskNsM).
  3. Re-run: $0 /dev/diskN"
fi

DEV="$1"
[[ "$DEV" =~ ^/dev/disk[0-9]+$ ]] || die "'$DEV' doesn't look like a whole-disk node" \
    "(/dev/diskN, not /dev/diskNsM or /dev/rdiskN — this script adds the r itself)"

[[ -f "$IMG" ]] || die "$IMG not found — build it first: make"

BOOT_DEV="$(diskutil info -plist / | plutil -extract ParentWholeDisk raw - 2>/dev/null || true)"
if [[ -n "$BOOT_DEV" && "$DEV" == "/dev/$BOOT_DEV" ]]; then
    die "$DEV is the disk macOS is booted from — refusing to touch it"
fi

echo "About to overwrite $DEV with $IMG:"
echo
diskutil info "$DEV" | sed -n '/Device Node/p;/Media Name/p;/Volume Name/p;/Disk Size/p;/Protocol/p'
echo
echo "Every partition and every byte on $DEV will be destroyed."
read -rp "Type the device path ($DEV) again to confirm: " CONFIRM
[[ "$CONFIRM" == "$DEV" ]] || die "confirmation did not match — aborted, nothing written"

echo "flash_usb: unmounting $DEV"
diskutil unmountDisk "$DEV"

RDEV="/dev/r${DEV#/dev/}"
echo "flash_usb: writing $IMG to $RDEV (raw device — faster than the buffered /dev/diskN node)"
echo "flash_usb: press Ctrl+T at any time for a progress report (BSD dd/SIGINFO)"
sudo dd if="$IMG" of="$RDEV" bs=1m

echo "flash_usb: done, ejecting"
diskutil eject "$DEV"

echo
echo "flash_usb: $DEV is ready. See docs/book/appendix-m-real-hardware.md for BIOS boot steps."

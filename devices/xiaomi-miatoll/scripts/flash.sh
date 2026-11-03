#!/bin/bash
# Write ztOS to a Redmi Note 9 Pro / 9S (miatoll) in fastboot (power off,
# then Volume Down + Power), from `installer/ztos build --target miatoll`:
#
#   CACHE     the boot partition: systemd-boot, the kernel, zboot (FAT32)
#   USERDATA  the system (ext4, ZR_ARCH), as a sparse image
#   DTBO      erased (U-Boot gives the kernel its device tree)
#   BOOT      U-Boot (sm7125-mainline's release), last, as postmarketOS does
#
#   devices/xiaomi-miatoll/scripts/flash.sh [--boot-only]
#
# --boot-only writes CACHE alone: a new kernel or boot stage, the system and
# everything on it kept (its modules must already be on it).
# This erases USERDATA, everything on the phone, unless --boot-only.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
W=$ROOT/build/ztos/miatoll
BOOTPART=$W/stage/boot/ztos-miatoll-bootpart.img
ROOTIMG=$W/root.img
UBOOT_URL=https://github.com/sm7125-mainline/u-boot/releases/download/2025-06-02/u-boot-sm7125.img
UBOOT_SHA256=6878802d0ee3c893e2a12213bca4be05da916be5dea8a2d895a803cb7f255b49
UBOOT=$ROOT/build/u-boot-sm7125-2025-06-02.img
ONLY_BOOT=
[ "${1:-}" = --boot-only ] && ONLY_BOOT=1

[ -f "$BOOTPART" ] || BOOTPART=$ROOT/out/ztos-miatoll-bootpart.img
[ -f "$BOOTPART" ] || { echo "no boot partition image: run installer/ztos build --target miatoll" >&2; exit 1; }
if [ -z "$ONLY_BOOT" ]; then
	[ -f "$ROOTIMG" ] || { echo "no $ROOTIMG: run installer/ztos build --target miatoll" >&2; exit 1; }
	if [ ! -f "$UBOOT" ] || ! echo "$UBOOT_SHA256  $UBOOT" | sha256sum -c --status; then
		curl -L --fail -o "$UBOOT.part" "$UBOOT_URL"
		echo "$UBOOT_SHA256  $UBOOT.part" | sha256sum -c --quiet
		mv "$UBOOT.part" "$UBOOT"
	fi
	SPARSE=$W/root.sparse.img
	if [ ! -f "$SPARSE" ] || [ "$SPARSE" -ot "$ROOTIMG" ]; then
		echo "making the sparse image of $ROOTIMG"
		img2simg "$ROOTIMG" "$SPARSE.part"
		mv "$SPARSE.part" "$SPARSE"
	fi
fi

fastboot devices | grep -q fastboot || { echo "no phone in fastboot: power off, then hold Volume Down + Power" >&2; exit 1; }
product=$(fastboot getvar product 2>&1 | sed -n 's/^product: *//p')
case "$product" in
joyeuse|curtana|excalibur|gram) ;;
*) echo "this is not a miatoll phone (product: $product)" >&2; exit 1 ;;
esac
# at a low charge the bootloader refuses some commands (erase), halfway through
if [ "$(fastboot getvar battery-soc-ok 2>&1 | sed -n 's/^battery-soc-ok: *//p')" != yes ]; then
	echo "the battery is too low for the bootloader: charge the phone powered off first" >&2
	exit 1
fi

if [ -n "$ONLY_BOOT" ]; then
	fastboot flash cache "$BOOTPART"
	fastboot reboot
	exit 0
fi
echo "This writes ztOS to the $product in fastboot and erases everything on it (USERDATA)."
read -r -p "Type the word erase to go on: " a
[ "$a" = erase ] || { echo "nothing written"; exit 1; }
fastboot flash cache "$BOOTPART"
fastboot flash userdata "$SPARSE"
fastboot erase dtbo
fastboot flash boot "$UBOOT"
fastboot reboot
echo "Done: the first boot grows the system to the whole of USERDATA, then shows the lock screen."

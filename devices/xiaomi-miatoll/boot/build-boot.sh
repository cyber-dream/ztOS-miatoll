#!/bin/bash
# The Redmi Note 9 Pro / 9S (miatoll)'s boot partition, for CACHE: a FAT32
# filesystem labelled pmOS_boot (the name U-Boot's boot order looks for)
# with systemd-boot as the EFI fallback loader, the kernel (an EFI zboot
# image), ztOS's boot stage (../initramfs/init) and one loader entry. The
# device tree comes from U-Boot in front of it (BOOT), as with postmarketOS.
#
#   devices/xiaomi-miatoll/boot/build-boot.sh [--sysroot <aarch64 root>] [--cmdline "..."] [--name N]
#
# --sysroot gives systemd-boot (usr/lib/systemd/boot/efi/systemd-bootaa64.efi)
# from the ztOS root being built; default: the GSI's build tree. The boot
# stage's busybox is Arch Linux ARM's (static), fetched once.
#
# Needs: build-kernel.sh run first; mkfs.vfat, mtools, cpio. No root.
# Out: out/<name>.img (+ .sha256).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
DEV=$(dirname "$HERE")
ROOT=$(cd "$DEV/../.." && pwd)
KTAG=sm7125-6.14.7-r2
KOUT=$ROOT/build/linux-$KTAG/out
NAME=ztos-miatoll-bootpart
SYSROOT=$ROOT/build/ztos/gsi/root
CMDLINE="loglevel=4"
BUSYBOX_PKG=busybox-1.36.1-4-aarch64.pkg.tar.xz
BUSYBOX_URL=http://mirror.archlinuxarm.org/aarch64/extra/$BUSYBOX_PKG
while [ $# -gt 0 ]; do
	case $1 in
	--sysroot) SYSROOT=$2; shift ;;
	--cmdline) CMDLINE="$CMDLINE $2"; shift ;;
	--name) NAME=$2; shift ;;
	*) echo "unknown option $1" >&2; exit 2 ;;
	esac
	shift
done
EFI=$SYSROOT/usr/lib/systemd/boot/efi/systemd-bootaa64.efi
KERNEL=$KOUT/arch/arm64/boot/vmlinuz.efi
for f in "$KERNEL" "$KOUT/kernel.release" "$EFI"; do
	[ -f "$f" ] || { echo "boot: no $f (build-kernel.sh first; --sysroot a root with systemd)" >&2; exit 1; }
done
W=$ROOT/build/miatoll-boot
mkdir -p "$W" "$ROOT/out"

# --- busybox (Arch Linux ARM's, statically linked) -------------------------------
BB=$ROOT/build/miatoll-busybox/usr/bin/busybox
if [ ! -f "$BB" ]; then
	mkdir -p "$ROOT/build/miatoll-busybox"
	curl -L --fail -o "$ROOT/build/miatoll-busybox/$BUSYBOX_PKG" "$BUSYBOX_URL"
	tar -xf "$ROOT/build/miatoll-busybox/$BUSYBOX_PKG" -C "$ROOT/build/miatoll-busybox" usr/bin/busybox
fi
file -L "$BB" | grep -q "ARM aarch64.*statically linked" || { echo "boot: $BB is not a static aarch64 busybox" >&2; exit 1; }

# --- the boot stage (initramfs) --------------------------------------------------
I=$W/initramfs
rm -rf "$I"
mkdir -p "$I"/{bin,sbin,dev,proc,sys,run,tmp,newroot,etc,usr/lib,usr/bin,usr/sbin}
ln -s usr/lib "$I/lib"
install -m755 "$BB" "$I/bin/busybox"
ln -s busybox "$I/bin/sh"
install -m755 "$DEV/initramfs/init" "$I/init"
# the touchscreens' firmware: their drivers are built in and ask for it
# before the root is mounted (postmarketOS's initramfs carries it too)
FWDIR=$ROOT/build/miatoll-firmware/usr/lib/firmware
[ -f "$FWDIR/focaltech_ts_fw_huaxing.bin" ] || sh "$DEV/firmware/fetch.sh"
install -d "$I/usr/lib/firmware"
install -m644 "$FWDIR/focaltech_ts_fw_huaxing.bin" "$FWDIR/novatek_ts_tianma_fw.bin" "$I/usr/lib/firmware/"
# every applet /init calls must be in this busybox (run through qemu-user,
# when there is one: no root needed, it is static)
QEMU=$(command -v qemu-aarch64-static || command -v qemu-aarch64 || true)
if [ -n "$QEMU" ]; then
	have=$("$QEMU" "$BB" --list)
	for a in sh mount umount mkdir sed cut awk cat ls head tail grep sleep ifconfig ip udhcpd telnetd \
			setsid findfs killall kill rmdir rm ln cp dmesg switch_root touch tr echo sync; do
		echo "$have" | grep -qx "$a" || { echo "boot: busybox has no $a applet, which /init needs" >&2; exit 1; }
	done
	echo "boot: busybox has every applet /init needs"
fi
(cd "$I" && find . | LC_ALL=C sort | cpio --quiet -o -H newc -R 0:0 | gzip -9n) > "$W/initramfs.gz"

# --- the FAT32 partition ---------------------------------------------------------
# UFS has 4 KiB sectors, and FAT32 wants 65525 clusters at least: 272 MiB
IMG=$ROOT/out/$NAME.img
rm -f "$IMG"
truncate -s 272M "$IMG"
mkfs.vfat -F 32 -S 4096 -s 1 -n pmOS_boot "$IMG" > /dev/null
mmd -i "$IMG" ::EFI ::EFI/BOOT ::loader ::loader/entries
mcopy -i "$IMG" "$EFI" ::EFI/BOOT/BOOTAA64.EFI
mcopy -i "$IMG" "$KERNEL" ::vmlinuz
mcopy -i "$IMG" "$W/initramfs.gz" ::initramfs
printf 'default ztos.conf\ntimeout 0\n' > "$W/loader.conf"
printf 'title ztOS\nlinux /vmlinuz\ninitrd /initramfs\noptions %s\n' "$CMDLINE" > "$W/ztos.conf"
mcopy -i "$IMG" "$W/loader.conf" ::loader/loader.conf
mcopy -i "$IMG" "$W/ztos.conf" ::loader/entries/ztos.conf
sha256sum "$IMG" | sed "s|  .*/|  |" > "$IMG.sha256"
echo "boot: $IMG (kernel $(cat "$KOUT/kernel.release"), cmdline: $CMDLINE)"

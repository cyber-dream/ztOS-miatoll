#!/bin/bash
# The Redmi Note 9 Pro / 9S (miatoll)'s boot partition, for CACHE: a FAT32
# filesystem labelled pmOS_boot (the name U-Boot's boot order looks for)
# with systemd-boot as the EFI fallback loader, the kernel (an EFI zboot
# image), ztOS's boot stage (../initramfs/init), the device tree and one
# loader entry.
#
#   devices/xiaomi-miatoll/boot/build-boot.sh [--variant V] [--sysroot <aarch64 root>] [--cmdline "..."] [--name N]
#
# The device tree is U-Boot's own for the variant (the release on BOOT carries
# one per phone and panel: joyeuse-huaxing, joyeuse-tianma, curtana-huaxing,
# curtana-tianma), with usb-c-sink.dtso applied: U-Boot's has the USB-C port
# as a source only, and the PMIC then charges at 500 mA whatever the charger
# (see that file). systemd-boot applies an overlay only to a tree its entry
# names, so the merged tree is the entry's `devicetree`; U-Boot's EFI
# devicetree fixup still fills in the memory. Default variant: joyeuse-huaxing,
# or the word in build/miatoll-variant.
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
VARIANT=$(cat "$ROOT/build/miatoll-variant" 2>/dev/null || echo joyeuse-huaxing)
UBOOT_URL=https://github.com/sm7125-mainline/u-boot/releases/download/2025-06-02/u-boot-sm7125.img
UBOOT_SHA256=6878802d0ee3c893e2a12213bca4be05da916be5dea8a2d895a803cb7f255b49
UBOOT=$ROOT/build/u-boot-sm7125-2025-06-02.img
BUSYBOX_PKG=busybox-1.36.1-4-aarch64.pkg.tar.xz
BUSYBOX_URL=http://mirror.archlinuxarm.org/aarch64/extra/$BUSYBOX_PKG
while [ $# -gt 0 ]; do
	case $1 in
	--variant) VARIANT=$2; shift ;;
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

# --- the device tree: U-Boot's for the variant, the USB-C port a sink too --------
case $VARIANT in
joyeuse-huaxing) MODEL="Xiaomi Redmi Note 9 Pro (Global, Huaxing)" ;;
joyeuse-tianma) MODEL="Xiaomi Redmi Note 9 Pro (Global, Tianma)" ;;
curtana-huaxing) MODEL="Xiaomi Redmi Note 9S (Huaxing)" ;;
curtana-tianma) MODEL="Xiaomi Redmi Note 9S (Tianma)" ;;
*) echo "boot: unknown variant $VARIANT (joyeuse-huaxing, joyeuse-tianma, curtana-huaxing, curtana-tianma)" >&2; exit 2 ;;
esac
if [ ! -f "$UBOOT" ] || ! echo "$UBOOT_SHA256  $UBOOT" | sha256sum -c --status; then
	curl -L --fail -o "$UBOOT.part" "$UBOOT_URL"
	echo "$UBOOT_SHA256  $UBOOT.part" | sha256sum -c --quiet
	mv "$UBOOT.part" "$UBOOT"
fi
DTC=$KOUT/scripts/dtc/dtc
rm -rf "$W/uboot-dtbs"
mkdir -p "$W/uboot-dtbs"
# every flattened tree appended to U-Boot (FDT magic, then its size)
python3 - "$UBOOT" "$W/uboot-dtbs" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
i, n = 0, 0
while (i := d.find(b'\xd0\x0d\xfe\xed', i)) >= 0:
    size = struct.unpack('>I', d[i + 4:i + 8])[0]
    if 4096 < size < 2 << 20 and i + size <= len(d):
        n += 1
        open(f'{sys.argv[2]}/{n}.dtb', 'wb').write(d[i:i + size])
    i += 4
PY
UDTB=
for f in "$W"/uboot-dtbs/*.dtb; do
	[ "$(fdtget "$f" / model 2>/dev/null)" = "$MODEL" ] && UDTB=$f
done
[ -n "$UDTB" ] || { echo "boot: U-Boot has no device tree for \"$MODEL\"" >&2; exit 1; }
"$DTC" -q -@ -I dts -O dtb -o "$W/usb-c-sink.dtbo" "$HERE/usb-c-sink.dtso"
fdtoverlay -i "$UDTB" -o "$W/miatoll.dtb" "$W/usb-c-sink.dtbo"
[ "$(fdtget "$W/miatoll.dtb" /soc@0/spmi@c440000/pmic@0/typec@1500/connector power-role)" = dual ] ||
	{ echo "boot: the USB-C overlay did not apply" >&2; exit 1; }
echo "boot: device tree $MODEL, USB-C port dual-role"

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
mcopy -i "$IMG" "$W/miatoll.dtb" ::miatoll.dtb
printf 'default ztos.conf\ntimeout 0\n' > "$W/loader.conf"
printf 'title ztOS\nlinux /vmlinuz\ninitrd /initramfs\ndevicetree /miatoll.dtb\noptions %s\n' "$CMDLINE" > "$W/ztos.conf"
mcopy -i "$IMG" "$W/loader.conf" ::loader/loader.conf
mcopy -i "$IMG" "$W/ztos.conf" ::loader/entries/ztos.conf
sha256sum "$IMG" | sed "s|  .*/|  |" > "$IMG.sha256"
echo "boot: $IMG (kernel $(cat "$KOUT/kernel.release"), cmdline: $CMDLINE)"

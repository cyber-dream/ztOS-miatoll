#!/bin/bash
# Save the Redmi Note 9 Pro / 9S (miatoll)'s own partitions, the ones that
# cannot be had back from any download, to backups/miatoll/stock-DATE/ on this
# computer, from a running ztOS (or postmarketOS) over SSH:
#
#   persist          sensor and camera calibration, the Wi-Fi and Bluetooth addresses
#   modemst1, modemst2, fsg, fsc
#                    the modem's file system: IMEI, network calibration
#   devinfo          the bootloader's unlock state
#
#   devices/xiaomi-miatoll/scripts/backup.sh [user@host]     (default alarm@172.16.42.1, the USB link)
#
# ztOS never writes any of these (flash.sh writes CACHE, USERDATA, DTBO and
# BOOT only), so this is insurance: a phone whose modem partitions are lost
# has no IMEI. Each partition is read twice and the two compared. Xiaomi's
# fastboot cannot read partitions, which is why this runs from the system.
# The stock BOOT and DTBO come back from Xiaomi's fastboot ROM for the phone,
# not from here.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
HOST=${1:-alarm@172.16.42.1}
PARTS="persist modemst1 modemst2 fsg fsc devinfo"
OUT=$ROOT/backups/miatoll/stock-$(date +%Y%m%d-%H%M%S)
SSH=(ssh -o ConnectTimeout=10 -o LogLevel=ERROR "$HOST")

"${SSH[@]}" -n true || { echo "cannot reach $HOST over SSH: is ztOS running, the cable in (or give user@address)?" >&2; exit 1; }
"${SSH[@]}" -n 'test -e /dev/disk/by-partlabel/modemst1' ||
	{ echo "$HOST has no modemst1 partition: is it a miatoll phone?" >&2; exit 1; }
read -r -s -p "The phone's password (for sudo there): " PW
echo
printf '%s\n' "$PW" | "${SSH[@]}" 'sudo -S -p "" true' 2>/dev/null || { echo "sudo refused that password" >&2; exit 1; }

mkdir -p "$OUT"
for p in $PARTS; do
	for n in 1 2; do
		printf '%s\n' "$PW" | "${SSH[@]}" "sudo -S -p '' cat /dev/disk/by-partlabel/$p" > "$OUT/$p.img.$n"
	done
	if ! cmp -s "$OUT/$p.img.1" "$OUT/$p.img.2"; then
		echo "$p: the two reads differ; nothing kept for it, run this again" >&2
		rm -f "$OUT/$p.img.1" "$OUT/$p.img.2"
		exit 1
	fi
	[ -s "$OUT/$p.img.1" ] || { echo "$p: read nothing" >&2; exit 1; }
	mv "$OUT/$p.img.1" "$OUT/$p.img"
	rm "$OUT/$p.img.2"
	echo "$p: $(stat -c %s "$OUT/$p.img") bytes"
done
(cd "$OUT" && sha256sum ./*.img > SHA256SUMS)
chmod -R go-rwx "$OUT"
echo "saved to $OUT (backups/ stays out of git): keep a copy somewhere safe"
echo "to put one back, from a running ztOS: sudo dd if=<name>.img of=/dev/disk/by-partlabel/<name>"

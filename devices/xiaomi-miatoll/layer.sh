# The Redmi Note 9 Pro / 9S (miatoll: joyeuse, curtana) device layer, run by
# the installer inside the build namespace after the GSI part, with $R (the
# root), $DEVSRC (this folder), $DEVROOT (the source tree), $SRC, $P, step and
# ch (see the S9's layer.sh). No postmarketOS: the kernel is built from
# sm7125-mainline (kernel/build-kernel.sh), the firmware comes from
# sm7125-mainline/firmware-xiaomi-miatoll, the boot stage is ztOS's zboot.
D=$DEVSRC
KTAG=sm7125-6.14.7-r2
KOUT=$DEVROOT/build/linux-$KTAG/out

step "adding the Redmi Note 9 Pro device layer"
[ -f "$KOUT/kernel.release" ] || {
	echo "no kernel: run devices/xiaomi-miatoll/kernel/build-kernel.sh first" >&2
	exit 1
}
KREL=$(cat "$KOUT/kernel.release")

# ztOSIAL: the manifest, the sensor framework's components, the vendor units
install -d "$R/vendor/ztos"
cp -a "$D/vendor/ztos/." "$R/vendor/ztos/"
chown -R 0:0 "$R/vendor"

cp -a "$D/overlay/." "$R/"
chown -R 0:0 "$R/etc/fstab"

# --- the kernel: ztOS's own, so no kernel package ----------------------------------
if ch pacman -Qq linux-aarch64 > /dev/null 2>&1; then
	ch pacman --noconfirm -Rdd linux-aarch64 > /dev/null
fi
rm -rf "$R/usr/lib/modules/$KREL"
install -d "$R/usr/lib/modules"
cp -a "$KOUT/modroot/usr/lib/modules/$KREL" "$R/usr/lib/modules/"
chown -R 0:0 "$R/usr/lib/modules/$KREL"
ch depmod -a "$KREL"

# --- the phone's own firmware (DSPs, Wi-Fi, zap shader, touchscreens) ---------------
sh "$D/firmware/fetch.sh" "$DEVROOT/build/miatoll-firmware"
cp -a "$DEVROOT/build/miatoll-firmware/usr/lib/firmware/." "$R/usr/lib/firmware/"
chown -R 0:0 "$R/usr/lib/firmware"

# --- pd-mapper and tqftpserv (not in Arch Linux ARM), built here --------------------
# (qrtr and rmtfs are packages.txt's; ztConflo's qcom_services did the same)
step "building the DSP services (pd-mapper, tqftpserv)"
QS=$DEVROOT/build/miatoll-userspace
mkdir -p "$QS"
for s in "pd-mapper https://github.com/linux-msm/pd-mapper.git v1.1" \
		"tqftpserv https://github.com/linux-msm/tqftpserv.git v1.3"; do
	set -- $s
	[ -d "$QS/$1-$3" ] || git clone -q --depth 1 --branch "$3" "$2" "$QS/$1-$3"
done
rm -rf "$R/usr/src/ztos-qcom"
install -d "$R/usr/src/ztos-qcom"
cp -r "$QS/pd-mapper-v1.1" "$R/usr/src/ztos-qcom/pd-mapper"
cp -r "$QS/tqftpserv-v1.3" "$R/usr/src/ztos-qcom/tqftpserv"
ch sh -c 'set -eu
cd /usr/src/ztos-qcom
make -C pd-mapper prefix=/usr install
cd tqftpserv
cc -O2 -Wall -DHAVE_XZ -DHAVE_ZSTD -o tqftpserv translate.c tqftpserv.c xz-decompress.c zstd-decompress.c \
	-lqrtr -llzma -lzstd
install -Dm755 tqftpserv /usr/bin/tqftpserv
sed "s+@prefix@+/usr+g" tqftpserv.service.in > /usr/lib/systemd/system/tqftpserv.service
chmod 644 /usr/lib/systemd/system/tqftpserv.service'
rm -rf "$R/usr/src/ztos-qcom"

# --- the boot partition (CACHE): systemd-boot, the kernel, zboot --------------------
step "making the boot partition (kernel $KREL, boot stage)"
bash "$D/boot/build-boot.sh" --sysroot "$R" --name ztos-miatoll-bootpart
install -d "$STAGEDIR/boot"
cp "$DEVROOT/out/ztos-miatoll-bootpart.img" "$STAGEDIR/boot/"

if [ -d "$DEVROOT/build/wallpapers/backgrounds/ztos" ]; then
	install -d "$R/usr/local/share/backgrounds"
	cp -a "$DEVROOT/build/wallpapers/backgrounds/ztos" "$R/usr/local/share/backgrounds/"
fi

# The PMIC's RTC is not set by ztOS and comes up years off after a flash: then
# gpg rejects pacman's keys as made "in the future", and zboot-firstboot
# fails. systemd sets the clock to at least /usr/lib/clock-epoch's date at
# start, so the build's date is the earliest the phone can believe; NTP
# corrects it once it is online.
touch "$R/usr/lib/clock-epoch"

# systemd-networkd runs the USB link's DHCP server (10-zboot-usb.network), so
# the computer gets 172.16.42.2; the serial port over USB has a login
DEVICE_UNITS="zboot-usb-gadget zboot-firstboot systemd-networkd serial-getty@ttyGS0 sshd
	rmtfs pd-mapper tqftpserv ModemManager zrnperfd ztos-miatoll-audio"

# a developer's SSH key, when the build tree has one (build/ is never
# committed): into /etc/skel, which the account step copies into the home
if [ -s "$DEVROOT/build/miatoll-authorized_keys" ]; then
	install -d -m 700 "$R/etc/skel/.ssh"
	install -m 600 "$DEVROOT/build/miatoll-authorized_keys" "$R/etc/skel/.ssh/authorized_keys"
fi

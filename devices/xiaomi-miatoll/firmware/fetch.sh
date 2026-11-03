#!/bin/sh
# The Redmi Note 9 Pro / 9S (miatoll)'s own firmware: the DSPs' (modem, ADSP,
# CDSP, Wi-Fi), the GPU's zap shader, video and the two touchscreens', from
# sm7125-mainline/firmware-xiaomi-miatoll at the commit postmarketOS's
# firmware-xiaomi-miatoll 5 ships, laid out as that package does.
# The GPU's own (a630_sqe.fw, a630_gmu.bin) is Arch's linux-firmware-qcom.
#
#   devices/xiaomi-miatoll/firmware/fetch.sh [<out root>]
#
# Out: <out root>/usr/lib/firmware/... (default build/miatoll-firmware).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
OUT=${1:-$ROOT/build/miatoll-firmware}
COMMIT=a7073ad14a91a103d14effbc87a94e632e29011a
SHA512=5535bbb86d41dabca255e76f0edd3032e58df1a8bee444dc26238119b224bdd8b3db97101439a81b9c24a16eeba106e8d0adf22904f0f810ee91008b69102a10
URL=https://github.com/sm7125-mainline/firmware-xiaomi-miatoll/archive/$COMMIT/firmware-xiaomi-miatoll-$COMMIT.tar.gz
TARBALL=$ROOT/build/firmware-xiaomi-miatoll-$COMMIT.tar.gz

mkdir -p "$ROOT/build"
if [ ! -f "$TARBALL" ] || ! echo "$SHA512  $TARBALL" | sha512sum -c --status; then
	echo "firmware: downloading $URL"
	curl -L --fail -o "$TARBALL.part" "$URL"
	echo "$SHA512  $TARBALL.part" | sha512sum -c --quiet
	mv "$TARBALL.part" "$TARBALL"
fi
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
tar -xzf "$TARBALL" -C "$W" --strip-components=1

FW=$OUT/usr/lib/firmware
rm -rf "$FW"
install -d "$FW/qcom/sm7125/miatoll"
install -m644 "$W/lib/firmware/focaltech_ts_fw_huaxing.bin" "$W/lib/firmware/novatek_ts_tianma_fw.bin" "$FW/"
for f in a615_zap.mbn adsp.mbn cdsp.mbn ipa_fws.mbn modem.mbn venus.mbn wlanmdsp.mbn \
		adspr.jsn adsps.jsn adspua.jsn cdspr.jsn modemr.jsn modemuw.jsn; do
	install -m644 "$W/lib/firmware/qcom/sm7125/miatoll/$f" "$FW/qcom/sm7125/miatoll/"
done
echo "firmware: $(find "$FW" -type f | wc -l) files in $FW"

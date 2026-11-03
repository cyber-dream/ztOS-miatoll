#!/bin/bash
# The Redmi Note 9 Pro / 9S (miatoll: joyeuse, curtana) kernel: the
# sm7125-mainline tree postmarketOS ships for these phones, built as
# postmarketOS builds it (its config), with ztOS's additions (ztos.config)
# and one fix, the speaker amplifier's driver (patch-tas2562.py). No postmarketOS tooling: the tarball comes from GitHub.
#
#   devices/xiaomi-miatoll/kernel/build-kernel.sh [-j N]
#
# Out: build/linux-<tag>/out/{arch/arm64/boot/vmlinuz.efi (EFI zboot, what
# systemd-boot starts), arch/arm64/boot/dts/qcom/sm7125-*.dtb, modroot/,
# kernel.release}. Runs on any Linux with clang, lld, llvm and bc (LLVM=1
# cross-compiles to arm64 without a separate toolchain); nothing needs root.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
TAG=sm7125-6.14.7-r2
# the tarball postmarketOS's linux-postmarketos-qcom-sm7125 6.14.7-r4 builds from
SHA512=1e36a159c792a48b207ef0734da1ebfc2aa309968f3e9b54ee8d67216c3ede53c34dfb2b6b8626c836721c2f23aec3ed57143a158c208d004ab6360bffb8db7f
URL=https://github.com/sm7125-mainline/linux/archive/$TAG/linux-$TAG.tar.gz

JOBS=$(nproc)
[ "${1:-}" = -j ] && JOBS=$2

B=$ROOT/build/linux-$TAG
SRC=$B/src
OUT=$B/out
TARBALL=$ROOT/build/linux-$TAG.tar.gz
mkdir -p "$ROOT/build"

if [ ! -f "$TARBALL" ] || ! echo "$SHA512  $TARBALL" | sha512sum -c --status; then
	echo "kernel: downloading $URL"
	curl -L --fail -o "$TARBALL.part" "$URL"
	echo "$SHA512  $TARBALL.part" | sha512sum -c --quiet
	mv "$TARBALL.part" "$TARBALL"
fi

# a fresh tree on every build, dated now (ccache keeps the rebuild quick)
echo "kernel: unpacking $TAG"
rm -rf "$SRC"
mkdir -p "$SRC"
tar -xzf "$TARBALL" -C "$SRC" --strip-components=1 --touch

python3 "$HERE/patch-tas2562.py" "$SRC"

mkdir -p "$OUT"
cp "$HERE/config-sm7125.aarch64" "$OUT/.config"
# HOSTCFLAGS: resolve_btfids builds libbpf with -Werror, and glibc 2.43's
# C23 strstr/strchr return const for a const argument, which 6.14's libbpf
# predates
M=(make -C "$SRC" O="$OUT" ARCH=arm64 LLVM=1 HOSTLDFLAGS=-fuse-ld=lld
	HOSTCFLAGS=-Wno-error=incompatible-pointer-types-discards-qualifiers
	KBUILD_BUILD_USER=ztos KBUILD_BUILD_HOST=ztos)
if command -v ccache > /dev/null; then
	export CCACHE_DIR=$ROOT/build/ccache CCACHE_BASEDIR=$B CCACHE_SLOPPINESS=time_macros,include_file_mtime,include_file_ctime
	M+=(CC="ccache clang" HOSTCC="ccache clang")
fi
"$SRC/scripts/kconfig/merge_config.sh" -m -O "$OUT" "$OUT/.config" "$HERE/ztos.config" > /dev/null
"${M[@]}" olddefconfig > /dev/null

# every line of ztos.config must have survived olddefconfig
bad=0
while IFS= read -r line; do
	case "$line" in
	CONFIG_*=*) grep -qxF "$line" "$OUT/.config" || { echo "kernel: config lost: $line" >&2; bad=1; } ;;
	"# CONFIG_"*" is not set")
		sym=${line#\# }; sym=${sym%% *}
		grep -q "^$sym=" "$OUT/.config" && { echo "kernel: config should be off: $sym" >&2; bad=1; } ;;
	esac
done < "$HERE/ztos.config"
[ "$bad" = 0 ] || exit 1

echo "kernel: building with $JOBS jobs"
# with CONFIG_EFI_ZBOOT the default image is vmlinuz.efi
"${M[@]}" -j"$JOBS" all
rm -rf "$OUT/modroot"
"${M[@]}" INSTALL_MOD_PATH="$OUT/modroot/usr" INSTALL_MOD_STRIP=1 modules_install > /dev/null
KREL=$(cat "$OUT/include/config/kernel.release")
rm -f "$OUT/modroot/usr/lib/modules/$KREL/build" "$OUT/modroot/usr/lib/modules/$KREL/source"
echo "$KREL" > "$OUT/kernel.release"
ls -l "$OUT/arch/arm64/boot/vmlinuz.efi" "$OUT"/arch/arm64/boot/dts/qcom/sm7125-xiaomi-*.dtb
echo "kernel: $KREL ready"

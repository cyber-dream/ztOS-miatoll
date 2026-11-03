> **This erases the phone.** ztOS writes USERDATA, CACHE, DTBO and BOOT. It
> never touches the IMEI, the modem's partitions or persist.

For the Redmi Note 9 Pro (`joyeuse`) and 9S (`curtana`), not the 5G.

## What you need

- A Linux computer with 30 GB free
- A USB-C data cable, and the phone charged above 50%

## 1. Install the tools on your computer

```sh
sudo pacman -S --needed git python clang lld llvm bc ccache flex bison pahole \
  dtc dosfstools mtools cpio e2fsprogs libarchive android-tools qemu-user-static qemu-user-static-binfmt
```

## 2. Get ztOS

```sh
git clone https://github.com/cyber-dream/ztOS-miatoll.git
cd ztOS-miatoll
installer/ztos doctor
```

## 3. Unlock the bootloader

1. In MIUI: Developer options > **Mi Unlock status**, bind your Mi account.
2. In fastboot (Volume Down + Power), unlock with Xiaomi's **Mi Unlock** tool. It may ask you to wait some days.

## 4. Install ztOS

```sh
mkdir -p build && echo joyeuse-huaxing > build/miatoll-variant   # or curtana-, -tianma
devices/xiaomi-miatoll/kernel/build-kernel.sh
installer/ztos build --target miatoll --user YOURNAME --password YOURPASSWORD
devices/xiaomi-miatoll/scripts/flash.sh    # phone in fastboot
```

The build takes about 1–1½ hours the first time.

## 5. Start ztOS

The first start takes about a minute. Then log in with your user name and
password. Over USB: `ssh YOURNAME@172.16.42.1`.

Save the phone's own partitions once with
`devices/xiaomi-miatoll/scripts/backup.sh YOURNAME@172.16.42.1`.

## If something goes wrong

| What you see | What to do |
|---|---|
| A black screen, but `ssh` works | Your phone has the other panel: write `joyeuse-tianma` to `build/miatoll-variant`, run `devices/xiaomi-miatoll/boot/build-boot.sh` and `flash.sh --boot-only` |
| `the battery is too low for the bootloader` | Charge it switched off for half an hour |
| `pacman` times out | Put `Server = http://de.mirror.archlinuxarm.org/$arch/$repo` first in `/etc/pacman.d/mirrorlist` |
| Back to Android | Flash Xiaomi's fastboot ROM with its `flash_all.sh` |

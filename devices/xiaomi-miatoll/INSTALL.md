## Installing ztOS on the Redmi Note 9 Pro / 9S (miatoll)

> **This erases the phone.** Everything in USERDATA (Android's apps, data and
> files) is replaced. ztOS writes four partitions only: CACHE (its boot
> partition), USERDATA (the system), DTBO (erased) and BOOT (U-Boot). The
> phone's identity (IMEI, the modem's calibration, the Wi-Fi and Bluetooth
> addresses, the sensors' calibration) is never written, and step 7 saves
> it to your computer anyway.

Works on the **Redmi Note 9 Pro** (global, `joyeuse`) and the **Redmi Note
9S** (`curtana`). Tried on a joyeuse with a Huaxing panel; the 9S and the
Tianma panel use the same kernel and device trees, but nobody has tried them
yet. The Redmi Note 9 Pro **5G** and the Chinese Note 9 Pro are other phones.

### What you need

- A Linux computer with 30 GB free and an internet connection (the build
  needs user namespaces; it does not run on Windows or macOS)
- A USB-C data cable (some only charge)
- The phone charged above 50 %
- About 1½ hours the first time, mostly the build; plus Xiaomi's waiting
  period for unlocking (step 2), which can be a week

### 1. Install the tools on your computer

On Arch Linux:

```sh
sudo pacman -S --needed git python clang lld llvm bc ccache flex bison pahole \
  dtc dosfstools mtools cpio e2fsprogs libarchive util-linux android-tools \
  qemu-user-static qemu-user-static-binfmt
```

The build runs as you, not as root, in a user namespace, so your user needs
a range of sub-UIDs. Check that this prints a line:

```sh
grep "^$USER:" /etc/subuid || sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 "$USER"
```

On other distributions the names differ a little: clang, lld, llvm, bc,
flex, bison, pahole (dwarves), dtc (device-tree-compiler), dosfstools,
mtools, cpio, e2fsprogs, bsdtar (libarchive), fastboot and img2simg
(android-tools or android-sdk-libsparse-utils), and qemu-user-static with
binfmt.

### 2. Unlock the bootloader

1. Back up the phone. Unlocking erases it.
2. In MIUI: Settings > About phone, tap **MIUI version** seven times.
3. Settings > Additional settings > Developer options > **Mi Unlock status**:
   bind your Mi account (with a SIM in and mobile data on).
4. Turn the phone off, hold **Volume Down + Power**: the fastboot screen.
5. Run Xiaomi's **Mi Unlock** tool (Windows), sign in with the same account
   and unlock. It usually says to wait a number of hours or days first;
   wait them out and run it again. Unlocking erases the phone.

### 3. Get ztOS

```sh
git clone https://github.com/cyber-dream/ztOS-miatoll.git
cd ztOS-miatoll
```

### 4. Say which phone and panel

The kernel's device tree depends on the phone and on who made its panel:
`joyeuse-huaxing` (the default), `joyeuse-tianma`, `curtana-huaxing` or
`curtana-tianma`. With the phone in fastboot,

```sh
fastboot getvar product
```

says `joyeuse` or `curtana`. There is no simple way to read the panel's
maker from fastboot: start with Huaxing, the more common one. If the screen
stays black after step 8 while the phone answers over USB, it is the other
panel; change the word, run `devices/xiaomi-miatoll/boot/build-boot.sh`
and `devices/xiaomi-miatoll/scripts/flash.sh --boot-only`, which writes the
boot partition only.

```sh
mkdir -p build
echo joyeuse-huaxing > build/miatoll-variant
```

### 5. Build the kernel

```sh
devices/xiaomi-miatoll/kernel/build-kernel.sh
```

It downloads sm7125-mainline's Linux 6.14.7 and builds it with clang: 20
to 60 minutes the first time, depending on the computer, and much less the
next time (ccache).

### 6. Build the system

Choose the user name and password you will log in with (the password is
also for `sudo`):

```sh
python3 installer/ztos build --target miatoll --user YOURNAME --password YOURPASSWORD --hostname ztos-miatoll
```

About 35 minutes: the phone's programs run emulated on the computer while
the system is put together. The result is `build/ztos/miatoll/root.img` and
the boot partition `build/ztos/miatoll/stage/boot/ztos-miatoll-bootpart.img`.

### 7. Install it

1. Turn the phone off and hold **Volume Down + Power** until the fastboot
   screen. Plug it in.
2. Run:

   ```sh
   devices/xiaomi-miatoll/scripts/flash.sh
   ```

   It checks that the phone is a miatoll and charged enough for its
   bootloader, then asks you to type `erase`. It writes the boot partition
   to CACHE, the system to USERDATA, erases DTBO and writes U-Boot to BOOT,
   then restarts the phone. Writing the system takes a few minutes.

### 8. Start ztOS

The first start takes about a minute: the system grows to fill USERDATA and
sets up pacman's keys. Then the lock screen; log in with your user and
password, and the Welcome app shows how ztOS is used.

Over the USB cable the phone is also a network: `ssh YOURNAME@172.16.42.1`
(the computer gets an address by DHCP).

Then save the phone's own partitions to the computer, once:

```sh
devices/xiaomi-miatoll/scripts/backup.sh YOURNAME@172.16.42.1
```

It asks the phone's password (for `sudo` on it), reads persist, the modem's
partitions (modemst1, modemst2, fsg, fsc) and devinfo twice each, compares
the two reads and keeps them in `backups/miatoll/`. ztOS never writes them;
keep a copy somewhere safe all the same.

### Upgrading to a new version, keeping your data

A new system version replaces USERDATA. `upgrade.sh` keeps your data across
it: your home (settings, files), Wi-Fi networks, Bluetooth pairings,
messages and your password. Build the new version as in steps 5 and 6, with
the same user name, then:

1. With the old ztOS running and the cable in:

   ```sh
   devices/xiaomi-miatoll/scripts/upgrade.sh save
   ```

2. Step 7.
3. When the new ztOS shows its lock screen:

   ```sh
   devices/xiaomi-miatoll/scripts/upgrade.sh restore backups/miatoll/ztos-data-DATE.tar.gz
   ```

The saved file holds Wi-Fi passwords and messages: it stays in `backups/`,
out of git.

A new kernel alone (step 5 again) keeps everything: copy its modules to the
phone (`build/linux-sm7125-6.14.7-r2/out/modroot/usr/lib/modules/*` to
`/usr/lib/modules/`, then `sudo depmod -a`), run
`devices/xiaomi-miatoll/boot/build-boot.sh`, and write the boot partition
alone with `devices/xiaomi-miatoll/scripts/flash.sh --boot-only`.

### If something goes wrong

| What you see | What to do |
|---|---|
| `no phone in fastboot` | Power off, hold Volume Down + Power; try another cable or USB port; `fastboot devices` should list it |
| `the battery is too low for the bootloader` | Charge it switched off for half an hour, then step 7 again |
| `this computer is missing what building ztOS needs` | Install what it lists (step 1) |
| `a /etc/subuid range for ...` | The `usermod --add-subuids` line in step 1 |
| `no kernel: run devices/xiaomi-miatoll/kernel/build-kernel.sh first` | Step 5, then step 6 again |
| A black screen, but `ssh` over USB works | The other panel: step 4 |
| ztOS's rescue screen: the system was not found | Step 7 again |
| `pacman` is slow or times out | Put `Server = http://de.mirror.archlinuxarm.org/$arch/$repo` first in `/etc/pacman.d/mirrorlist` on the phone |

### Back to Android

Download Xiaomi's **fastboot ROM** (not the recovery ROM) for your model and
region, and flash it from fastboot with its own `flash_all.sh`. It writes
every partition ztOS used, BOOT and DTBO included. Keep the bootloader
unlocked until Android has started once.

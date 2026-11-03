> **This erases the phone.** ztOS replaces Android's apps, data and files in
> USERDATA, and writes three more partitions: CACHE (its boot partition),
> DTBO (erased) and BOOT (U-Boot). It never touches the phone's identity: the
> IMEI and the modem's calibration (modemst1, modemst2, fsg, fsc), the Wi-Fi
> and Bluetooth addresses and the sensors' calibration (persist), and the
> bootloader's unlock state (devinfo). Step 5 saves those to your computer
> anyway.

Works on the **Redmi Note 9 Pro** (global, `joyeuse`) and the **Redmi Note
9S** (`curtana`). It was tried on a joyeuse with a Huaxing panel. The 9S and
the Tianma panel use the same kernel and device trees, but nobody has tried
them yet. The Redmi Note 9 Pro **5G** and the Chinese Note 9 Pro are
different phones, and this does not work on them.

## What you need

- A Linux computer with 30 GB free and an internet connection. The build
  needs user namespaces, so it does not run on Windows or macOS.
- A USB-C data cable (some cables only charge), and the phone charged above 50%
- About 1½ hours the first time, mostly the build, plus Xiaomi's waiting
  period for unlocking (step 3), which can be a week

## 1. Install the tools on your computer

```sh
sudo pacman -S --needed git python clang lld llvm bc ccache flex bison pahole \
  dtc dosfstools mtools cpio e2fsprogs libarchive util-linux android-tools \
  qemu-user-static qemu-user-static-binfmt
grep "^$USER:" /etc/subuid || sudo usermod --add-subuids 100000-165535 --add-subgids 100000-165535 "$USER"
```

The second line gives your user a range of sub-UIDs. The build runs as you,
not as root, in a user namespace, and needs them.

## 2. Get ztOS

```sh
git clone https://github.com/cyber-dream/ztOS-miatoll.git
cd ztOS-miatoll
installer/ztos doctor
```

`doctor` should say `this computer can build ztOS`. If it doesn't, it lists
what is missing.

## 3. Unlock the bootloader

1. Back up what you want to keep from Android: unlocking erases the phone.
2. In MIUI, go to Settings > About phone and tap **MIUI version** seven times.
3. Go to Settings > Additional settings > Developer options > **Mi Unlock
   status** and bind your Mi account. This needs a SIM in the phone and
   mobile data on.
4. Turn the phone off and hold **Volume Down + Power** until the fastboot
   screen appears.
5. Run Xiaomi's **Mi Unlock** tool (Windows), sign in with the same account
   and unlock. It usually asks you to wait some hours or days first. Wait
   that long, then run it again.

## 4. Install ztOS

Say which phone and panel you have: `joyeuse-huaxing`, `joyeuse-tianma`,
`curtana-huaxing` or `curtana-tianma`. With the phone in fastboot,
`fastboot getvar product` prints `joyeuse` or `curtana`. Fastboot can't tell
you who made the panel, so start with Huaxing, the more common one. Step 5
says what to do if that is the wrong one.

```sh
mkdir -p build
echo joyeuse-huaxing > build/miatoll-variant
```

Build the kernel and the system. Choose the user name and password you will
log in with; the password is also the one for `sudo`.

```sh
devices/xiaomi-miatoll/kernel/build-kernel.sh
python3 installer/ztos build --target miatoll --user YOURNAME --password YOURPASSWORD --hostname ztos-miatoll
```

Then wait while they run:

- **The kernel** takes 20–60 minutes the first time, depending on the
  computer, and much less after that because ccache keeps its work. The
  script downloads sm7125-mainline's Linux 6.14.7 and builds it with clang.
- **The system** takes about 35 minutes. The phone's programs run emulated
  on the computer while the system is put together.

Turn the phone off, hold **Volume Down + Power** until the fastboot screen
appears, and plug it in. Then run:

```sh
devices/xiaomi-miatoll/scripts/flash.sh
```

First it checks that the phone is a miatoll and charged enough for its
bootloader. Then it asks you to type `erase`. It writes the boot partition
to CACHE and the system to USERDATA, erases DTBO, writes U-Boot to BOOT and
restarts the phone. Writing the system takes a few minutes.

## 5. Start ztOS

The first start takes about a minute: the system grows to fill USERDATA and
sets up pacman's keys. When the lock screen appears, log in with your user
name and password. The Welcome app then shows how ztOS is used.

If the screen stays black but the phone answers over USB, it has the other
panel. Write the other variant to `build/miatoll-variant`, then run
`devices/xiaomi-miatoll/boot/build-boot.sh` and
`devices/xiaomi-miatoll/scripts/flash.sh --boot-only`. That writes only the
boot partition.

Over the USB cable the phone is also a network: `ssh YOURNAME@172.16.42.1`.
The computer gets its address by DHCP.

Then save the phone's own partitions to the computer, once:

```sh
devices/xiaomi-miatoll/scripts/backup.sh YOURNAME@172.16.42.1
```

It asks for the phone's password, for `sudo` on the phone. It reads persist,
modemst1, modemst2, fsg, fsc and devinfo twice each, compares the two reads,
and keeps them in `backups/miatoll/`. Keep a copy somewhere safe.

## If something goes wrong

| What you see | What to do |
|---|---|
| `no phone in fastboot` | Power the phone off and hold Volume Down + Power. If that doesn't help, try another cable or USB port; `fastboot devices` should list the phone |
| `the battery is too low for the bootloader` | Charge it, switched off, for half an hour, then run `flash.sh` again |
| `this computer is missing what building ztOS needs` | Install what it lists (step 1) |
| `a /etc/subuid range for ...` | Run the `usermod --add-subuids` line from step 1 |
| `no kernel: run devices/xiaomi-miatoll/kernel/build-kernel.sh first` | Build the kernel, then the system again (step 4) |
| A black screen, but `ssh` over USB works | It is the other panel (step 5) |
| ztOS's rescue screen says the system was not found | Run `flash.sh` again |
| `pacman` is slow or times out | On the phone, put `Server = http://de.mirror.archlinuxarm.org/$arch/$repo` first in `/etc/pacman.d/mirrorlist` |

## Updating, keeping your data

A new system replaces USERDATA, and `upgrade.sh` carries your data across
it: your home, Wi-Fi networks, Bluetooth pairings, messages and password.

1. Build the new version as in step 4, with the same user name.
2. With the old ztOS running and the cable in, run
   `devices/xiaomi-miatoll/scripts/upgrade.sh save`.
3. Run `flash.sh`.
4. When the new ztOS shows its lock screen, run
   `devices/xiaomi-miatoll/scripts/upgrade.sh restore backups/miatoll/ztos-data-DATE.tar.gz`.

The saved file holds Wi-Fi passwords and messages, so keep it private.

## Back to Android

Download Xiaomi's **fastboot ROM** for your model and region (not the
recovery ROM), and flash it from fastboot with its own `flash_all.sh`. It
writes back every partition ztOS used, BOOT and DTBO included. Keep the
bootloader unlocked until Android has started once.

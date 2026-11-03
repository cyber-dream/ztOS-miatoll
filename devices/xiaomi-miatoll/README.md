# ztOS on the Redmi Note 9 Pro / 9S (miatoll)

The SM7125 (Snapdragon 720G) Xiaomi phones: **joyeuse** (Redmi Note 9 Pro,
Global) and **curtana** (Redmi Note 9S). ztOS is built here **without
postmarketOS**: the kernel, the boot stage, the firmware and the device
layer come from this folder, and the system from `installer/ztos build
--target miatoll`. The layout follows the Galaxy S9 (Snapdragon) port
(Davitekk/ztOS, `devices/samsung-starqltechn`).

Tried on a joyeuse (Huaxing panel), ztOS BETA-1.1 (0.2.1).

## Status

**Works**: tried, and does what it should. **Partial**: some of it works;
the notes say what. **Untested**: in place, not yet tried. **Not started**:
nothing yet.

| Category | Feature | Status | Notes |
|---|---|---|---|
| Boot | Booting from internal storage (UFS) | Works | Xiaomi's bootloader, U-Boot, systemd-boot, the kernel, zboot; the root is found in 1.7 s |
| Boot | Rescue shell | Untested | zboot's, on USB (telnet 172.16.42.1), when no root is found or with `zboot.rescue=1` |
| Display | Panel, 1080x2400 | Works | Huaxing FT8756 (Tianma NT36675 built in too, untried), MSM DPU |
| Display | Brightness | Works | ztOSIAL Display1, linear curve |
| Input | Touchscreen | Works | FT8756 over SPI; its firmware in the boot stage. The kernel logs an occasional CRC error on a read |
| Input | Power and volume keys | Works | |
| 3D | GPU | Works | Adreno 618 through freedreno (Mesa reports FD618), OpenGL ES 3.2 |
| Power | Battery level | Works | PM6150 QGauge (`qcom_qg`); for a minute after a reboot it can show a wrong level |
| Power | Charging | Works | At 5 V and the 1.4 A input limit Xiaomi's bootloader programs: 0.4-0.5 A into the battery with the screen on (about 30 °C). Linux has no driver for the PM6150's charger; the PMIC charges by itself once the USB-C port may be a sink ([`boot/usb-c-sink.dtso`](boot/usb-c-sink.dtso), below). ztOSIAL's Power1 tells charging from the current into the battery |
| Power | Fast charging | Not started | USB PD at 9 V (a 9 V sink PDO) and a charger driver for the input limit above 1.4 A |
| Power | Clock across reboots | Partial | Not kept by the PMIC's RTC; `/usr/lib/clock-epoch` keeps it at least at the build's date, NTP sets it once online |
| Network | Wi-Fi | Works | WCN3991 (ath10k_snoc), `wld0`, through the modem DSP |
| Network | USB networking to a computer | Works | NCM: 172.16.42.1, the computer gets 172.16.42.2; a serial console (ttyGS0) too |
| Network | Bluetooth | Untested | |
| Modem | DSP, QRTR services | Works | qrtr, rmtfs, pd-mapper, tqftpserv; the modem, ADSP and CDSP run |
| Modem | SIM, calls, SMS, mobile data | Not started | No ModemManager in the GSI yet |
| Audio | Speaker | Works | TAS2562, sm7125-mainline's UCM profile; PipeWire's default output. Needs [`kernel/patch-tas2562.py`](kernel/patch-tas2562.py): the mainline driver writes the amplifier's digital volume in the wrong byte order (below) |
| Audio | Headphones, microphones | Untested | In the profile (WCD9375): headphones, bottom and top mics |
| Sensors | Accelerometer, light, proximity | Not started | On the ADSP's sensor domain (hexagonrpcd, libssc) |
| Camera | Cameras | Not started | |
| Other | Fingerprint, IR, NFC | Not started | |

## How it is made

| | |
|---|---|
| Kernel | [sm7125-mainline](https://github.com/sm7125-mainline/linux) `sm7125-6.14.7-r2`, the tarball and config postmarketOS's `linux-postmarketos-qcom-sm7125` 6.14.7 builds, plus [`kernel/ztos.config`](kernel/ztos.config) and one fix, [`kernel/patch-tas2562.py`](kernel/patch-tas2562.py): [`kernel/build-kernel.sh`](kernel/build-kernel.sh). Cross-compiled with clang (`LLVM=1`), no root, about 25 minutes the first time and minutes after (ccache). The config is postmarketOS's but for `LOCALVERSION` (`6.14.7-ztos-sm7125`); its 771 modules are the same |
| USB-C | U-Boot's device tree has the port as a source only (`power-role = "source"`): the Type-C driver then keeps the PMIC in source-only mode (`TYPE_C_MODE_CFG` 0x1544 = 0x04), a charger's attach is never seen, BC1.2 detection never starts (it starts on a CC attach), and the input limit stays at USB's 500 mA (`ICL_STATUS` 0x1107 = 0x0a). [`boot/usb-c-sink.dtso`](boot/usb-c-sink.dtso) makes it dual-role, trying sink first, with one 5 V 3 A sink PDO; the PMIC then detects the charger (DCP) and takes the 1.4 A already programmed (0x1107 = 0x1c). Read from the PMIC with regmap's debugfs, read-only; nothing in the charger is written |
| Boot chain | The Xiaomi bootloader starts U-Boot ([sm7125-mainline's 2025-06-02 release](https://github.com/sm7125-mainline/u-boot/releases)) from BOOT, with DTBO erased; U-Boot gives the device tree and starts systemd-boot (the root's own `systemd-bootaa64.efi`) from CACHE, a 272 MiB FAT32 labelled `pmOS_boot` (UFS's 4 KiB sectors need that size for FAT32), which starts the kernel (an EFI zboot image) and zboot: [`boot/build-boot.sh`](boot/build-boot.sh), no root (mtools). The device tree is U-Boot's own for the phone and panel (`--variant`: joyeuse-huaxing by default, joyeuse-tianma, curtana-huaxing, curtana-tianma; or the word in `build/miatoll-variant`), taken from the U-Boot release with the USB-C overlay applied, as the entry's `devicetree` (systemd-boot applies overlays only to a tree it loads); U-Boot's EFI fixup fills in the memory |
| Boot stage | zboot ([`initramfs/init`](initramfs/init)), the S9 (Snapdragon) port's with this phone's names, on Arch Linux ARM's static busybox: finds the root labelled `ZR_ARCH` (or `pmOS_root`, a ztConflo image's) in USERDATA, mounts it read-only and hands over; UFS, the panels and the USB gadget are built into the kernel, so it loads nothing. It carries the touchscreens' firmware, which their built-in drivers ask for before the root is mounted |
| Firmware | [sm7125-mainline/firmware-xiaomi-miatoll](https://github.com/sm7125-mainline/firmware-xiaomi-miatoll) at the commit postmarketOS's `firmware-xiaomi-miatoll` 5 ships, the same 15 files ([`firmware/fetch.sh`](firmware/fetch.sh), checked by sha512); the GPU's microcode from Arch's `linux-firmware-qcom`; Wi-Fi's board file from Arch's `linux-firmware` |
| Modem, DSPs | qrtr and rmtfs from Arch Linux ARM's [aur] repository ([`packages.txt`](packages.txt)); pd-mapper v1.1 and tqftpserv v1.3 built from linux-msm's sources in [`layer.sh`](layer.sh) |
| Sound | The `Xiaomi/miatoll` UCM profile from [sm7125-mainline/alsa-ucm-conf](https://github.com/sm7125-mainline/alsa-ucm-conf) (`059aa77e`, what postmarketOS's `alsa-ucm-conf-qcom-sm7125` 2 ships), with its own headphone sequences beside it so Arch's codec files stay as they are: [`overlay/usr/share/alsa/ucm2`](overlay/usr/share/alsa/ucm2). [`ztos-miatoll-audio`](overlay/usr/local/sbin/ztos-miatoll-audio): the LPASS pin controller can ask the ADSP for its clocks too early at boot, time out and leave the card unmade; it is bound again once the ADSP is up. Then it runs the profile's BootSequence (`alsaucm set _boot`) after alsa-restore and udev's own `alsactl restore`: a saved state with the amplifier's volume at 0 (-110 dB) otherwise comes back at every boot |
| Speaker amplifier | TAS2562 on I2C 9-004c. Its digital volume is four bytes, and the chip takes the value when the last one (`DVC_CFG4`) is written; the mainline driver writes that one first, so each change applied the previous value's upper bytes (traced: 80 then 109 read back as 80's bytes 1-3). The patch writes it MSB first, drops the register cache (the page select drifted from the chip's, and writes landed on the wrong page) and resets the chip at probe: it is powered from the battery and keeps its registers across a reboot |
| ztOSIAL | [`vendor/ztos/manifest.toml`](vendor/ztos/manifest.toml): generic backends; [`vendor/ztos/csf/components`](vendor/ztos/csf/components) for ztoscsf (this phone's daemons, no sensor hub) |
| Root | `LABEL=ZR_ARCH` in [`overlay/etc/fstab`](overlay/etc/fstab); zboot-firstboot grows it to the whole of USERDATA (about 103 GB) on the first boot |

### What ztOS needed besides this folder

- **ztOSIAL Power1** (`ial/ztos_ial/backends/generic.py`): with no charger
  driver, charging is the current into the battery. `qcom_qg` counts it
  negative while charging; the USB-C port's `tcpm-source-psy` is what the
  phone gives out, not a charger; the gauge's `current_avg` lags a minute
  and the UDC's state stays "configured" after unplugging, so neither is
  used. More than 10 mA in, or a voltage over the open-circuit one, is a
  charger, held for 30 s so the state does not flap on a weak supply; more
  than 100 mA out ends it at once. Display1 and Haptics1 take `"auto"` as
  unset.
- **ztoscsf** (`ztmetro/csf/ztoscsf.c`) reads a device layer's
  `/vendor/ztos/csf/components` first and looks after a sensor hub only
  where one is listed: no "Core power, Vibration, Notification not
  available" or "Sensor hub stopped" on a phone without the S9's hub.
- **ztMETRO** (`ztmetro/ztmetro.c`) asks Power1's `ChargerOnline` when sysfs
  shows no charger.
- **The installer** (`installer/ztos_installer`): the `miatoll` target; and
  a user the base already has (`alarm`) now gets `/etc/skel` copied into its
  home. It did not, so `~/.config/ztmetro` was missing and **Settings saved
  nothing**. pacman's download timeout is off, so a stalling mirror does not
  abort the build.

## Installing

> **A full flash erases the phone**: everything in USERDATA. Only CACHE,
> USERDATA, DTBO and BOOT are written; the modem's own partitions (EFS:
> IMEI, calibration) are never touched.

1. **Unlock the bootloader** with Xiaomi's Mi Unlock tool (MIUI: Settings >
   About phone, tap MIUI version seven times; Developer options > Mi Unlock
   status, bind the account; wait out Xiaomi's waiting period).
2. **Charge the phone.** At a low charge the bootloader refuses some
   commands halfway through (`erase dtbo` failed at 12 %); `flash.sh` checks
   `battery-soc-ok` first.
3. On the computer (Linux): clang, lld, llvm, bc, ccache, flex, bison,
   pahole, dosfstools, mtools, cpio, android-tools (fastboot, img2simg) and
   qemu-user-static (to check the boot stage's busybox).
4. Build and flash:

   ```
   devices/xiaomi-miatoll/kernel/build-kernel.sh
   installer/ztos build --target miatoll --user alarm --password alarm --hostname ztos-miatoll
   # the phone in fastboot: power off, hold Volume Down + Power
   devices/xiaomi-miatoll/scripts/flash.sh
   ```

   `installer/ztos build` runs in a user namespace, no sudo, about 35
   minutes (ARM programs run emulated on an x86 computer). `flash.sh` checks
   that the phone is a miatoll and charged, asks for the word `erase`, then
   writes CACHE, USERDATA (sparse), erases DTBO and writes U-Boot to BOOT
   last.
5. The first boot grows the root and sets up pacman's keyring, then shows
   the lock screen. Log in over USB: `ssh alarm@172.16.42.1`.

### Updating

- **A new kernel or boot stage alone**, the system and its data kept:
  install the new modules on the phone (`usr/lib/modules/<release>` from
  `build/linux-sm7125-6.14.7-r2/out/modroot`, then `depmod`), and
  `scripts/flash.sh --boot-only` (CACHE only).
- **A new system version**, keeping the user's data:
  `scripts/upgrade.sh save` on the old system, `scripts/flash.sh`, wait for
  the lock screen, `scripts/upgrade.sh restore <file>`. Kept: the home,
  Wi-Fi networks, Bluetooth pairings, messages, learned performance state,
  SSH host keys, the time zone and the password (the same `--user` on both).
  The file holds Wi-Fi passwords: it stays in `backups/` (git-ignored).

### Going back

`fastboot flash cache <the previous boot partition image>` puts back the
previous kernel and boot stage without touching the system. If even
fastboot will not come up, hold Volume Down + Power from off: it is the
Xiaomi bootloader's own, never written.

## Known issues and next steps

- **Fast charging**: a 9 V sink PDO for USB PD chargers, and a charger
  driver for an input limit above the bootloader's 1.4 A. Casey Connolly's
  SMB5 patch for `qcom_smbx` (v1, June 2025, not merged: it also writes the
  Type-C block the Type-C driver owns) is a start, but it programs float
  voltage, charge current and input limit with SMB2's scales. The PM6150 is
  SMB5: float voltage 3.6 V + 10 mV a step, currents 50 mA a step (Xiaomi's
  `qpnp-smb5.c`, and the bootloader's FV 0x55 = 4.45 V). As posted it would
  set about 4.82 V. Any driver writes only the input limit at first.
- **The clock**: a service keeping the RTC's offset, as the S9 (Snapdragon)
  port's `ztos-starq-rtc` does.
- **ztMETRO aborts on a requested restart** (display size, style): it does
  not remove its backend listeners before `wlr_backend_destroy`, and
  wlroots asserts; greetd starts it again. ztOS's own, the S9's too.
- **Calls, SMS, data**: ModemManager over QRTR.
- **Sensors**: hexagonrpcd and libssc for the ADSP's sensor domain
  (`vendor/ztos/udev/81-libssc-xiaomi-miatoll.rules` is postmarketOS's).
- The TAS2562 volume order is a mainline bug (`sound/soc/codecs/tas2562.c`,
  the same on master): worth sending upstream.
- `zrndevtest` (the device test app) crashes: it looks for the S9's parts.
- Settings' processor, graphics, memory and About pages show the S9's
  hardware (Exynos 9810, Mali-G72): written into Settings, not read.
- `/etc/os-release` still says BETA-1 / 0.2.0: BETA-1.1 left
  `os/overlay/etc/os-release` behind.
- Waydroid: no binderfs in the kernel config yet (`CONFIG_ANDROID_BINDERFS`
  in `kernel/ztos.config` would add it).

## How it got here

The first image was ztConflo's postmarketOS path (pmbootstrap's kernel,
initramfs and firmware under the ztOS GSI). The fixes it needed went into
ztConflo: the fastboot-bootpart flashing (FAT32 ESP on CACHE, U-Boot on
BOOT, DTBO erased), the Qualcomm services for the modem and Wi-Fi, ztoscsf's
component list, and `/etc/skel` for a user the GSI already has. This folder
then replaced postmarketOS's parts one at a time, each checked against that
working image: the kernel built here (config identical but for the
version), zboot instead of postmarketOS's initramfs (tried by flashing CACHE
alone on the running system, its data kept), the firmware fetched here
(byte for byte the same), and finally the whole system from the installer.

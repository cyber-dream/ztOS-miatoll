# ztOS on the Redmi Note 9 Pro / 9S (miatoll)

The SM7125 (Snapdragon 720G) Xiaomi phones: **joyeuse** (Redmi Note 9 Pro,
Global) and **curtana** (Redmi Note 9S). Built without postmarketOS: the
layout follows the Galaxy S9 (Snapdragon) port (Davitekk/ztOS,
`devices/samsung-starqltechn`).

Tried on a joyeuse with the Huaxing panel.

| | |
|---|---|
| Kernel | [sm7125-mainline](https://github.com/sm7125-mainline/linux) `sm7125-6.14.7-r2`, the tree and config postmarketOS ships, plus [`kernel/ztos.config`](kernel/ztos.config): [`kernel/build-kernel.sh`](kernel/build-kernel.sh) (clang, no root) |
| Boot | the Xiaomi bootloader starts U-Boot ([sm7125-mainline's release](https://github.com/sm7125-mainline/u-boot/releases)) from BOOT; U-Boot starts systemd-boot from CACHE (FAT32, `pmOS_boot`), which starts the kernel and zboot: [`boot/build-boot.sh`](boot/build-boot.sh). DTBO is erased; U-Boot gives the device tree |
| Boot stage | zboot ([`initramfs/init`](initramfs/init)): finds the `ZR_ARCH` root in USERDATA; if it cannot, a rescue shell on USB (telnet on 172.16.42.1), its log saved to CACHE |
| Firmware | [sm7125-mainline/firmware-xiaomi-miatoll](https://github.com/sm7125-mainline/firmware-xiaomi-miatoll) at postmarketOS's commit ([`firmware/fetch.sh`](firmware/fetch.sh)); the GPU's from linux-firmware-qcom |
| Modem, DSPs | qrtr and rmtfs (Arch Linux ARM), pd-mapper and tqftpserv built in [`layer.sh`](layer.sh) |
| ztOSIAL | [`vendor/ztos/manifest.toml`](vendor/ztos/manifest.toml): generic backends |

## Status

| Works | Not yet |
|---|---|
| boot, display (1080x2400), touch, GPU (Adreno 618, freedreno), Wi-Fi, speaker (UCM from sm7125-mainline), modem DSP up, battery level, charging (slowly: see below), USB networking, ztOSIAL's checks | headphones and microphones (in the profile, untried), calls and mobile data (no ModemManager in the GSI), sensors, fast charging, the clock across reboots |

Charging is the PMIC's own, at its default input limit: Linux has no driver
for the PM6150's charger, so ztOSIAL's Power1 tells charging from the
battery's current.

## Building and flashing

```
devices/xiaomi-miatoll/kernel/build-kernel.sh
installer/ztos build --target miatoll --user alarm
devices/xiaomi-miatoll/scripts/flash.sh          # erases the phone
devices/xiaomi-miatoll/scripts/flash.sh --boot-only   # a new kernel or boot stage alone
```

`scripts/upgrade.sh save` before a full flash and `restore` after the first
boot keep the user's data.

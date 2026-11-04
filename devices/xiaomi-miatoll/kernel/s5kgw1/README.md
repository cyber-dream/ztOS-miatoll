# S5KGW1, the main camera

The Redmi Note 9 Pro's 64 MP camera is a Samsung ISOCELL GW1 (S5KGW1), with a
DW9800 focus motor and a module EEPROM. Mainline has no driver for it; this is
one, out of tree.

**Status: the sensor runs, no frame arrives.** The driver finds the sensor and
starts it: its frame counter (0x0005) counts at the mode's rate, in D-PHY on 3
lanes (0x0111 = 2, 0x0114 = 2). CAMSS receives nothing: CSIPHY1's status shows
no high-speed clock on its clock lane, and CSID0 no packet, at 1318 and 2195
Mbps a lane, on 3 or 4 lanes, through CSIPHY1 or CSIPHY0.

## What is known

| | |
|---|---|
| I2C | 0x3d on CCI0 master 0 (`/dev/i2c-12`); answers only out of reset |
| ID | model 0x0971 at 0x0000, revision 0xA2 at 0x0002 (0x90, 0x03 at 0x0D, 0x0E) |
| Focus | DW9800 at 0x0c (ID 0xF2); mainline's `dw9800` |
| EEPROM | 0x51; 256 bytes read 8 at a time (CCI's limit: a 64-byte read fails) |
| Power | AVDD 2.8 V (PM6150L GPIO 9), DVDD (TLMM 42), IOVDD (L13A); MCLK1 at 19.2 MHz on GPIO 14; reset TLMM 25 |
| CSI | CSIPHY1, data lanes 0-2, not combo |

The power, pins and link are in [`../../boot/main-camera.dtsi`](../../boot/main-camera.dtsi).

## Building

The register tables are not in this repository. `gen-tables.py` writes them
(`s5kgw1-tables.h`) from the phone's own camera module file, which the user
fetches:

```sh
curl -LO https://raw.githubusercontent.com/TheMuppets/proprietary_vendor_xiaomi_miatoll/lineage-22.2/proprietary/vendor/lib64/camera/com.qti.sensormodule.sunny_s5kgw1.bin
python3 gen-tables.py com.qti.sensormodule.sunny_s5kgw1.bin > s5kgw1-tables.h
make -C ../../../../build/linux-sm7125-6.14.7-r2/src O=$PWD/../../../../build/linux-sm7125-6.14.7-r2/out ARCH=arm64 LLVM=1 M=$PWD modules
```

`start_mode=N` (module parameter) picks the mode it starts in: 0 is 4640x3472
at 30 fps, 1 is 2320x1736 at 60, 2 is 1920x1080 at 120.

## What was ruled out

- Samsung's own GW1 tables (the Exynos 9611 kernel's, for 26 MHz, PLLs
  recalculated): the sensor never starts with them.
- The PLLs, the output format (RAW10), the lane count and order: read back
  from the sensor while it streams.
- CAMSS's CSIPHY clock: it was 150 MHz, too slow for these links; fixed by
  [`../patch-camss-cphy-rx.py`](../patch-camss-cphy-rx.py). Not enough alone.
- The CSIPHY lane settings: Qualcomm's own for this PHY made no difference.

## Cautions

- With the CAMSS link in the device tree, CAMSS waits for this sensor before it
  makes `/dev/media0`: if the driver is missing or fails, every camera goes.
- Touch the sensor or its lens over I2C only while a CAMSS stream runs: woken
  with CAMSS off, the CCI can stay in a runtime-PM error until a reboot.

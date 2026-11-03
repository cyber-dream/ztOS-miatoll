#!/usr/bin/env python3
"""The phone's entry for ztOS's Supported Devices site (devices.ztos.online,
"Add your port"), as the form sends it, with the installation guide taken
from ../INSTALL.md so there is one copy of it:

    devices/xiaomi-miatoll/scripts/port-entry.py > devices/xiaomi-miatoll/port-entry.json

The form also needs a photo of the phone running ztOS and the maintainer's
name and contact; MAINTAINER and CONTACT below are filled in by hand.
Statuses: only what was tried on the phone itself is "working" or
"partial"; what is in place but not tried is "untested".
"""
import json
import pathlib

DEV = pathlib.Path(__file__).resolve().parent.parent
MAINTAINER = ''
CONTACT = ''

SUMMARY = (
    "ztOS on the Redmi Note 9 Pro (joyeuse) and Redmi Note 9S (curtana), Snapdragon 720G, without "
    "postmarketOS. Mainline Linux 6.14.7 from sm7125-mainline, with a fix for the speaker amplifier's "
    "volume and Landlock for pacman. Xiaomi's bootloader starts U-Boot from BOOT, then systemd-boot on "
    "CACHE, the kernel and ztOS's boot stage; the system lives in USERDATA. Display, touch, the Adreno "
    "GPU, Wi-Fi, the speaker and charging (at 1.4 A) work; the modem DSP runs, but calls, SMS, data and "
    "the sensors are not set up yet. Built on Linux in a user namespace, no root needed."
)

SPECS = [
    ['SoC', 'Qualcomm Snapdragon 720G (SM7125)'],
    ['CPU', '2× Kryo 465 Gold at 2.3 GHz, 6× Kryo 465 Silver at 1.8 GHz'],
    ['GPU', 'Adreno 618 (freedreno, OpenGL ES 3.2)'],
    ['RAM', '4, 6 or 8 GB'],
    ['Storage', '64 or 128 GB UFS 2.1, microSD'],
    ['Display', '6.67 in, 1080×2400 IPS LCD'],
    ['Battery', '5020 mAh'],
    ['Kernel', 'Mainline Linux 6.14.7 (sm7125-mainline)'],
    ['Released', '2020'],
    ['Device layer', 'devices/xiaomi-miatoll in github.com/cyber-dream/ztOS-miatoll'],
]

W, P, B, U = 'working', 'partial', 'broken', 'untested'
FEATURES = [
    ('Boot', 'Booting from internal storage', W,
     "Xiaomi's bootloader, U-Boot, systemd-boot, the kernel, ztOS's boot stage; the root is found in under 2 s"),
    ('Display', 'Panel, 1080x2400', W, 'Huaxing FT8756 (MSM DPU); the Tianma panel is built in but not tried'),
    ('Display', 'Brightness', W, 'ztOSIAL Display1'),
    ('Display', 'GPU', W, 'Adreno 618 through freedreno'),
    ('Input', 'Touchscreen', W, 'FT8756 over SPI, multi-touch'),
    ('Input', 'Power and volume keys', W, ''),
    ('Input', 'Vibration', U, ''),
    ('Power', 'CPU, frequency scaling', U, ''),
    ('Power', 'Battery and charging', P,
     "Level from the PM6150's fuel gauge; charging at 5 V and 1.4 A (0.4-0.5 A into the battery with the "
     "screen on): the PMIC charges by itself, there is no charger driver yet. No fast charging"),
    ('Power', 'Sleep (suspend)', U, ''),
    ('Power', 'Clock across reboots', P, "Not kept by the PMIC's RTC; never earlier than the build's date, NTP sets it once online"),
    ('Network', 'Wi-Fi', W, 'WCN3991 (ath10k), through the modem DSP'),
    ('Network', 'Bluetooth', U, ''),
    ('Network', 'Mobile data', B, 'Not set up yet: ModemManager over QRTR'),
    ('Network', 'USB networking', W, 'NCM: 172.16.42.1 (the computer gets 172.16.42.2), plus a serial console'),
    ('Modem', 'SIM and signal', B, 'The modem DSP and its QRTR services run; ModemManager is not set up yet'),
    ('Modem', 'SMS', B, 'Not set up yet'),
    ('Modem', 'Calls', B, 'Not set up yet'),
    ('Audio', 'Speaker', W,
     "TAS2562; media and system sounds through PipeWire. Needs a kernel fix: mainline's driver writes the "
     "amplifier's volume in the wrong order"),
    ('Audio', 'Microphone', U, 'In the sound profile (WCD9375), not tried'),
    ('Audio', 'Headphones', U, 'In the sound profile, not tried'),
    ('Sensors', 'Ambient light', B, "On the ADSP's sensor domain: not set up yet"),
    ('Sensors', 'Accelerometer, gyroscope', B, "On the ADSP's sensor domain: not set up yet"),
    ('Sensors', 'Proximity', B, "On the ADSP's sensor domain: not set up yet"),
    ('Camera', 'Cameras', B,
     'No picture yet. The front (OV16A1Q) and macro (S5K5E9) sensors are detected and give raw frames, '
     'which need libcamera; the main 64 MP, ultra-wide and depth cameras have no drivers'),
    ('Location', 'GPS', U, ''),
    ('Software', 'ztMETRO', W, ''),
    ('Software', 'Android apps (Waydroid)', B, 'No binderfs in the kernel yet'),
    ('Software', 'ztOSIAL (ztos-ial test)', W, '`ztos-ial check`: ok; Power1 from a vendor backend (charging told from the battery\'s current)'),
    ('Other', 'Fingerprint, IR, NFC', B, 'Not started'),
]

NOTES = [
    ('Charging is slow, and only up to 1.4 A.',
     'Linux has no driver for the PM6150\'s charger yet; the PMIC charges on its own at the input limit '
     'the bootloader sets. A long charge takes a few hours; a 9 V fast charger charges at 5 V.'),
    ("Settings shows the Galaxy S9's processor and graphics.",
     "Its processor, graphics and About pages are written for the S9 (Exynos 9810, Mali-G72). The phone's "
     "own facts are in its ztOSIAL manifest, for when Settings reads them."),
    ('The clock is wrong after a restart without internet.',
     'The PMIC\'s clock is not kept; it starts at the build\'s date until NTP sets it.'),
    ('No sound, while the volume panel says Headphones.',
     "The volume panel's output switch also offers the headphones when none are plugged in, and the "
     "choice is kept per app. Switch back to Speaker there."),
    ('A wrong battery level for a minute after a restart.',
     'The fuel gauge needs a minute to settle; it corrects itself.'),
]


def main():
    entry = {
        'name': 'Xiaomi Redmi Note 9 Pro / 9S',
        'maker': 'Xiaomi',
        'model': 'M2003J6B2G (9 Pro), M2003J6A1G (9S)',
        'codename': 'miatoll',
        'maintainer': MAINTAINER,
        'contact': CONTACT,
        'summary': SUMMARY,
        'specs': SPECS,
        'install': {'markdown': (DEV / 'INSTALL.md').read_text().strip()},
        'features': [{'category': c, 'feature': f, 'status': s, 'notes': n} for c, f, s, n in FEATURES],
        'notes': [{'title': t, 'text': x} for t, x in NOTES],
    }
    # the form's limits (devices-site/public/index.html)
    assert len(entry['summary']) <= 800, len(entry['summary'])
    assert len(entry['install']['markdown']) <= 30000
    assert all(len(f['notes']) <= 300 and len(f['feature']) <= 80 for f in entry['features'])
    assert all(len(n['title']) <= 120 and len(n['text']) <= 800 for n in entry['notes'])
    assert all(len(k) <= 40 and len(v) <= 200 for k, v in entry['specs'])
    print(json.dumps(entry, ensure_ascii=False, indent=1))


if __name__ == '__main__':
    main()

# LineageOS 24 (Android 17) for the Galaxy Note II (GT-N7100), on mainline Linux

LineageOS 24 running on Linux 7.3.1 with the open-source graphics stack (Mesa lima) on the
Exynos 4412 Prime: no Samsung kernel, no Mali blobs. Samsung's binaries are only used where
there is no open driver: the modem (boot daemon + RIL) and the GPS daemon, both behind HALs
written for this port.

## Status

| | |
|---|---|
| Display, brightness, auto-brightness | works (EA8061 panel) |
| GPU | works: Mali-400 MP4 with Mesa lima (OpenGL ES 2), drm_hwcomposer, minigbm |
| Touchscreen, S Pen, touch keys | work |
| Audio | works (WM1811: speaker, headphones, microphone; capture runs at the playback rate of 44.1 kHz) |
| Wi-Fi, Bluetooth | work (BCM4334) |
| Modem | XMM6262 boots, baseband and IMEI are reported; calls, SMS and mobile data are untested (no SIM was at hand) |
| GPS | the BCM4752 runs and reports NMEA; a fix outdoors is not verified yet |
| Sensors | work: accelerometer, gyroscope, magnetometer, barometer, light, proximity |
| Rear camera | preview, photos up to 2304x1296 and video (software H.264 encoder: about 11 fps at 960x720 through CameraX); the autofocus doesn't move the lens |
| Front camera | doesn't work: the FIMC-IS firmware from the stock ROM never opens the sensor; the mainline driver was written for the Tizen firmware, which isn't available (`device/samsung/n7100/camera/README.txt`) |
| Charging, battery, standby | work, with deep sleep; a cable plugged into the switched-off phone boots Android (no charger UI) |
| USB | adb, MTP; the gadget is gone while the phone is in deep sleep |
| USB OTG | implemented (role switch through the MAX77693 MUIC, 5 V boost), untested |
| NFC | not possible: the PN544 is an HCI chip, Android's NFC stack only speaks NCI |
| Hardware video decoding | H.264 up to 1080p30 on the MFC v5, through v4l2_codec2 (Codec2) with a decoder for it; FIMC converts its tiled frames. Other codecs and all encoders are software |
| Security | SELinux enforcing, file-based encryption (Adiantum: the Cortex-A9 has no AES instructions) |

CPU at 1.6 GHz (Prime operating points), GPU at 533 MHz, schedutil, a power HAL that raises
CPU/GPU/memory bus floors on touch and app launches.

## Installing

Needs TWRP in the recovery partition (3.7.0 tested) and the stock bootloader (sboot); the
partition table stays Samsung's.

1. Boot TWRP (Volume Up + Home + Power).
2. Install `lineage-24.0-<date>-UNOFFICIAL-n7100.zip`. Coming from stock or LineageOS 21, the
   zip clears /data and /metadata (Android formats and encrypts them on first boot); an update
   of this build keeps the data.
3. Reboot. The first boot takes a few minutes.

No GApps are included.

## Building

Linux (or WSL2) with the usual LineageOS build requirements, clang/lld for the kernel, Rust with
the `armv7-unknown-linux-musleabihf` target for n7100tool.

```bash
# 1. Samsung blobs and firmware, from LineageOS 21 for the n7100 and the stock N7100XXUFNE1 system
#    (a TWRP backup or an unpacked /system); checked against device/samsung/n7100/proprietary/blobs.sha1
tools/extract-blobs.sh 21.0-20250120-HTML6405-n7100.zip <stock system>

# 2. Kernel: Linux 7.3.1 + kernel/patches, into device/samsung/n7100/prebuilt
git clone -b v7.3.1 --depth 1 https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
tools/build-kernel.sh linux

# 3. LineageOS 24 (lineage-24.0): applies patches/, builds, packages the flash zip into out/
repo init -u https://github.com/LineageOS/android.git -b lineage-24.0 --git-lfs && repo sync
tools/build-android.sh <lineage tree> out
```

`N7100_DEVELOPMENT=true tools/build-android.sh ...` builds a debuggable userdebug that authorises
the adb key in `device/samsung/n7100/adb_keys` (not in the repository) and stays awake;
`tools/twrp-flash.sh` writes the images over adb from TWRP.

## Layout

- `kernel/` — `base.config` (postmarketOS' linux-postmarketos-exynos4), the Android and device
  fragments, and the patch series on top of v7.3.1: device tree, display panels, sensor hub,
  touch, S Pen, charger, modem (Samsung's modem_if ported), USB, and fixes.
- `device/samsung/n7100/` — the LineageOS device tree, including the HALs written for this port:
  radio (AIDL host for Samsung's libsec-ril), GNSS (around Samsung's legacy GPS HAL and gpsd),
  camera (from AOSP's external camera HAL), power, sensors, lights, vibrator. Each has its notes.
- `patches/` — changes to LineageOS repositories (`external/minigbm`, `external/v4l2_codec2`,
  `frameworks/av`, `packages/modules/Connectivity`, `vendor/aospext`); `patches/development/` only
  for development builds.
- `tools/` — extracting blobs, building, the flash zip; `tools/development/` debugging aids.
- `n7100tool/` — keeps the hardware watchdog fed.

## Credits

The mainline Exynos 4 work this builds on, in particular by Artur Weber, Henrik Grimler, Simon
Shields, Martin Jücker and the postmarketOS community; LineageOS and AOSP; GloDroid's aospext,
drm_hwcomposer and minigbm; Mesa's lima driver.

## License

The kernel patches are GPL-2.0 like the kernel. The device tree and tools are Apache-2.0 unless a
file says otherwise. Samsung's binaries are not part of this repository.

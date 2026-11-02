#!/bin/bash
# Extract the Samsung blobs the GT-N7100 build needs and apply the fixups that make them run on
# Android 17 (see android/device_samsung_n7100/proprietary/README). The blobs aren't
# redistributable, so they are not in this repository: run this once before building.
#
# usage: tools/extract-blobs.sh <lineage-21.zip> <stock-system>
#   lineage-21.zip  LineageOS 21 for the n7100 (21.0-20250120-HTML6405-n7100.zip): its vendor
#                   image has the modem boot daemon and Samsung's RIL
#   stock-system    Samsung's stock N7100XXUFNE1 /system: a TWRP backup folder
#                   (system.ext4.win000, ...) or an unpacked system directory
#
# needs: unzip, brotli ($BROTLI), python3, debugfs (e2fsprogs), readelf, patchelf 0.18 ($PATCHELF);
# an AOSP tree has brotli in out/host/linux-x86/bin and patchelf in prebuilts/extract-tools
set -euo pipefail

LOS21=$(realpath "$1")
STOCK=$(realpath "$2")
ROOT=$(realpath "$(dirname "$0")/..")
DT=$ROOT/device/samsung/n7100
P=$DT/proprietary
FW=$DT/prebuilt/firmware
PE=${PATCHELF:-patchelf}
BROTLI=${BROTLI:-brotli}
"$PE" --version | grep -q "0.18" || echo "warning: $("$PE" --version) instead of patchelf 0.18, checksums may differ"

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

echo "== LineageOS 21 vendor image"
unzip -q -o "$LOS21" vendor.new.dat.br vendor.transfer.list -d "$T"
"$BROTLI" -d -f "$T/vendor.new.dat.br" -o "$T/vendor.new.dat"
# sdat2img: write the "new" block ranges of the transfer list into a sparse image
python3 -I - "$T/vendor.transfer.list" "$T/vendor.new.dat" "$T/vendor.img" <<'EOF'
import sys
lst, dat, img = sys.argv[1:]
lines = open(lst).read().split("\n")
with open(dat, "rb") as src, open(img, "wb") as out:
    for line in lines[2:]:
        cmd = line.split(" ")
        if cmd[0] != "new":
            continue
        r = [int(x) for x in cmd[1].split(",")[1:]]
        for start, end in zip(r[0::2], r[1::2]):
            out.seek(start * 4096)
            out.write(src.read((end - start) * 4096))
EOF
mkdir -p "$P/bin" "$P/lib/hw" "$P/etc" "$FW/brcm"
dump() { debugfs -R "dump $1 $2" "$T/vendor.img" 2>/dev/null; [ -s "$2" ] || { echo "missing $1"; exit 1; }; }
dump /bin/cbd "$P/bin/cbd"
for l in libsec-ril.so libsecnativefeature.so libstlport.so libsqlite.so libexpat.so; do
    dump /lib/$l "$P/lib/$l"
done

echo "== stock N7100XXUFNE1 system"
S=$T/stock
mkdir -p "$S"
FILES="system/bin/gpsd system/lib/hw/gps.default.so system/bin/gps.cer system/etc/gps.xml
       system/etc/wifi/bcmdhd_sta.bin_b2 system/etc/wifi/nvram_net.txt_murata
       system/vendor/firmware/bcm4334_murata.hcd system/vendor/firmware/fimc_is_fw.bin
       system/vendor/firmware/setfile.bin system/vendor/firmware/mfc_fw.bin"
if ls "$STOCK"/system.ext4.win[0-9][0-9][0-9] >/dev/null 2>&1; then
    # TWRP splits the backup into complete tar archives (hence --ignore-zeros); member names
    # start with /system
    cat "$STOCK"/system.ext4.win[0-9][0-9][0-9] |
        tar -x --ignore-zeros -C "$S" $(for f in $FILES; do echo "/$f"; done)
else
    for f in $FILES; do mkdir -p "$S/$(dirname $f)"; cp "$STOCK/${f#system/}" "$S/$f"; done
fi
cp "$S/system/bin/gpsd" "$P/bin/"
cp "$S/system/lib/hw/gps.default.so" "$P/lib/hw/"
cp "$S/system/bin/gps.cer" "$P/etc/"
cp "$S/system/etc/wifi/bcmdhd_sta.bin_b2" "$FW/brcm/brcmfmac4334-sdio.bin"
cp "$S/system/etc/wifi/nvram_net.txt_murata" "$FW/brcm/brcmfmac4334-sdio.samsung,n7100.txt"
cp "$S/system/vendor/firmware/bcm4334_murata.hcd" "$FW/brcm/BCM4334B0.hcd"
cp "$S/system/vendor/firmware/fimc_is_fw.bin" "$FW/exynos4_fimc_is_fw.bin"
cp "$S/system/vendor/firmware/setfile.bin" "$FW/exynos4_s5k6a3_setfile.bin"
cp "$S/system/vendor/firmware/mfc_fw.bin" "$FW/s5p-mfc.fw"

echo "== fixups"
# libsec-ril: our RIL host instead of libril.so, our compat library instead of LineageOS'
# Android 10 libcutils copy; bionic's libstdc++ exists for vendor code only as libstdc++_vendor.so
# (in this order: patchelf's output depends on it, and blobs.sha1 on that)
"$PE" --replace-needed libril.so libril-n7100.so "$P/lib/libsec-ril.so"
for l in libsec-ril.so libstlport.so libsecnativefeature.so; do
    "$PE" --replace-needed libstdc++.so libstdc++_vendor.so "$P/lib/$l"
done
"$PE" --replace-needed libcutils-v29.so libsecril-compat.so "$P/lib/libsec-ril.so"
# gpsd: KitKat's libgui and libwrappergps come from gnss/compat (libgps-compat)
"$PE" --replace-needed libgui.so libgps-compat.so --remove-needed libicuuc.so \
    --remove-needed libwrappergps.so --replace-needed libstdc++.so libstdc++_vendor.so "$P/bin/gpsd"
"$PE" --replace-needed libstdc++.so libstdc++_vendor.so "$P/lib/hw/gps.default.so"
# KitKat crt objects: text relocations in gpsd's _start and the HAL's __on_dlclose
python3 -I "$P/fix-textrel.py" "$P/bin/gpsd"
python3 -I "$P/fix-textrel.py" "$P/lib/hw/gps.default.so"
# vendor code may not use /data/system or /data/misc (Treble SELinux)
python3 -I "$P/patch-paths.py" "$P/bin/gpsd" /data/system/gps/=/data/vendor/gps/
python3 -I "$P/patch-paths.py" "$P/lib/hw/gps.default.so" /data/system/gps/=/data/vendor/gps/
python3 -I "$P/patch-paths.py" "$P/lib/libsec-ril.so" /data/misc/radio=/data/vendor/ril
# gps.xml: GPS_PWR_EN is a GPIO LED here; data, logs (off unless cLogEnabled) and the TLS
# certificate move to /data/vendor and /vendor; FILE logging, as JAVA polls a socket forever
sed -e 's|gpioNStdbyPath="/sys/class/sec/gps/GPS_PWR_EN/value"|gpioNStdbyPath="/sys/class/leds/gps-pwr-en/brightness"|' \
    -e 's|acNvStoDir="/data/system/gps/"|acNvStoDir="/data/vendor/gps/"|' \
    -e 's|acLtoDir="/data/system/gps/"|acLtoDir="/data/vendor/gps/"|' \
    -e 's|FactoryTestFilePath="/data/"|FactoryTestFilePath="/data/vendor/gps/"|' \
    -e 's|ctrlPipe="/data/gps/glgpsctrl"|ctrlPipe="/data/vendor/gps/glgpsctrl"|' \
    -e 's|acLogDirectory="/storage/sdcard0/gps/broadcom/storage"|acLogDirectory="/data/vendor/gps/log/"|' \
    -e 's|tlsCertPath="/system/bin/gps.cer"|tlsCertPath="/vendor/etc/gps.cer"|' \
    -e 's|Log="JAVA"|Log="FILE"|' \
    -e 's/\r$//' \
    "$S/system/etc/gps.xml" > "$P/etc/gps.xml"

echo "== checksums"
cd "$DT"
if sha1sum --quiet -c "$P/blobs.sha1"; then
    echo "all blobs match"
else
    echo "checksum mismatch: different inputs or patchelf version"
    exit 1
fi

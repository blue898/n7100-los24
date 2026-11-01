#!/bin/bash
# Build the TWRP flash zip from exported images (tools/twrp_flash_android.sh's format):
#   boot.img, system.raw.gz, vendor.raw.gz and their .size/.sha1 files
# usage: tools/make-flashzip.sh <image dir> <out.zip>
set -euo pipefail
IMG=$(realpath "$1")
OUT=$(realpath -m "$2")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

mkdir -p "$T/META-INF/com/google/android"
cp "$IMG/boot.img" "$T/boot.img"
cp "$IMG/system.raw.gz" "$T/system.img.gz"
cp "$IMG/vendor.raw.gz" "$T/vendor.img.gz"
{
    echo "boot $(cat "$IMG/boot.img.size") $(cat "$IMG/boot.img.sha1")"
    echo "system $(cat "$IMG/system.raw.size") $(cat "$IMG/system.raw.sha1")"
    echo "vendor $(cat "$IMG/vendor.raw.size") $(cat "$IMG/vendor.raw.sha1")"
} > "$T/images.txt"
echo "# dummy, see update-binary" > "$T/META-INF/com/google/android/updater-script"

cat > "$T/META-INF/com/google/android/update-binary" <<'EOF'
#!/sbin/sh
# LineageOS 24 for the Galaxy Note II (GT-N7100), mainline kernel: writes the images to the
# Samsung partitions and checks them. Coming from another OS (stock, LineageOS 21), /data and
# /metadata are cleared so that Android sets them up (file-based encryption) on first boot.
OUTFD=$2
ZIP=$3
ui_print() { echo "ui_print $1" > /proc/self/fd/$OUTFD; echo "ui_print" > /proc/self/fd/$OUTFD; }
abort() { ui_print "ERROR: $1"; exit 1; }

part() {
    for d in /dev/block/by-name /dev/block/platform/*/by-name /dev/block/platform/*/*/by-name; do
        [ -e "$d/$1" ] && { readlink -f "$d/$1"; return; }
    done
}

ui_print "LineageOS 24 for the GT-N7100 (mainline Linux)"
BOOT=$(part BOOT); SYSTEM=$(part SYSTEM); VENDOR=$(part HIDDEN)
USERDATA=$(part USERDATA); OTA=$(part OTA)
[ -n "$BOOT" ] && [ -n "$SYSTEM" ] && [ -n "$VENDOR" ] && [ -n "$USERDATA" ] && [ -n "$OTA" ] ||
    abort "Samsung partitions BOOT/SYSTEM/HIDDEN/USERDATA/OTA not found: not a GT-N7100?"
grep -qi "smdk4x12\|n7100\|t03g" /proc/cmdline /proc/cpuinfo 2>/dev/null || abort "not a GT-N7100"

# Is this already our build (n7100, Android 17 or later: an update, keep the data)? LineageOS 21
# for the n7100 has the same device name, but SDK 34.
UPDATE=0
mkdir -p /tmp/los24sys
if mount -o ro -t ext4 "$SYSTEM" /tmp/los24sys 2>/dev/null; then
    P=/tmp/los24sys/system/build.prop
    SDK=$(sed -n "s/^ro.system.build.version.sdk=//p" $P 2>/dev/null)
    grep -q "^ro.product.system.device=n7100" $P 2>/dev/null && [ "${SDK:-0}" -ge 37 ] && UPDATE=1
    umount /tmp/los24sys
fi
umount /data 2>/dev/null; umount /system 2>/dev/null; umount /vendor 2>/dev/null

write() { # <name> <dev> <member> <gz>
    set -- $1 $2 $3 $4 $(grep "^$1 " /tmp/los24-images.txt | cut -d" " -f2,3)
    ui_print "Writing $1..."
    if [ "$4" = gz ]; then
        unzip -p "$ZIP" "$3" | gunzip -c | dd of="$2" bs=1048576 2>/dev/null || abort "writing $1 failed"
    else
        unzip -p "$ZIP" "$3" | dd of="$2" bs=1048576 2>/dev/null || abort "writing $1 failed"
    fi
    sync
    if [ $(($5 % 1048576)) -eq 0 ]; then
        got=$(dd if="$2" bs=1048576 count=$(($5 / 1048576)) 2>/dev/null | sha1sum | cut -d" " -f1)
    else
        got=$(head -c "$5" "$2" | sha1sum | cut -d" " -f1)
    fi
    [ "$got" = "$6" ] || abort "$1 doesn't match what was written (got $got)"
}

unzip -p "$ZIP" images.txt > /tmp/los24-images.txt || abort "images.txt missing"
write boot "$BOOT" boot.img raw
write system "$SYSTEM" system.img.gz gz
write vendor "$VENDOR" vendor.img.gz gz

if [ $UPDATE = 1 ]; then
    ui_print "Update of LineageOS 24: data kept"
else
    ui_print "New installation: clearing /data and /metadata"
    ui_print "(Android formats and encrypts them on first boot)"
    dd if=/dev/zero of="$USERDATA" bs=1048576 count=16 2>/dev/null
    dd if=/dev/zero of="$OTA" bs=1048576 count=8 2>/dev/null
    sync
fi
ui_print "Done. The first boot takes a few minutes."
exit 0
EOF
chmod 755 "$T/META-INF/com/google/android/update-binary"

rm -f "$OUT"
# images are stored (already compressed), the scripts deflated
(cd "$T" && zip -q -0 "$OUT" boot.img system.img.gz vendor.img.gz &&
    zip -q -9 "$OUT" images.txt META-INF/com/google/android/update-binary \
        META-INF/com/google/android/updater-script)
ls -la "$OUT"
sha256sum "$OUT" | tee "$OUT.sha256"

#!/bin/bash
# Build the flash zip (TWRP, LineageOS recovery) from exported images
# (tools/twrp_flash_android.sh's format): boot.img, system.raw.gz, vendor.raw.gz and their
# .size/.sha1 files, recovery.img if there is one.
# With LOS set (the LineageOS tree, built), the zip gets a whole-file signature with the AOSP
# test key, the one our test-keys builds use and LineageOS recovery checks zips against.
# usage: [LOS=~/los24] tools/make-flashzip.sh <image dir> <out.zip>
set -euo pipefail
IMG=$(realpath "$1")
OUT=$(realpath -m "$2")
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

mkdir -p "$T/META-INF/com/google/android"
cp "$IMG/boot.img" "$T/boot.img"
cp "$IMG/system.raw.gz" "$T/system.img.gz"
cp "$IMG/vendor.raw.gz" "$T/vendor.img.gz"
REC=
[ -f "$IMG/recovery.img" ] && { cp "$IMG/recovery.img" "$T/recovery.img"; REC=recovery.img; }
# images.txt: <name> <size> <sha1> <size in MiB, 0 if not whole MiB>. The MiB count is worked out
# here: the shell of LineageOS recovery (mksh on 32-bit ARM) computes with 32 bits, and system is
# 2 GiB.
line() { # <name> <size> <sha1>
    echo "$1 $2 $3 $(($2 % 1048576 ? 0 : $2 / 1048576))"
}
{
    line boot "$(cat "$IMG/boot.img.size")" "$(cat "$IMG/boot.img.sha1")"
    line system "$(cat "$IMG/system.raw.size")" "$(cat "$IMG/system.raw.sha1")"
    line vendor "$(cat "$IMG/vendor.raw.size")" "$(cat "$IMG/vendor.raw.sha1")"
    [ -z "$REC" ] || line recovery "$(stat -c %s "$IMG/recovery.img")" "$(sha1sum "$IMG/recovery.img" | cut -d' ' -f1)"
} > "$T/images.txt"
echo "# dummy, see update-binary" > "$T/META-INF/com/google/android/updater-script"

cat > "$T/META-INF/com/google/android/update-binary" <<'EOF'
#!/sbin/sh
# LineageOS 24 for the Galaxy Note II (GT-N7100), mainline kernel: writes the images to the
# Samsung partitions and checks them. Coming from another OS (stock, LineageOS 21), /data and
# /metadata are cleared so that Android sets them up (file-based encryption) on first boot.
OUTFD=$2
ZIP=$3
# what goes wrong, for a look over adb (or TWRP's file manager)
exec 2>/tmp/los24-install.log
ui_print() { echo "ui_print $1" > /proc/self/fd/$OUTFD; echo "ui_print" > /proc/self/fd/$OUTFD; }
abort() { ui_print "ERROR: $1"; exit 1; }

part() {
    for d in /dev/block/by-name /dev/block/platform/*/by-name /dev/block/platform/*/*/by-name; do
        [ -e "$d/$1" ] && { readlink -f "$d/$1"; return; }
    done
}

ui_print "LineageOS 24 for the GT-N7100 (mainline Linux)"
BOOT=$(part BOOT); SYSTEM=$(part SYSTEM); VENDOR=$(part HIDDEN)
USERDATA=$(part USERDATA); CACHE=$(part CACHE); OTA=$(part OTA)
[ -n "$BOOT" ] && [ -n "$SYSTEM" ] && [ -n "$VENDOR" ] && [ -n "$USERDATA" ] && [ -n "$CACHE" ] &&
    [ -n "$OTA" ] || abort "Samsung partitions BOOT/SYSTEM/HIDDEN/USERDATA/CACHE/OTA not found: not a GT-N7100?"
# grep -E: toybox (LineageOS recovery) has no \| in basic regular expressions
grep -qiE "smdk4x12|n7100|t03g" /proc/cmdline /proc/cpuinfo 2>/dev/null || abort "not a GT-N7100"
# The kernel carries the EA8061 panel's device tree only; with the S6EVR02 panel the screen stays
# black. TWRP's Samsung kernel picks the panel driver by the OLED ID pin and logs which one.
if dmesg 2>/dev/null | grep -qE "s6evr02 lcd panel driver has been probed|s6evr02_check_fb" ||
    readlink -f /sys/class/lcd/panel/device 2>/dev/null | grep -q s6evr02; then
    abort "this Note II has the S6EVR02 display panel; this build supports the EA8061 panel only"
fi

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
umount /data 2>/dev/null; umount /system 2>/dev/null; umount /vendor 2>/dev/null; umount /cache 2>/dev/null

write() { # <name> <dev> <member> <gz>; images.txt adds <size> <sha1> <MiB>
    set -- $1 $2 $3 $4 $(grep "^$1 " /tmp/los24-images.txt | cut -d" " -f2,3,4)
    ui_print "Writing $1..."
    if [ "$4" = gz ]; then
        unzip -p "$ZIP" "$3" | gunzip -c | dd of="$2" bs=1048576 2>/dev/null || abort "writing $1 failed"
    else
        unzip -p "$ZIP" "$3" | dd of="$2" bs=1048576 2>/dev/null || abort "writing $1 failed"
    fi
    sync
    if [ "${7:-0}" -gt 0 ]; then
        got=$(dd if="$2" bs=1048576 count=$7 2>/dev/null | sha1sum | cut -d" " -f1)
    else
        got=$(head -c "$5" "$2" | sha1sum | cut -d" " -f1)
    fi
    [ "$got" = "$6" ] || abort "$1 doesn't match what was written (got $got)"
}

unzip -p "$ZIP" images.txt > /tmp/los24-images.txt || abort "images.txt missing"
write boot "$BOOT" boot.img raw
write system "$SYSTEM" system.img.gz gz
write vendor "$VENDOR" vendor.img.gz gz

# Our recovery is two-stage: kernel and loader in RECOVERY, the ramdisk (with kernel modules)
# in the vendor partition just written, so it is updated along with it. Another recovery (TWRP)
# stays as it is.
RECOVERY=$(part RECOVERY)
if grep -q "^recovery " /tmp/los24-images.txt && [ -n "$RECOVERY" ]; then
    if [ "$(dd if="$RECOVERY" bs=1 skip=48 count=12 2>/dev/null)" = "n7100-2stage" ]; then
        write recovery "$RECOVERY" recovery.img raw
    else
        ui_print "Recovery: another one (TWRP?), left as it is"
    fi
fi

# /metadata is on CACHE, OTA holds the bootloader message (/misc). Builds before 2026-11-03 had
# /metadata on OTA: an update from one of them starts /metadata afresh.
OURMETA=0
mkdir -p /tmp/los24meta
if mount -o ro -t ext4 "$CACHE" /tmp/los24meta 2>/dev/null; then
    [ -d /tmp/los24meta/aconfig ] && OURMETA=1
    umount /tmp/los24meta
fi
if [ $UPDATE = 1 ]; then
    ui_print "Update of LineageOS 24: data kept"
else
    ui_print "New installation: clearing /data and /metadata"
    ui_print "(Android formats and encrypts them on first boot)"
    dd if=/dev/zero of="$USERDATA" bs=1048576 count=16 2>/dev/null
    OURMETA=0
fi
if [ $OURMETA = 0 ]; then
    dd if=/dev/zero of="$CACHE" bs=1048576 count=16 2>/dev/null
    dd if=/dev/zero of="$OTA" bs=1048576 count=8 2>/dev/null
fi
sync
ui_print "Done. The first boot takes a few minutes."
exit 0
EOF
chmod 755 "$T/META-INF/com/google/android/update-binary"

rm -f "$OUT" "$T/unsigned.zip"
# images are stored (already compressed), the scripts deflated
(cd "$T" && zip -q -0 unsigned.zip boot.img system.img.gz vendor.img.gz $REC &&
    zip -q -9 unsigned.zip images.txt META-INF/com/google/android/update-binary \
        META-INF/com/google/android/updater-script)
if [ -n "${LOS:-}" ]; then
    J=$(ls -d "$LOS"/prebuilts/jdk/jdk*/linux-x86/bin/java | sort -V | tail -1)
    K=$LOS/build/make/target/product/security/testkey
    "$J" -Djava.library.path="$LOS/out/host/linux-x86/lib64" \
        -jar "$LOS/out/host/linux-x86/framework/signapk.jar" -w "$K.x509.pem" "$K.pk8" \
        "$T/unsigned.zip" "$OUT"
else
    mv "$T/unsigned.zip" "$OUT"
fi
ls -la "$OUT"
sha256sum "$OUT" | tee "$OUT.sha256"

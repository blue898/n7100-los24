#!/bin/sh
# Flash images over adb from TWRP (development; for installs use the zip): boot.img -> BOOT,
# system -> SYSTEM, vendor -> HIDDEN (the N7100 PIT: mmcblk0p8, p13, p14), each checked by reading
# it back.
# usage: [ONLY="boot vendor"] tools/twrp-flash.sh <image dir> [--reboot]
#   image dir: tools/build-android.sh's output (boot.img, *.raw.gz, .size, .sha1)
set -e
export MSYS_NO_PATHCONV=1
O=$1
T=/data/n7100-flash
adb wait-for-recovery
adb shell "mkdir -p $T"

flash() { # <file> <block device> <gz:yes|no> <size> <sha1>
    echo "== $2 <- $1"
    dev=$2
    # adb under TWRP sometimes loses a few bytes of a large push: check it before writing
    want_push=$(sha1sum "$1" | cut -d' ' -f1)
    for try in 1 2 3 4; do
        adb push "$1" $T/img > /dev/null
        [ "$(adb shell "sha1sum $T/img" | cut -d' ' -f1)" = "$want_push" ] && break
        echo "   push $try damaged, again"
        [ $try = 4 ] && { echo "   push failed"; exit 1; }
    done
    if [ "$3" = yes ]; then
        adb shell "gunzip -c $T/img | dd of=$dev bs=1048576 2>&1 | tail -n1; sync"
    else
        adb shell "dd if=$T/img of=$dev bs=1048576 2>&1 | tail -n1; sync"
    fi
    adb shell "rm -f $T/img"
    # busybox head -c reads in tiny chunks: dd in MiB blocks where possible
    if [ $(($4 % 1048576)) -eq 0 ]; then
        got=$(adb shell "dd if=$dev bs=1048576 count=$(($4 / 1048576)) 2>/dev/null | sha1sum" | cut -d' ' -f1)
    else
        got=$(adb shell "head -c $4 $dev | sha1sum" | cut -d' ' -f1)
    fi
    [ "$got" = "$5" ] && echo "   verified $got" || { echo "   MISMATCH want $5 got $got"; exit 1; }
}

want() { [ -z "$ONLY" ] || echo " $ONLY " | grep -q " $1 "; }
want boot && flash "$O/boot.img" /dev/block/mmcblk0p8 no "$(cat "$O/boot.img.size")" "$(cat "$O/boot.img.sha1")"
want vendor && flash "$O/vendor.raw.gz" /dev/block/mmcblk0p14 yes "$(cat "$O/vendor.raw.size")" "$(cat "$O/vendor.raw.sha1")"
want system && flash "$O/system.raw.gz" /dev/block/mmcblk0p13 yes "$(cat "$O/system.raw.size")" "$(cat "$O/system.raw.sha1")"
adb shell "rmdir $T; sync"
[ "$2" = "--reboot" ] && adb reboot

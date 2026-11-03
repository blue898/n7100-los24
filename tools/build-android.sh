#!/bin/bash
# Build LineageOS 24 for the GT-N7100 and package it:
#   - applies patches/ to the LineageOS tree (once), builds n7100tool, puts the device tree into
#     <lineage>/device/samsung/n7100
#   - lunch lineage_n7100-<release>-userdebug, m bootimage recoveryimage systemimage vendorimage
#   - exports raw images (gzip, size, sha1) and recovery.img to <out>, and the flash zip (signed
#     with the AOSP test key, for TWRP and LineageOS recovery) next to them
#
# usage: tools/build-android.sh <lineage> <out>
#   lineage  a LineageOS lineage-24.0 tree (repo init -u https://github.com/LineageOS/android.git
#            -b lineage-24.0 --git-lfs && repo sync)
#   out      directory for the images and the zip
# before: tools/extract-blobs.sh and tools/build-kernel.sh
# N7100_DEVELOPMENT=true builds a debuggable userdebug with device/samsung/n7100/adb_keys
# authorised (patches/development as well); default: a release like LineageOS' own builds
set -euo pipefail
R=$(realpath "$(dirname "$0")/..")
L=$(realpath "$1")
O=$(realpath -m "$2")
DT=$R/device/samsung/n7100
export N7100_DEVELOPMENT=${N7100_DEVELOPMENT:-false}

[ -f "$DT/prebuilt/kernel/zImage-dtb" ] || { echo "no kernel: run tools/build-kernel.sh"; exit 1; }
(cd "$DT" && sha1sum --quiet -c proprietary/blobs.sha1) || { echo "blobs missing: run tools/extract-blobs.sh"; exit 1; }

# patches/<project path with _ for />-<what>.patch; a "_" that belongs to a directory name stays
# one (external_v4l2_codec2 -> external/v4l2_codec2)
project() { # <patch>
    local part path="" seg=""
    for part in $(basename "$1" | sed 's/-.*//; s/_/ /g'); do
        seg=${seg:+${seg}_}$part
        [ -d "$L/${path:+$path/}$seg" ] && path=${path:+$path/}$seg && seg=""
    done
    [ -z "$seg" ] || { echo "no project for $1" >&2; exit 1; }
    echo "$path"
}
apply() { # <patch> <reverse>
    local repo
    repo=$(project "$1") || exit 1
    if [ "$2" = reverse ]; then
        git -C "$L/$repo" apply -R --check "$1" 2>/dev/null && git -C "$L/$repo" apply -R "$1" || true
    else
        git -C "$L/$repo" apply --check "$1" 2>/dev/null && git -C "$L/$repo" apply "$1" || true
        git -C "$L/$repo" apply -R --check "$1" 2>/dev/null || { echo "$1 doesn't apply to $repo"; exit 1; }
    fi
}
for p in "$R"/patches/*.patch; do apply "$p" forward; done
for p in "$R"/patches/development/*.patch; do
    [ "$N7100_DEVELOPMENT" = true ] && apply "$p" forward || apply "$p" reverse
done

# n7100tool (hardware watchdog keeper), static ARM binary
mkdir -p "$DT/prebuilt/bin"
(cd "$R/n7100tool" && cargo build --release)
cp "$R/n7100tool/target/armv7-unknown-linux-musleabihf/release/n7100tool" "$DT/prebuilt/bin/"

mkdir -p "$L/device/samsung"
# no -t: copied files get a fresh mtime, else the build keeps stale PRODUCT_COPY_FILES outputs
rsync -rlpD --checksum --delete "$DT/" "$L/device/samsung/n7100/"

cd "$L"
set +u
source build/envsetup.sh > /dev/null
source vendor/lineage/vars/aosp_target_release
lunch "lineage_n7100-${aosp_target_release}-userdebug"
m bootimage recoveryimage systemimage vendorimage
set -u

mkdir -p "$O"
PO=$L/out/target/product/n7100
cp "$PO/boot.img" "$O/boot.img"
cp "$PO/recovery.img" "$O/recovery.img"
sha1sum "$O/boot.img" | cut -d' ' -f1 > "$O/boot.img.sha1"
stat -c %s "$O/boot.img" > "$O/boot.img.size"
for n in system vendor; do
    if file "$PO/$n.img" | grep -q "Android sparse"; then
        simg2img "$PO/$n.img" "$O/$n.raw"
    else
        cp "$PO/$n.img" "$O/$n.raw"
    fi
    sha1sum "$O/$n.raw" | cut -d' ' -f1 > "$O/$n.raw.sha1"
    stat -c %s "$O/$n.raw" > "$O/$n.raw.size"
    gzip -1 -f "$O/$n.raw"
done
LOS=$L "$R/tools/make-flashzip.sh" "$O" "$O/lineage-24.0-$(date +%Y%m%d)-UNOFFICIAL-n7100.zip"

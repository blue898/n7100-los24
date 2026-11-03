#!/bin/bash
# Autofocus experiment on the phone (release build with rooted debugging, or a development build):
# installs an s5c73m3.ko variant, reboots, checks which power-on sequence runs, then streams the
# rear camera with an AF scan in normal and macro range and rates the sharpness.
# The rear camera must look at something with detail (text) and the phone must stay still.
# usage: tools/afexp/afexp-run.sh <orig|A|B|C>   (variants: s5c-variant.py)
set -e
export MSYS_NO_PATHCONV=1 ANDROID_SERIAL=${ANDROID_SERIAL:-4d003387507e60b7}
D=$(cd "$(dirname "$0")" && pwd -W 2>/dev/null || pwd)
V=$1
OUT=$D/run-$V-$(date +%H%M)
mkdir -p "$OUT"
[ -f "$D/s5c73m3-$V.ko" ] || { echo "no $D/s5c73m3-$V.ko"; exit 1; }

boot_wait() {
    adb wait-for-device
    adb shell 'while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done'
    adb root >/dev/null 2>&1 || true
    sleep 3
    adb wait-for-device
    adb shell 'while [ "$(getprop sys.boot_completed)" != 1 ]; do sleep 2; done'
}

boot_wait
adb shell 'svc power stayon true; mount -o remount,rw /vendor'
adb shell '[ -f /data/local/tmp/s5c73m3.ko.release ] || cp /vendor/lib/modules/s5c73m3.ko /data/local/tmp/s5c73m3.ko.release'
adb push "$D/s5c73m3-$V.ko" /vendor/lib/modules/s5c73m3.ko >/dev/null
adb push "$D/afmeasure.sh" /data/local/tmp/afmeasure.sh >/dev/null
adb shell 'chmod 644 /vendor/lib/modules/s5c73m3.ko; restorecon /vendor/lib/modules/s5c73m3.ko; sync'
echo "== $V installed, rebooting"
adb reboot
sleep 20
boot_wait
adb shell 'logcat -b kernel -d | grep -i -E "s5c73m3|experiment" | head -8' | tee "$OUT/kernel-boot.txt"
sleep 15  # let the camera provider finish its probing opens
echo "== measuring"
adb shell 'sed -i "s/sleep [0-9]*$/sleep 3/" /data/local/tmp/afmeasure.sh; sh /data/local/tmp/afmeasure.sh' > "$OUT/run.txt" 2>&1 || true
adb pull /data/local/tmp/afm/log "$OUT/log" >/dev/null
adb pull /data/local/tmp/afm/f.raw "$OUT/f.raw" >/dev/null
grep -E "normal|macro|status: [1-4]" "$OUT/log" | awk '{print $NF}' | uniq -c | tr '\n' ' '; echo
python "$D/sharp.py" "$OUT/f.raw" "$OUT/log" | tail -6
# keep the last frame for a look, drop the raw stream (147 MB)
python - "$OUT/f.raw" "$OUT/last.y" <<'PY' || true
import sys
d = open(sys.argv[1], "rb").read(); n = len(d) // 614400
open(sys.argv[2], "wb").write(d[(n - 1) * 614400:(n - 1) * 614400 + 307200])
PY
rm -f "$OUT/f.raw"
echo "== results in $OUT"

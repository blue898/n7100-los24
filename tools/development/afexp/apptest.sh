#!/bin/bash
# Tap-to-focus in Aperture (video mode, torch on): screenshots before and after taps on the given
# screen points, the S5C73M3 AF controls polled meanwhile (afpoll.sh), CameraX's results
# usage: apptest.sh OUTDIR "x y" ...
export MSYS_NO_PATHCONV=1 ANDROID_SERIAL=${ANDROID_SERIAL:-4d003387507e60b7}
O=$1; shift; mkdir -p "$O"
adb push "$(dirname "$0")/afpoll.sh" /data/local/tmp/ >/dev/null
adb shell 'logcat -c; am start -n org.lineageos.aperture/.CameraLauncher >/dev/null 2>&1; sleep 5'
adb shell "input tap 610 917; sleep 4; (sh /data/local/tmp/afpoll.sh 150 > /data/local/tmp/afpoll.log &)
  screencap -p /data/local/tmp/c0.png; n=1
  for p in $(printf '"%s" ' "$@"); do input tap \$p; sleep 3.5; screencap -p /data/local/tmp/c\$n.png; n=\$((n+1)); done
  sleep 7; screencap -p /data/local/tmp/c\$n.png; input tap 610 917; wait"
for i in $(seq 0 $(($# + 1))); do adb pull /data/local/tmp/c$i.png "$O/" >/dev/null; done
adb shell cat /data/local/tmp/afpoll.log | uniq -c -f1 > "$O/afpoll.txt"
adb shell 'logcat -d | grep -E "Tap to focus started|onSuccess|reset"' > "$O/cameractl.txt"
cat "$O/afpoll.txt" "$O/cameractl.txt"

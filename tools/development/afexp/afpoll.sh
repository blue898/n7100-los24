#!/system/bin/sh
# Polls the S5C73M3 AF controls while an app uses the camera: continuous AF, AF area, status
T=/data/local/tmp
for d in /sys/class/video4linux/*; do [ "$(cat $d/name)" = S5C73M3 ] && SD=/dev/${d##*/}; done
for i in $(seq $1); do
  echo "$(date +%T.%N | cut -c1-11) $($T/v4l2-ctl -d $SD -C focus_automatic_continuous,auto_focus_status,auto_focus_area 2>&1 | tr '\n' ' ')"
  sleep 0.1
done
